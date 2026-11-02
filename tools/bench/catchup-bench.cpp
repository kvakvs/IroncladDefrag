// catchup-bench: the finish stage of a USN catch-up at the design target (T069, plan.md 6.5: catch-up in 10 s or
// less), without a real volume. Builds a synthetic 30 TB / 20 M-file snapshot the way the MFT scanner finishes one,
// picks re-read records (a "typical day"), deletes a third of those files, and times the catch-up finish as a patch of
// the saved snapshot (mft::finishCatchUp) against the full finish of every file, checking both give the same snapshot.
// The journal and record reads come on top; scan-bench measures the whole catch-up on a scale volume.
//
// Usage: catchup-bench [--files N] [--changes N] [--threads N] [--full 0|1]

#include "core/format/Count.h"
#include "core/ntfs/NtfsFormat.h"
#include "core/snapshot/FreeRunList.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftMerge.h"
#include "services/scan/ScanCompare.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <string>
#include <thread>

namespace {

    using namespace icd;
    using Clock = std::chrono::steady_clock;

    // Command-line settings.
    struct Args {
        std::uint32_t files = 20'000'000; // Regular files of the synthetic volume.
        std::size_t changes = 5'000; // Records re-read (about what a day of 10,000 changes names).
        std::size_t threads = 0; // Workers; 0: all processors but one.
        bool full = true; // Also time the full finish and compare.
    };

    // Seconds since start.
    double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

    // Parses "--name value" pairs; unknown names are ignored.
    Args parse(int argc, char** argv) {
        Args a;
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::string name = argv[i];
            const std::uint64_t value = std::strtoull(argv[i + 1], nullptr, 10);
            a.files = name == "--files" ? static_cast<std::uint32_t>(value) : a.files;
            a.changes = name == "--changes" ? value : a.changes;
            a.threads = name == "--threads" ? value : a.threads;
            a.full = name == "--full" ? value != 0 : a.full;
        }
        return a;
    }

    // A synthetic volume finished like an MFT scan (placeholder, metadata flags), as a saved snapshot would be.
    core::Snapshot savedSnapshot(const Args& a, core::Executor& executor) {
        core::SynthOptions o;
        o.geometry = {8192, (30ULL << 40U) / 8192}; // 30 TB needs 8 KB clusters on NTFS.
        o.files = a.files;
        o.fragmentedShare = 0.3;
        o.maxFragments = 40;
        const core::Snapshot s = core::generateSyntheticVolume(o);
        auto drafts = services::mft::draftsOfSnapshot(s, [](std::uint64_t) { return true; });
        return services::mft::finishDrafts(std::move(drafts), s.geometry,
                                           {s.freeSpace.runs().begin(), s.freeSpace.runs().end()}, executor)
            .snapshot;
    }

    // The re-read records and their fresh drafts: the saved files, a third of them (not directories) deleted.
    struct Day {
        std::vector<std::uint64_t> records; // Sorted.
        services::mft::DraftSet fresh; // In record order.
    };

    // Random re-read records of the saved snapshot.
    Day typicalDay(const core::Snapshot& s, std::size_t changes) {
        std::mt19937_64 random(1);
        std::set<std::uint64_t> reread;
        while (reread.size() < std::min(changes, s.files.size() - 1)) {
            reread.insert(core::ntfs::recordOf(s.files.fileReference(random() % (s.files.size() - 1))));
        }
        Day day{{reread.begin(), reread.end()}, {}};
        const auto all = services::mft::draftsOfSnapshot(s, [&](std::uint64_t r) { return reread.contains(r); });
        std::vector<core::FileIndex> to(all.files.size(), core::noFile);
        for (std::size_t i = 0; i < all.files.size(); ++i) {
            if (i % 3 != 0 || (all.files[i].flags & core::FileDirectory) != 0) {
                to[i] = static_cast<core::FileIndex>(day.fresh.files.size());
                day.fresh.files.push_back(all.files[i]);
            }
        }
        for (core::ExtentEntry e : all.extents) {
            if (to[e.file] != core::noFile) {
                e.file = to[e.file];
                day.fresh.extents.push_back(e);
            }
        }
        return day;
    }

    // The full finish of the same catch-up: every kept file drafted and finished again.
    core::Snapshot fullFinish(const core::Snapshot& s, const Day& day, core::Executor& executor) {
        const auto reread = [&](std::uint64_t r) { return std::ranges::binary_search(day.records, r); };
        auto kept = services::mft::draftsOfSnapshot(s, [&](std::uint64_t r) { return !reread(r); });
        std::vector<core::ClusterRun> free(s.freeSpace.runs().begin(), s.freeSpace.runs().end());
        for (const core::ExtentRecord& r : s.extents.records()) {
            const std::uint64_t reference = s.files.fileReference(r.file());
            if (reference != 0 && reread(core::ntfs::recordOf(reference))) {
                free.push_back(r.run());
            }
        }
        const core::FreeRunList merged(std::move(free), s.geometry.totalClusters);
        return services::mft::finishDrafts(services::mft::combineDrafts(std::move(kept), day.fresh), s.geometry,
                                           {merged.runs().begin(), merged.runs().end()}, executor)
            .snapshot;
    }

} // namespace

// Entry point.
int main(int argc, char** argv) {
    const Args a = parse(argc, argv);
    const std::size_t threads =
        a.threads != 0 ? a.threads : std::max<std::size_t>(1, std::thread::hardware_concurrency() - 1);
    services::ThreadPool pool(threads);
    auto start = Clock::now();
    const core::Snapshot saved = savedSnapshot(a, pool);
    std::printf("saved snapshot: %s files, %s extents, built in %.1f s, %zu workers\n",
                core::formatCount(saved.files.size()).c_str(), core::formatCount(saved.extents.size()).c_str(),
                since(start), threads);
    const Day day = typicalDay(saved, a.changes);
    core::Snapshot base = saved;
    start = Clock::now();
    auto patched = services::mft::finishCatchUp(base, day.fresh, day.records, pool);
    std::printf("  catch-up finish (patch)   %.2f s, %s records re-read   target: whole catch-up <= 10 s\n",
                since(start), core::formatCount(day.records.size()).c_str());
    if (!patched) {
        std::printf("The patch was refused.\n");
        return 1;
    }
    if (!a.full) {
        return 0;
    }
    start = Clock::now();
    const core::Snapshot full = fullFinish(saved, day, pool);
    std::printf("  full finish (every file)  %.2f s\n", since(start));
    const bool same = services::compareExact(full, *patched).same();
    std::printf("  patch = full finish       %s\n", same ? "yes" : "NO");
    return same ? 0 : 1;
}
