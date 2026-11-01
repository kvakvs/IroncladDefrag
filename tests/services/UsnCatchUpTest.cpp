// USN catch-up pieces (T065): when a saved snapshot can be caught up, drafts of a snapshot finish into the same
// snapshot, and combining drafts keeps record order. The end-to-end check (scripted changes, catch-up = full rescan)
// is tests/integration/catchup/check-catchup.ps1.

#include "core/ntfs/NtfsFormat.h"
#include "platform/win/VolumeBitmap.h"
#include "platform/win/Volumes.h"
#include "services/exec/ThreadPool.h"
#include "services/scan/MftMerge.h"
#include "services/scan/MftScanner.h"
#include "services/scan/ScanCompare.h"
#include "services/snapshot/UsnCatchUp.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    using core::ntfs::makeReference;

    // A snapshot with a cursor on a 1000-cluster volume.
    core::Snapshot withCursor(std::int64_t next) {
        core::Snapshot s;
        s.geometry = {4096, 1000};
        s.usn = core::UsnCursor{77, next};
        return s;
    }

    TEST(UsnCatchUp, BlockersFollowTheRescanRules) {
        const core::VolumeGeometry geometry{4096, 1000};
        const platform::UsnJournalState journal{
            .journalId = 77, .firstUsn = 1000, .nextUsn = 5000, .lowestValidUsn = 1000, .maximumSize = 1 << 20};
        EXPECT_FALSE(catchUpBlocker(withCursor(2000), journal, geometry));
        EXPECT_FALSE(catchUpBlocker(withCursor(5000), journal, geometry)); // Nothing new.
        EXPECT_TRUE(catchUpBlocker(core::Snapshot{}, journal, geometry)); // No cursor.
        EXPECT_TRUE(catchUpBlocker(withCursor(2000), std::nullopt, geometry)); // No journal.
        auto other = journal;
        other.journalId = 78;
        EXPECT_NE(catchUpBlocker(withCursor(2000), other, geometry)->find("recreated"), std::string::npos);
        EXPECT_NE(catchUpBlocker(withCursor(500), journal, geometry)->find("no longer"), std::string::npos); // Wrapped.
        EXPECT_TRUE(catchUpBlocker(withCursor(6000), journal, geometry)); // Ahead of the journal: not this one.
        EXPECT_NE(catchUpBlocker(withCursor(2000), journal, {4096, 2000})->find("size"), std::string::npos);
    }

    // A draft at a record.
    mft::FileDraft draft(std::uint64_t record, std::string_view name) {
        mft::FileDraft d;
        d.reference = makeReference(record, 1);
        d.parentReference = makeReference(5, 5);
        d.name = name;
        return d;
    }

    TEST(UsnCatchUp, CombineDraftsKeepsRecordOrderAndPrefersFresh) {
        mft::DraftSet kept;
        kept.files = {draft(5, ""), draft(20, "old"), draft(40, "keep")};
        kept.extents = {{.run = {10, 1}, .vcn = 0, .file = 1, .stream = 0, .flags = 0},
                        {.run = {30, 1}, .vcn = 0, .file = 2, .stream = 0, .flags = 0}};
        mft::DraftSet fresh;
        fresh.files = {draft(20, "new"), draft(30, "added")};
        fresh.extents = {{.run = {50, 2}, .vcn = 0, .file = 0, .stream = 0, .flags = 0}};
        const mft::DraftSet all = mft::combineDrafts(std::move(kept), std::move(fresh));
        ASSERT_EQ(all.files.size(), 4U);
        EXPECT_EQ(all.files[1].name, "new"); // The re-read record replaces the saved one.
        EXPECT_EQ(all.files[2].name, "added");
        EXPECT_EQ(all.files[3].name, "keep");
        ASSERT_EQ(all.extents.size(), 2U); // The replaced record's old extent is gone.
        EXPECT_EQ(all.extents[0].file, 3U);
        EXPECT_EQ(all.extents[1].file, 1U);
    }

    // A mounted ICD-TEST volume, when the tests can read it.
    std::optional<platform::VolumeRecord> testVolume() {
        if (!platform::isElevated()) {
            return std::nullopt;
        }
        for (auto& v : platform::enumerateVolumes()) {
            if (v.label.starts_with(L"ICD-TEST") && v.layout.ntfs) {
                return v;
            }
        }
        return std::nullopt;
    }

    // Drafts of a real scan's snapshot finish into the same snapshot; replacing some records by their re-read drafts
    // does too.
    TEST(UsnCatchUp, SnapshotDraftsFinishIntoTheSameSnapshot) {
        const auto v = testVolume();
        if (!v) {
            GTEST_SKIP() << "needs administrator rights and an ICD-TEST volume";
        }
        ThreadPool pool(4);
        const auto handle = platform::openVolume(v->volumeName, GENERIC_READ);
        const core::VolumeGeometry geometry{v->layout.bytesPerCluster, v->layout.totalClusters};
        const auto free = platform::readFreeRuns(handle.get(), geometry.totalClusters, pool);
        const std::vector<core::ClusterRun> freeRuns(free.runs().begin(), free.runs().end());
        const core::Snapshot scanned =
            scanFilesMft(handle.get(), v->volumeName, geometry, *v->layout.ntfs, freeRuns, pool, {}, nullptr).snapshot;
        const auto all = mft::draftsOfSnapshot(scanned, [](std::uint64_t) { return true; });
        const ScanComparison same = compareExact(scanned, mft::finishDrafts(all, geometry, freeRuns, pool).snapshot);
        EXPECT_TRUE(same.same()) << (same.samples.empty() ? "" : same.samples.front());

        std::vector<std::uint64_t> records;
        for (std::uint64_t r = 0; r < 400; r += 7) {
            records.push_back(r);
        }
        const MftRecordSet reread = readMftRecords(v->volumeName, geometry, *v->layout.ntfs, records, pool, {});
        const auto kept = mft::draftsOfSnapshot(scanned, [&](std::uint64_t r) { return r >= 400 || r % 7 != 0; });
        const auto fresh = mft::draftFiles(*reread.rows, reread.extensions, geometry, pool);
        const ScanComparison mixed = compareExact(
            scanned, mft::finishDrafts(mft::combineDrafts(kept, fresh), geometry, freeRuns, pool).snapshot);
        EXPECT_TRUE(mixed.same()) << (mixed.samples.empty() ? "" : mixed.samples.front());
    }

}} // namespace icd::services
