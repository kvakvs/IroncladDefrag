// VSS alignment (T090): copy-on-write units for 4 KB and 16 KB clusters, aligned moves, shifting destinations onto
// aligned positions, and the unaligned-move report.

#include "core/planner/VssAlign.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    TEST(VssAlign, UnitFollowsTheClusterSize) {
        EXPECT_EQ(vssUnit(4096), 4U);
        EXPECT_EQ(vssUnit(8192), 2U);
        EXPECT_EQ(vssUnit(16384), 1U);
        EXPECT_EQ(vssUnit(65536), 1U);
        EXPECT_EQ(vssUnit(512), 32U);
    }

    TEST(VssAlign, AlignedMovesKeepTheOffsetInEveryBlock) {
        const std::vector<ClusterRun> src{{6, 4}};
        EXPECT_TRUE(
            movesAligned(src, std::vector<ClusterRun>{{2, 4}}, {.unit = 4, .bytesPerCluster = 4096})); // 2 = 6 mod 4.
        EXPECT_FALSE(movesAligned(src, std::vector<ClusterRun>{{1, 4}}, {.unit = 4, .bytesPerCluster = 4096}));
        // Two source fragments into one run: each piece must keep its offset.
        EXPECT_TRUE(movesAligned(std::vector<ClusterRun>{{6, 2}, {12, 2}}, std::vector<ClusterRun>{{2, 4}},
                                 {.unit = 4, .bytesPerCluster = 4096}));
        EXPECT_FALSE(movesAligned(std::vector<ClusterRun>{{6, 2}, {13, 2}}, std::vector<ClusterRun>{{2, 4}},
                                  {.unit = 4, .bytesPerCluster = 4096}));
        EXPECT_TRUE(movesAligned(std::vector<ClusterRun>{{6, 2}, {12, 2}}, std::vector<ClusterRun>{{2, 2}, {8, 2}},
                                 {.unit = 4, .bytesPerCluster = 4096}));
        EXPECT_FALSE(movesAligned(std::vector<ClusterRun>{{4, 1}}, std::vector<ClusterRun>{{8, 1}},
                                  {.unit = 4, .bytesPerCluster = 4096})); // < 16 KB.
        EXPECT_TRUE(movesAligned(std::vector<ClusterRun>{{3, 1}}, std::vector<ClusterRun>{{8, 1}},
                                 {.unit = 1, .bytesPerCluster = 16384}));
    }

    TEST(VssAlign, DestinationsShiftOntoAlignedPositions) {
        //  0 .. 5 6 7 8 9 10 ..   A (4 clusters, 16 KB) at [6,10) planned into [1,5): one cluster off.
        const auto v = test::pictureVolume("......AAAA......");
        const FileIndex a = v.files.at('A');
        HoleSet pool(v.snapshot.freeSpace.runs());
        (void)pool.add({6, 4}); // A moves, so its clusters are free after the plan.
        (void)pool.take({1, 4});
        std::vector<Destination> d{{.file = a, .runs = {{1, 4}}, .reason = MoveReason::Relocate, .zone = noZone}};
        EXPECT_EQ(alignDestinations(v.snapshot, d, pool, nullptr), 1U);
        EXPECT_EQ(d[0].runs.front(), (ClusterRun{2, 4}));
        EXPECT_TRUE(pool.contains({1, 1})); // Given back.
        EXPECT_FALSE(pool.contains({5, 1})); // Taken.
        // On 16 KB clusters every move is aligned already.
        const auto big = test::pictureVolume("......AAAA......", {.clusterBytes = 16384});
        HoleSet bigPool(big.snapshot.freeSpace.runs());
        std::vector<Destination> bd{{.file = big.files.at('A'), .runs = {{1, 4}}}};
        EXPECT_EQ(alignDestinations(big.snapshot, bd, bigPool, nullptr), 0U);
        EXPECT_EQ(bd[0].runs.front(), (ClusterRun{1, 4}));
    }

    TEST(VssAlign, NoRoomMeansUnaligned) {
        //  B A A A A B ...   A planned into [0,4) between files: no free cluster to shift into... except its own.
        const auto v = test::pictureVolume("BAAAAB......");
        HoleSet pool(v.snapshot.freeSpace.runs());
        std::vector<Destination> d{{.file = v.files.at('A'), .runs = {{7, 4}}}}; // 7 vs 1: off by 2 either way.
        (void)pool.take({7, 4});
        (void)pool.take({6, 1});
        (void)pool.take({11, 1});
        EXPECT_EQ(alignDestinations(v.snapshot, d, pool, nullptr), 0U);
        Plan plan;
        plan.add({.file = v.files.at('A')}, std::vector<ClusterRun>{{1, 4}}, d[0].runs);
        plan.add({.file = v.files.at('B')}, std::vector<ClusterRun>{{0, 1}, {5, 1}}, std::vector<ClusterRun>{{8, 2}});
        const VssReport report = vssReport(plan, 4096);
        EXPECT_EQ(report.moves, 2U);
        EXPECT_EQ(report.unaligned, 2U); // A off by 2; B under 16 KB.
        EXPECT_EQ(report.growthBytes, 6U * 4096);
    }

}} // namespace icd::core::planner
