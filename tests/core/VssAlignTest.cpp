// VSS alignment (T090): copy-on-write units for 4 KB and 16 KB clusters, aligned moves, shifting destinations onto
// aligned positions, the unaligned-move report, and blocking unaligned moves (T121).

#include "core/planner/VssAlign.h"

#include "core/planner/Validator.h"
#include "core/planner/levels/FastLevel.h"
#include "core/planner/levels/MaximumLevel.h"
#include "core/planner/levels/ModerateLevel.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace icd::core::planner { namespace {

    // The plans of one level on a fragmented synthetic volume with shadow copies, without and with unaligned moves
    // blocked.
    struct BlockedPlans {
        Snapshot snapshot; // The volume.
        PlanResult open; // Unaligned moves allowed.
        PlanResult blocked; // Unaligned moves blocked.
    };

    // The levels the blocking test plans: Fast and Moderate (no parking, separate pipelines) and Maximum (parking).
    enum class Level : std::uint8_t { Fast, Moderate, Maximum };

    // Plans one level both ways.
    BlockedPlans planBothWays(Level level) {
        SynthOptions options;
        options.seed = 29;
        options.files = 2000;
        options.geometry.totalClusters = 1ULL << 20U;
        options.fillPercent = 75.0;
        options.fragmentedShare = 0.4;
        BlockedPlans out{.snapshot = generateSyntheticVolume(options), .open = {}, .blocked = {}};
        strategy::Zone photos = test::zone("Photos", {"photos"}, strategy::ZoneAction::Sort, strategy::Anchor::Front);
        photos.order = strategy::ZoneOrder::DirectoryName;
        const strategy::ZoneScript script =
            test::script({photos, test::zone("Rest", {}, strategy::ZoneAction::FastFill)});
        const auto classes = test::classesOf(out.snapshot);
        PlanInput input{.snapshot = &out.snapshot,
                        .script = &script,
                        .catalog = &classify::builtinCatalog(),
                        .classes = classes,
                        .churn = {},
                        .now = test::now};
        input.shadowCopies = true;
        InlineExecutor serial;
        const ZoneAssignment assignment = assignInput(input, serial);
        const auto plan = [&] {
            switch (level) {
            case Level::Fast:
                return planFast(input, assignment, serial);
            case Level::Moderate:
                return planModerate(input, assignment, serial);
            case Level::Maximum:
                break;
            }
            return planMaximum(input, assignment, serial);
        };
        out.open = plan();
        input.blockUnaligned = true;
        out.blocked = plan();
        return out;
    }

    // Blocking leaves no unaligned move, the plan still validates (dependents and parked pairs went with the dropped
    // moves), and the needed moves left out are reported as skipped for alignment or for waiting on one.
    TEST(VssAlign, BlockingLeavesOnlyAlignedMoves) {
        for (const Level level : {Level::Fast, Level::Moderate, Level::Maximum}) {
            const BlockedPlans p = planBothWays(level);
            const int maximum = static_cast<int>(level); // The level, for the failure messages.
            const std::uint32_t cluster = p.snapshot.geometry.bytesPerCluster;
            const VssReport open = vssReport(p.open.plan, cluster);
            ASSERT_GT(open.unaligned, 0U) << "level " << maximum; // Small files alone are never aligned.
            const VssReport blocked = vssReport(p.blocked.plan, cluster);
            EXPECT_EQ(blocked.unaligned, 0U) << "level " << maximum;
            EXPECT_GT(blocked.moves, 0U) << "level " << maximum;
            const Validation v = validatePlan(p.snapshot, p.blocked.plan, {.layout = &p.blocked.layout});
            EXPECT_TRUE(v.ok()) << (v.ok() ? "" : v.issues.front().message);
            EXPECT_TRUE(std::ranges::any_of(p.blocked.plan.skipped,
                                            [](const SkippedFile& s) { return s.reason == SkipReason::Unaligned; }));
            EXPECT_LT(p.blocked.plan.totals.clustersMoved, p.open.plan.totals.clustersMoved);
        }
    }

    // A drop reaches every move that would land on clusters a dropped move leaves taken, and both moves of a parked
    // file; aligned moves that depend on nothing dropped stay.
    TEST(VssAlign, DroppingFollowsDependencies) {
        Plan plan;
        plan.lowerBound = 4 + 4 + 4 + 1;
        // 0: file 0, aligned [4,8) -> [100,104). 1: file 1, unaligned (1 cluster) [20,21) -> [200,201).
        // 2: file 2, aligned, lands on file 1's sources [16,24) -> [16+4..): overlaps [20,21) -> dropped too.
        // 3: file 3, aligned [40,44) -> [300,304), independent.
        plan.add({.file = 0, .clusters = 4}, std::vector<ClusterRun>{{4, 4}}, std::vector<ClusterRun>{{100, 4}});
        plan.add({.file = 1, .clusters = 1}, std::vector<ClusterRun>{{20, 1}}, std::vector<ClusterRun>{{200, 1}});
        plan.add({.file = 2, .clusters = 4}, std::vector<ClusterRun>{{48, 4}}, std::vector<ClusterRun>{{20, 4}});
        plan.add({.file = 3, .clusters = 4}, std::vector<ClusterRun>{{40, 4}}, std::vector<ClusterRun>{{300, 4}});
        EXPECT_EQ(dropUnaligned(plan, 4096), 2U);
        ASSERT_EQ(plan.moves.size(), 2U);
        EXPECT_EQ(plan.moves[0].file, 0U);
        EXPECT_EQ(plan.moves[1].file, 3U);
        ASSERT_EQ(plan.skipped.size(), 2U);
        EXPECT_EQ(plan.skipped[0].reason, SkipReason::Unaligned);
        EXPECT_EQ(plan.skipped[1].reason, SkipReason::Dependency);
        EXPECT_EQ(plan.skippedClusters, 5U);

        // A parked file: an aligned park whose return is unaligned goes as a pair, and so does a move after the park
        // that lands on the park's sources.
        Plan parked;
        parked.add({.file = 5, .reason = MoveReason::Park, .extra = true, .clusters = 4},
                   std::vector<ClusterRun>{{8, 4}}, std::vector<ClusterRun>{{400, 4}});
        parked.add({.file = 6, .clusters = 4}, std::vector<ClusterRun>{{60, 4}}, std::vector<ClusterRun>{{8, 4}});
        parked.add({.file = 5, .fromParking = true, .clusters = 4}, std::vector<ClusterRun>{{400, 4}},
                   std::vector<ClusterRun>{{61, 4}});
        EXPECT_EQ(dropUnaligned(parked, 4096), 3U);
        EXPECT_TRUE(parked.moves.empty());
        EXPECT_EQ(parked.extra, 0U);
        EXPECT_EQ(parked.skipped.size(), 2U); // The park was extra: no skip entry for it.
    }

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
