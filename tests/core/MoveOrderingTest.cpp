// Move ordering and cycles (T088): chains come out in dependency order, cycles are parked at Maximum (smallest file,
// one extra move) or dropped with their dependents otherwise; every file moves once except reported parking. Also
// "move the smaller thing".

#include "core/planner/MakeRoom.h"
#include "core/planner/MoveOrdering.h"
#include "core/planner/Validator.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    // A destination of one file into zone 0 (a zone target: cycles among these park at Maximum).
    Destination to(FileIndex file, std::vector<ClusterRun> runs) {
        return {.file = file, .runs = std::move(runs), .reason = MoveReason::Relocate, .zone = 0};
    }

    // Orders destinations into a plan with a lower bound and validates it.
    struct Ordered {
        Plan plan;
        OrderReport report;
        Validation validation;
    };

    // Runs the orderer.
    Ordered order(const test::PictureVolume& v, const std::vector<Destination>& d, bool parking) {
        Ordered out;
        for (const Destination& x : d) {
            out.plan.lowerBound += clustersOf(x.runs);
        }
        out.report = orderMoves(v.snapshot, d, {.allowParking = parking}, out.plan);
        finishTotals(out.plan, v.snapshot);
        out.validation = validatePlan(v.snapshot, out.plan);
        return out;
    }

    TEST(MoveOrdering, ChainsComeOutInDependencyOrder) {
        //  A B C .    A -> B's place, B -> C's place, C -> the free cluster: C, B, A.
        const auto v = test::pictureVolume("ABC.");
        const auto& f = v.files;
        const std::vector<Destination> d{to(f.at('A'), {{1, 1}}), to(f.at('B'), {{2, 1}}), to(f.at('C'), {{3, 1}})};
        const Ordered o = order(v, d, false);
        ASSERT_TRUE(o.validation.ok()) << o.validation.issues.front().message;
        ASSERT_EQ(o.plan.moves.size(), 3U);
        EXPECT_EQ(o.plan.moves[0].file, f.at('C'));
        EXPECT_EQ(o.plan.moves[1].file, f.at('B'));
        EXPECT_EQ(o.plan.moves[2].file, f.at('A'));
        EXPECT_EQ(o.report.cycles, 0U);
    }

    TEST(MoveOrdering, SwapIsParkedAtMaximumAndDroppedOtherwise) {
        const auto v = test::pictureVolume("AB..");
        const auto& f = v.files;
        const std::vector<Destination> d{to(f.at('A'), {{1, 1}}), to(f.at('B'), {{0, 1}})};
        const Ordered parked = order(v, d, true);
        ASSERT_TRUE(parked.validation.ok()) << parked.validation.issues.front().message;
        EXPECT_EQ(parked.report.parked, 1U);
        ASSERT_EQ(parked.plan.moves.size(), 3U);
        EXPECT_EQ(parked.plan.moves[0].reason, MoveReason::Park);
        EXPECT_EQ(parked.plan.moves[0].file, parked.plan.moves[2].file); // The parked file returns last.
        EXPECT_TRUE(parked.plan.moves[2].fromParking);
        EXPECT_EQ(parked.plan.extra, 1U);
        const Ordered redirected = order(v, d, false);
        ASSERT_TRUE(redirected.validation.ok()) << redirected.validation.issues.front().message;
        EXPECT_EQ(redirected.report.redirected, 1U); // A goes to free space for good; B then takes A's place.
        ASSERT_EQ(redirected.plan.moves.size(), 2U);
        EXPECT_EQ(redirected.plan.destinations(redirected.plan.moves[0]).front(), (ClusterRun{2, 1}));
        EXPECT_EQ(redirected.plan.extra, 0U);
        EXPECT_TRUE(redirected.plan.skipped.empty());
    }

    TEST(MoveOrdering, CyclesWithoutAZoneAreRedirectedEvenAtMaximum) {
        // Defragmenting needs the file in one piece anywhere: free space reaches that in one move, no parking.
        const auto v = test::pictureVolume("AB..");
        auto d = std::vector<Destination>{to(v.files.at('A'), {{1, 1}}), to(v.files.at('B'), {{0, 1}})};
        for (Destination& x : d) {
            x.zone = noZone;
        }
        const Ordered o = order(v, d, true);
        ASSERT_TRUE(o.validation.ok()) << o.validation.issues.front().message;
        EXPECT_EQ(o.report.parked, 0U);
        EXPECT_EQ(o.report.redirected, 1U);
        EXPECT_EQ(o.plan.extra, 0U);
    }

    TEST(MoveOrdering, CyclesParkTheSmallestFileAndDropTheirDependents) {
        //  0 1 2 3 4 5 6     A -> [2,4) (B and the free cluster), B -> [0,1), F -> [1,2) waits for A.
        //  A A B . F . .
        const auto v = test::pictureVolume("AAB.F..");
        const auto& f = v.files;
        const std::vector<Destination> d{to(f.at('A'), {{2, 2}}), to(f.at('B'), {{0, 1}}), to(f.at('F'), {{1, 1}})};
        const Ordered parked = order(v, d, true);
        ASSERT_TRUE(parked.validation.ok()) << parked.validation.issues.front().message;
        EXPECT_EQ(parked.plan.moves[0].file, f.at('B')); // B (1 cluster) is smaller than A (2).
        EXPECT_EQ(parked.plan.moves[0].reason, MoveReason::Park);
        EXPECT_EQ(parked.plan.extra, 1U);
        EXPECT_EQ(parked.plan.moves.size(), 4U);
        const Ordered redirected = order(v, d, false);
        ASSERT_TRUE(redirected.validation.ok()) << redirected.validation.issues.front().message;
        EXPECT_EQ(redirected.report.redirected, 1U);
        EXPECT_EQ(redirected.plan.moves.size(), 3U); // B to free space, then A and F as planned.
        EXPECT_TRUE(redirected.plan.skipped.empty());
    }

    TEST(MoveOrdering, ThreeCycleWithIndependentChain) {
        //  A B C D E . .    A->1, B->2, C->0 (a cycle); D->5, E->3 (a chain after D).
        const auto v = test::pictureVolume("ABCDE..");
        const auto& f = v.files;
        const std::vector<Destination> d{to(f.at('A'), {{1, 1}}), to(f.at('B'), {{2, 1}}), to(f.at('C'), {{0, 1}}),
                                         to(f.at('E'), {{3, 1}}), to(f.at('D'), {{5, 1}})};
        const Ordered parked = order(v, d, true);
        ASSERT_TRUE(parked.validation.ok()) << parked.validation.issues.front().message;
        EXPECT_EQ(parked.report.cycles, 1U);
        EXPECT_EQ(parked.plan.moves.size(), 6U);
        const Ordered redirected = order(v, d, false);
        ASSERT_TRUE(redirected.validation.ok()) << redirected.validation.issues.front().message;
        EXPECT_EQ(redirected.plan.moves.size(), 5U); // No extra move: one file of the cycle ends in free space.
        EXPECT_EQ(redirected.report.redirected, 1U);
    }

    TEST(MoveOrdering, WithoutScratchSpaceCyclesAreDroppedWithTheirDependents) {
        const auto v = test::pictureVolume("AB");
        const auto& f = v.files;
        const std::vector<Destination> d{to(f.at('A'), {{1, 1}}), to(f.at('B'), {{0, 1}})};
        for (const bool parking : {true, false}) {
            const Ordered o = order(v, d, parking);
            EXPECT_TRUE(o.validation.ok());
            EXPECT_EQ(o.report.parked + o.report.redirected, 0U);
            EXPECT_EQ(o.plan.skippedClusters, 2U);
            EXPECT_EQ(o.plan.skipped.front().reason, SkipReason::Cycle);
        }
        //  A A B F    A -> [2,4) waits for B and F, B -> [0,1), F -> [1,2): no free cluster at all.
        const auto w = test::pictureVolume("AABF");
        const std::vector<Destination> e{to(w.files.at('A'), {{2, 2}}), to(w.files.at('B'), {{0, 1}}),
                                         to(w.files.at('F'), {{1, 1}})};
        const Ordered o = order(w, e, false);
        EXPECT_TRUE(o.validation.ok());
        EXPECT_EQ(o.report.dropped, 3U);
        EXPECT_EQ(o.plan.skippedClusters, 4U);
    }

    TEST(MakeRoom, EvictsTheFewestClusters) {
        //  0 1 2 3 4 5 6 7
        //  S S X X . T . .     a 3-cluster window: [4,7) evicts only T.
        const auto v = test::pictureVolume("SSXX.T..");
        const HoleSet pool(v.snapshot.freeSpace.runs());
        const std::vector<ClusterRun> region{{0, 8}};
        const auto room = findRoom(v.snapshot, pool, {.clusters = 3, .own = {}, .region = region, .maxEvict = 2},
                                   [](FileIndex) { return true; });
        ASSERT_TRUE(room);
        EXPECT_EQ(room->window, (ClusterRun{4, 3}));
        EXPECT_EQ(room->blockers, (std::vector<FileIndex>{v.files.at('T')}));
        EXPECT_EQ(room->evicted, 1U);
        const FileIndex t = v.files.at('T');
        const auto noT = findRoom(v.snapshot, pool, {.clusters = 3, .own = {}, .region = region, .maxEvict = 2},
                                  [&](FileIndex file) { return file != t; });
        ASSERT_TRUE(noT);
        EXPECT_EQ(noT->window, (ClusterRun{2, 3})); // X and the hole at 4.
        EXPECT_FALSE(findRoom(v.snapshot, pool, {.clusters = 3, .own = {}, .region = region, .maxEvict = 0},
                              [](FileIndex) { return true; }));
    }

}} // namespace icd::core::planner
