// core::patchSnapshot (T069) on a hand-made volume: released clusters become free, the placeholder keeps what nobody
// took, new extents land in both, and patches that cannot give a fresh build's result are refused.

#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/SnapshotPatch.h"

#include <gtest/gtest.h>

#include <vector>

namespace icd::core { namespace {

    // Base: root (0), a (1) at 10-19, b (2) at 30-39 and 60-64, placeholder (3) at 40-49; free 0-9, 20-29, 50-59,
    // 65-99.
    Snapshot base() {
        SnapshotBuilder b({4096, 100});
        b.addFile({"", noFile, 0, FileDirectory, {}, 5});
        b.addFile({"a", 0, 10, 0, {}, 20});
        b.addFile({"b", 0, 20, 0, {}, 30});
        b.addFile({"$Unattributed", 0, 0, FileMetadata | FileUnmovable, {}, 0});
        b.addExtent({{10, 10}, 0, 1, 0, 0});
        b.addExtent({{30, 10}, 0, 2, 0, 0});
        b.addExtent({{60, 5}, 10, 2, 0, 0});
        b.addExtent({{40, 10}, 0, 3, 0, ExtentUnmovable});
        for (const ClusterRun run : {ClusterRun{0, 10}, {20, 10}, {50, 10}, {65, 35}}) {
            b.addFreeRun(run);
        }
        return std::move(b).build();
    }

    // The new snapshot's columns: root and a continued, c new (b deleted), then the placeholder.
    struct Columns {
        std::vector<FileIndex> baseOf{0, 1, noFile, noFile}; // Root, a, c, placeholder.
        std::vector<FileIndex> parents{noFile, 0, 0, 0}; // All under the root.
        std::vector<std::uint16_t> flags{FileDirectory, 0, 0, FileMetadata | FileUnmovable}; // As in the base.

        // A patch over these columns: c takes 35-44 (b's old clusters and the placeholder's).
        SnapshotPatch patch() const {
            SnapshotPatch p;
            p.baseOf = baseOf;
            p.parents = parents;
            p.flags = flags;
            p.newFiles = {{"c", noFile, 7, 0, {}, 40}, {"$Unattributed", 0, 0, 0, {}, 0}};
            p.extents = {{{35, 10}, 0, 2, 0, 0}};
            p.baseUnattributed = 3;
            p.unattributed = 3;
            return p;
        }
    };

    TEST(SnapshotPatch, ReleasesTakesAndKeepsUnclaimedClusters) {
        Snapshot b = base();
        const Columns columns;
        InlineExecutor inline_;
        const auto out = patchSnapshot(b, columns.patch(), inline_);
        ASSERT_TRUE(out.has_value());
        ASSERT_EQ(out->files.size(), 4U);
        EXPECT_EQ(out->names.name(out->files.name(2)), "c");
        EXPECT_EQ(out->files.allocatedClusters(1), 10U); // a kept its extent.
        EXPECT_EQ(out->files.allocatedClusters(2), 10U);
        EXPECT_EQ(out->files.allocatedClusters(3), 5U); // Placeholder: 45-49 left.
        const auto free = out->freeSpace.runs();
        const std::vector<ClusterRun> expectedFree{{0, 10}, {20, 15}, {50, 50}}; // b's 30-34 and 60-64 freed.
        EXPECT_TRUE(std::ranges::equal(
            free, expectedFree, [](ClusterRun x, ClusterRun y) { return x.start == y.start && x.length == y.length; }));
        ASSERT_EQ(out->extents.size(), 3U);
        EXPECT_EQ(out->extents.records()[1].file(), 2U); // c at 35, between a and the placeholder.
        EXPECT_EQ(out->extents.records()[2].run().start, 45U);
        EXPECT_EQ(out->extents.extentsOf(3).size(), 1U);
    }

    TEST(SnapshotPatch, RefusesWhatAFreshBuildWouldDoDifferently) {
        InlineExecutor inline_;
        const auto refused = [&](auto change) {
            Snapshot b = base();
            Columns columns;
            SnapshotPatch p = columns.patch();
            change(columns, p);
            p.baseOf = columns.baseOf;
            p.flags = columns.flags;
            const bool result = patchSnapshot(b, std::move(p), inline_).has_value();
            if (!result) { // Untouched (a successful patch spends the base).
                EXPECT_EQ(b.files.size(), 4U);
                EXPECT_EQ(b.names.name(b.files.name(2)), "b");
            }
            return !result;
        };
        EXPECT_TRUE(refused([](Columns&, SnapshotPatch& p) { p.extents[0].run = {15, 3}; })); // Over a's extent.
        EXPECT_TRUE(refused([](Columns& c, SnapshotPatch&) { c.flags[1] = FileCompressed; })); // a's flags changed.
        EXPECT_TRUE(refused([](Columns& c, SnapshotPatch&) { c.baseOf[0] = 1; })); // a continued twice.
        EXPECT_TRUE(refused([](Columns&, SnapshotPatch& p) { p.newFiles.pop_back(); })); // Columns disagree.
        EXPECT_TRUE(refused([](Columns&, SnapshotPatch& p) { p.unattributed = noFile; })); // Placeholder runs left.
        EXPECT_TRUE(refused([](Columns&, SnapshotPatch& p) {
            p.extents.push_back({{38, 4}, 10, 2, 0, 0}); // New extents overlap each other.
        }));
        EXPECT_FALSE(refused([](Columns&, SnapshotPatch&) {}));
    }

}} // namespace icd::core
