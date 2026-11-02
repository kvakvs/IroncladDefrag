// The catch-up finish as a patch of the saved snapshot (T069): on random volumes and random changes it must give the
// full finish's snapshot exactly, for any executor and over several rounds, and refuse (leaving the saved snapshot
// untouched) where it cannot.

#include "core/ntfs/NtfsFormat.h"
#include "core/snapshot/FreeRunList.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftMerge.h"
#include "services/scan/ScanCompare.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <map>
#include <random>
#include <set>
#include <string>

namespace icd::services { namespace {

    using core::ntfs::makeReference;
    using core::ntfs::recordOf;

    // Volume size of the test volumes.
    constexpr core::VolumeGeometry geometry{4096, 400'000};

    // Record of the $Extend directory, whose files are metadata.
    constexpr std::uint64_t extendRecord = 11;

    // A random volume as drafts (what a scan reads) plus the names they point at.
    struct Volume {
        std::deque<std::string> names; // Owns the draft names.
        mft::DraftSet drafts; // In record order.
        std::vector<core::ClusterRun> free; // Free runs; the other gaps are unclaimed clusters.
    };

    // Builds random volumes and random changes to them.
    class Maker {
    public:
        explicit Maker(std::uint64_t seed) : random_(seed) {}

        // A volume of about `files` files in a random tree, with gaps (free or unclaimed), unused records, resident
        // files, named streams and a metadata directory.
        Volume volume(std::size_t files) {
            Volume v;
            for (std::uint64_t r = 0; r < 12; ++r) {
                addSystem(v, r);
            }
            for (std::uint64_t r = 16; r < 16 + files; ++r) {
                if (chance(0.1)) {
                    continue; // An unused record: new files may appear here later.
                }
                addFile(v, r, randomParent(v, r));
            }
            v.free.push_back({cursor_, geometry.totalClusters - cursor_});
            return v;
        }

        // A file draft at a record with random facts and extents.
        void addFile(Volume& v, std::uint64_t record, std::uint64_t parentReference) {
            mft::FileDraft d;
            d.reference = makeReference(record, static_cast<std::uint16_t>(1 + record % 5));
            d.parentReference = parentReference;
            d.name = name(v);
            d.flags = chance(0.15) ? std::uint16_t{core::FileDirectory} : std::uint16_t{0};
            d.sizeBytes = random_() % 1'000'000;
            d.times = {static_cast<std::int64_t>(random_() % 1000), static_cast<std::int64_t>(random_() % 1000), 0};
            d.mainSeen = true;
            const auto file = static_cast<core::FileIndex>(v.drafts.files.size());
            if (!chance(0.1)) {
                placeExtents(v, file, d);
            }
            v.drafts.files.push_back(d);
        }

        // Random extents for a draft at the cursor, with a gap before each (free or unclaimed).
        void placeExtents(Volume& v, core::FileIndex file, mft::FileDraft& d) {
            const bool named = (d.flags & core::FileDirectory) == 0 && chance(0.1);
            d.flags |= named ? std::uint16_t{core::FileAlternateStreams} : std::uint16_t{0};
            for (std::uint16_t stream = 0; stream <= (named ? 1 : 0); ++stream) {
                std::uint64_t vcn = 0;
                for (std::uint64_t k = 0, pieces = 1 + random_() % 4; k < pieces; ++k) {
                    gap(v);
                    const core::ClusterCount length = 1 + random_() % 40;
                    v.drafts.extents.push_back({{cursor_, length}, vcn, file, stream, 0});
                    cursor_ += length;
                    vcn += length;
                }
            }
            d.clusters = true;
        }

        // Advances the cursor by a random gap, free or unclaimed.
        void gap(Volume& v) {
            const core::ClusterCount length = random_() % 8;
            if (length != 0 && chance(0.75)) {
                v.free.push_back({cursor_, length});
            }
            cursor_ += length;
        }

        // A system record (root at 5, $Extend at 11 as a directory).
        void addSystem(Volume& v, std::uint64_t record) {
            mft::FileDraft d;
            d.reference = makeReference(record, record == 0 ? 1 : static_cast<std::uint16_t>(record));
            d.parentReference = makeReference(5, 5);
            d.name = record == 5 ? std::string_view() : v.names.emplace_back("$Sys" + std::to_string(record));
            const bool directory = record == 5 || record == extendRecord;
            d.flags = directory ? std::uint16_t{core::FileDirectory} : std::uint16_t{0};
            d.mainSeen = true;
            const auto file = static_cast<core::FileIndex>(v.drafts.files.size());
            if (record == 0 || directory) {
                placeExtents(v, file, d);
            }
            v.drafts.files.push_back(d);
        }

        // A directory of the volume (or $Extend now and then) for a new file at `record`.
        std::uint64_t randomParent(const Volume& v, std::uint64_t record) {
            std::vector<std::uint64_t> dirs;
            for (const mft::FileDraft& d : v.drafts.files) {
                if ((d.flags & core::FileDirectory) != 0 && recordOf(d.reference) != record) {
                    dirs.push_back(d.reference);
                }
            }
            return dirs[random_() % dirs.size()];
        }

        // A name from a small set (names repeat, as on real volumes), sometimes a new one.
        std::string_view name(Volume& v) {
            const std::uint64_t n = random_() % 40;
            return n == 0 ? v.names.emplace_back("unique-" + std::to_string(random_()))
                          : v.names.emplace_back("name-" + std::to_string(n) + ".txt");
        }

        // True with probability p.
        bool chance(double p) { return std::uniform_real_distribution<double>(0, 1)(random_) < p; }

        // Random index below n.
        std::size_t below(std::size_t n) { return random_() % n; }

        // The random source.
        std::mt19937_64& random() { return random_; }

    private:
        std::mt19937_64 random_; // Seeded.
        core::Lcn cursor_ = 16; // Next LCN to place at.
    };

    // A snapshot finished from a volume, as a full MFT scan makes it.
    core::Snapshot finished(Volume v) {
        core::InlineExecutor inline_;
        return mft::finishDrafts(std::move(v.drafts), geometry, std::move(v.free), inline_).snapshot;
    }

    // One catch-up's input: the re-read records and their fresh drafts.
    struct Changes {
        std::deque<std::string> names; // Owns new names.
        std::vector<std::uint64_t> records; // Re-read, sorted.
        mft::DraftSet fresh; // Drafts of the re-read records that still exist, in record order.
    };

    // Clusters fresh files may take: free space, unclaimed clusters and the old clusters of moved and deleted files.
    class Space {
    public:
        // Every gap of the snapshot (free or unclaimed) is available.
        explicit Space(const core::Snapshot& s) {
            for (const core::ExtentRecord& r : s.extents.records()) {
                const bool placeholder = s.files.fileReference(r.file()) == 0;
                if (placeholder) {
                    runs_.push_back(r.run());
                }
            }
            runs_.insert(runs_.end(), s.freeSpace.runs().begin(), s.freeSpace.runs().end());
        }

        // Makes runs available.
        void add(std::span<const core::ClusterRun> runs) { runs_.insert(runs_.end(), runs.begin(), runs.end()); }

        // Takes up to `length` clusters from a random available run; empty when nothing is left.
        core::ClusterRun take(core::ClusterCount length, std::mt19937_64& random) {
            std::erase_if(runs_, [](const core::ClusterRun& r) { return r.length == 0; });
            if (runs_.empty()) {
                return {};
            }
            core::ClusterRun& from = runs_[random() % runs_.size()];
            const core::ClusterRun taken{from.start, std::min(length, from.length)};
            from.start += taken.length;
            from.length -= taken.length;
            return taken;
        }

    private:
        std::vector<core::ClusterRun> runs_; // Available, disjoint.
    };

    // Random changes to a snapshot: re-read records whose files are kept, renamed or moved to another directory,
    // resized, given new extents, or deleted, and new files (some at unused records in the middle).
    Changes randomChanges(const core::Snapshot& s, Maker& maker, double share) {
        Changes c;
        std::set<std::uint64_t> reread{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        std::set<std::uint64_t> used;
        for (core::FileIndex f = 0; f + 1 < s.files.size(); ++f) {
            used.insert(recordOf(s.files.fileReference(f)));
            if (maker.chance(share)) {
                reread.insert(recordOf(s.files.fileReference(f)));
            }
        }
        mft::DraftSet drafts = mft::draftsOfSnapshot(s, [&](std::uint64_t r) { return reread.contains(r); });
        Space space(s);
        std::map<std::uint64_t, std::pair<mft::FileDraft, std::vector<core::ExtentEntry>>> out;
        std::vector<std::uint64_t> dirs; // Directories outside the metadata tree.
        std::set<std::uint64_t> metadata; // References of metadata files.
        for (core::FileIndex f = 0; f < s.files.size(); ++f) {
            const std::uint64_t reference = s.files.fileReference(f);
            if (s.files.has(f, core::FileMetadata)) {
                metadata.insert(reference);
            }
            else if (s.files.has(f, core::FileDirectory) && !reread.contains(recordOf(reference))) {
                dirs.push_back(reference);
            }
        }
        std::vector<std::vector<core::ExtentEntry>> extentsOf(drafts.files.size());
        for (const core::ExtentEntry& e : drafts.extents) {
            extentsOf[e.file].push_back(e);
        }
        std::vector<std::pair<mft::FileDraft, std::vector<core::ExtentEntry>>> moved;
        for (std::size_t i = 0; i < drafts.files.size(); ++i) {
            mft::FileDraft d = drafts.files[i];
            const std::uint64_t record = recordOf(d.reference);
            const bool system = record < 16;
            const bool directory = (d.flags & core::FileDirectory) != 0;
            if (!system && !directory && maker.chance(0.25)) {
                std::vector<core::ClusterRun> runs;
                for (const auto& e : extentsOf[i]) {
                    runs.push_back(e.run);
                }
                space.add(runs); // Deleted.
                continue;
            }
            if (!system && !metadata.contains(d.reference) && maker.chance(0.3)) {
                // Renamed and moved, never into or out of the metadata tree: kept files below would change their
                // metadata flag, which the patch refuses (RefusesWhenKeptFilesBecomeMetadata).
                d.name = c.names.emplace_back("renamed-" + std::to_string(maker.random()() % 7));
                d.parentReference = dirs[maker.below(dirs.size())];
            }
            if (!system && maker.chance(0.3)) {
                d.sizeBytes += 4096;
                d.times.modified += 100;
            }
            if (!system && !directory && !extentsOf[i].empty() && maker.chance(0.3)) {
                std::vector<core::ClusterRun> runs;
                for (const auto& e : extentsOf[i]) {
                    runs.push_back(e.run);
                }
                space.add(runs);
                moved.emplace_back(d, std::vector<core::ExtentEntry>{});
                continue;
            }
            out[record] = {d, extentsOf[i]};
            if (directory && !metadata.contains(d.reference)) {
                dirs.push_back(d.reference);
            }
        }
        const auto place = [&](mft::FileDraft& d, std::vector<core::ExtentEntry>& extents) {
            std::uint64_t vcn = 0;
            for (int k = 0; k < 3; ++k) {
                const core::ClusterRun run = space.take(1 + maker.below(30), maker.random());
                if (run.length == 0) {
                    break;
                }
                extents.push_back({run, vcn, 0, 0, 0});
                vcn += run.length;
            }
            d.clusters = !extents.empty();
        };
        for (auto& [d, extents] : moved) {
            place(d, extents);
            out[recordOf(d.reference)] = {d, extents};
        }
        const std::uint64_t top = used.empty() ? 16 : *used.rbegin() + 1;
        for (std::uint64_t r = 16; r < top + 20; ++r) {
            if (used.contains(r) || out.contains(r) || !maker.chance(r < top ? 0.3 : 0.5)) {
                continue;
            }
            mft::FileDraft d;
            d.reference = makeReference(r, 9);
            d.parentReference = dirs[maker.below(dirs.size())];
            d.name = c.names.emplace_back("new-" + std::to_string(r));
            d.mainSeen = true;
            std::vector<core::ExtentEntry> extents;
            place(d, extents);
            out[r] = {d, extents};
            reread.insert(r);
        }
        for (auto& [record, entry] : out) {
            const auto file = static_cast<core::FileIndex>(c.fresh.files.size());
            c.fresh.files.push_back(entry.first);
            for (core::ExtentEntry e : entry.second) {
                e.file = file;
                c.fresh.extents.push_back(e);
            }
        }
        c.records.assign(reread.begin(), reread.end());
        return c;
    }

    // What the full finish makes of the saved snapshot and the changes (the reference).
    core::Snapshot fullFinish(const core::Snapshot& saved, const Changes& c) {
        const auto reread = [&](std::uint64_t r) { return std::ranges::binary_search(c.records, r); };
        mft::DraftSet kept = mft::draftsOfSnapshot(saved, [&](std::uint64_t r) { return !reread(r); });
        std::vector<core::ClusterRun> free(saved.freeSpace.runs().begin(), saved.freeSpace.runs().end());
        for (const core::ExtentRecord& r : saved.extents.records()) {
            const std::uint64_t reference = saved.files.fileReference(r.file());
            if (reference != 0 && reread(recordOf(reference))) {
                free.push_back(r.run());
            }
        }
        const core::FreeRunList merged(std::move(free), geometry.totalClusters);
        core::InlineExecutor inline_;
        return mft::finishDrafts(mft::combineDrafts(std::move(kept), c.fresh), geometry,
                                 {merged.runs().begin(), merged.runs().end()}, inline_)
            .snapshot;
    }

    // Expects two snapshots to be equal, with the first differences in the message.
    void expectSame(const core::Snapshot& a, const core::Snapshot& b) {
        const ScanComparison same = compareExact(a, b);
        EXPECT_TRUE(same.same()) << same.differences
                                 << " differences, first: " << (same.samples.empty() ? "" : same.samples.front());
    }

    TEST(CatchUpPatch, EqualsTheFullFinishOnRandomChanges) {
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            SCOPED_TRACE(seed);
            Maker maker(seed);
            core::Snapshot saved = finished(maker.volume(1500));
            const Changes c = randomChanges(saved, maker, 0.05 + 0.05 * static_cast<double>(seed % 4));
            const core::Snapshot expected = fullFinish(saved, c);
            core::InlineExecutor inline_;
            auto patched = mft::finishCatchUp(saved, c.fresh, c.records, inline_);
            ASSERT_TRUE(patched.has_value());
            expectSame(expected, *patched);
        }
    }

    TEST(CatchUpPatch, AnyExecutorAndSeveralRoundsGiveTheSame) {
        Maker maker(99);
        core::Snapshot saved = finished(maker.volume(3000));
        ThreadPool pool(4);
        for (int round = 0; round < 4; ++round) {
            SCOPED_TRACE(round);
            const Changes c = randomChanges(saved, maker, 0.1);
            const core::Snapshot expected = fullFinish(saved, c);
            core::Snapshot copy = saved;
            core::InlineExecutor inline_;
            auto serial = mft::finishCatchUp(copy, c.fresh, c.records, inline_);
            auto parallel = mft::finishCatchUp(saved, c.fresh, c.records, pool);
            ASSERT_TRUE(serial && parallel);
            expectSame(expected, *parallel);
            expectSame(*serial, *parallel);
            EXPECT_EQ(serial->names.size(), parallel->names.size());
            saved = std::move(*parallel); // The next round patches the patched snapshot.
        }
    }

    // Index of the first fresh draft whose record is in the saved snapshot and is not a system record, and which has
    // extents.
    std::size_t freshWithExtents(const Changes& c) {
        for (std::size_t i = 0; i < c.fresh.files.size(); ++i) {
            const bool has = std::ranges::any_of(c.fresh.extents, [&](const auto& e) { return e.file == i; });
            if (recordOf(c.fresh.files[i].reference) >= 16 && has) {
                return i;
            }
        }
        return c.fresh.files.size();
    }

    TEST(CatchUpPatch, RefusesFreshExtentsOverKeptOnes) {
        Maker maker(5);
        core::Snapshot saved = finished(maker.volume(1500));
        Changes c = randomChanges(saved, maker, 0.05);
        const std::size_t i = freshWithExtents(c);
        ASSERT_LT(i, c.fresh.files.size());
        // A kept file's first cluster (not re-read: moved files would have released it).
        core::ClusterRun kept{};
        for (const core::ExtentRecord& r : saved.extents.records()) {
            const std::uint64_t reference = saved.files.fileReference(r.file());
            if (reference != 0 && !std::ranges::binary_search(c.records, recordOf(reference))) {
                kept = {r.lcn(), 1};
                break;
            }
        }
        c.fresh.extents.push_back({kept, 1000, static_cast<core::FileIndex>(i), 0, 0});
        const core::Snapshot before = saved;
        core::InlineExecutor inline_;
        EXPECT_FALSE(mft::finishCatchUp(saved, c.fresh, c.records, inline_).has_value());
        expectSame(before, saved); // Untouched: the caller takes the full path.
    }

    TEST(CatchUpPatch, RefusesWhenKeptFilesBecomeMetadata) {
        Maker maker(6);
        core::Snapshot saved = finished(maker.volume(1500));
        Changes c = randomChanges(saved, maker, 0.05);
        // Move a re-read directory that has kept children under $Extend: those children turn into metadata.
        bool moved = false;
        for (mft::FileDraft& d : c.fresh.files) {
            const std::uint64_t record = recordOf(d.reference);
            if (record < 16 || (d.flags & core::FileDirectory) == 0 || moved) {
                continue;
            }
            for (core::FileIndex f = 0; f + 1 < saved.files.size() && !moved; ++f) {
                const core::FileIndex p = saved.files.parent(f);
                const bool keptChild = p != core::noFile && saved.files.fileReference(p) == d.reference &&
                    !saved.files.has(p, core::FileMetadata) &&
                    !std::ranges::binary_search(c.records, recordOf(saved.files.fileReference(f)));
                moved = keptChild;
            }
            d.parentReference = moved ? makeReference(extendRecord, extendRecord) : d.parentReference;
        }
        ASSERT_TRUE(moved);
        core::InlineExecutor inline_;
        EXPECT_FALSE(mft::finishCatchUp(saved, c.fresh, c.records, inline_).has_value());
    }

}} // namespace icd::services
