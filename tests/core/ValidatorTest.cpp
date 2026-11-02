// Plan model and validator (T082): a valid plan passes, and deliberately broken plans are caught for every rule of
// plan.md 9.5.

#include "core/planner/Validator.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    using test::pictureVolume;

    // A plan under construction.
    class PlanMaker {
    public:
        // Appends a move.
        PlanMaker& move(FileIndex file, std::vector<ClusterRun> from, std::vector<ClusterRun> to,
                        MoveReason reason = MoveReason::Defragment, ZoneIndex zone = noZone) {
            plan_.add({.file = file, .reason = reason, .zone = zone}, from, to);
            return *this;
        }

        // Appends the return of a parked file.
        PlanMaker& unpark(FileIndex file, std::vector<ClusterRun> from, std::vector<ClusterRun> to) {
            plan_.add({.file = file, .reason = MoveReason::Relocate, .fromParking = true}, from, to);
            return *this;
        }

        // The plan with lower bound, extra and totals filled in.
        Plan done(const Snapshot& snapshot, ClusterCount lowerBound, ClusterCount extra = 0) {
            plan_.lowerBound = lowerBound;
            plan_.extra = extra;
            finishTotals(plan_, snapshot);
            return plan_;
        }

    private:
        Plan plan_; // Being built.
    };

    // Whether any issue mentions part.
    bool mentions(const Validation& v, std::string_view part) {
        return std::ranges::any_of(v.issues,
                                   [&](const ValidationIssue& i) { return i.message.find(part) != std::string::npos; });
    }

    // A two-file volume: A in two fragments, B at the end.
    //   0 1 2 3 4 5 6 7
    //   A . A . . . B B
    struct Fixture {
        test::PictureVolume v = pictureVolume("A.A...BB");
        FileIndex a = v.files.at('A');
        FileIndex b = v.files.at('B');
    };

    TEST(Validator, AcceptsAValidChain) {
        Fixture f;
        const Plan plan =
            PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{3, 2}}).move(f.b, {{6, 2}}, {{0, 2}}).done(f.v.snapshot, 4);
        const Validation v = validatePlan(f.v.snapshot, plan);
        EXPECT_TRUE(v.ok()) << v.issues.front().message;
        EXPECT_EQ(v.totals.clustersMoved, 4U);
        EXPECT_EQ(v.totals.fragmentsBefore, 3U); // A had two, B one.
        EXPECT_EQ(v.totals.fragmentsAfter, 2U);
        EXPECT_EQ(v.freeClusters, 4U);
        EXPECT_EQ(v.freeRuns, 2U); // [2,3) and [5,8).
        EXPECT_EQ(plan.totals, v.totals);
    }

    TEST(Validator, CatchesDestinationsThatAreNotFreeYet) {
        Fixture f;
        // B into [0,2) before A has left cluster 0.
        const Plan plan =
            PlanMaker().move(f.b, {{6, 2}}, {{0, 2}}).move(f.a, {{0, 1}, {2, 1}}, {{3, 2}}).done(f.v.snapshot, 4);
        const Validation v = validatePlan(f.v.snapshot, plan);
        EXPECT_TRUE(mentions(v, "move 1 (\\B): destination [0, 2) is not free at its move"));
        const Plan onto = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{5, 2}}).done(f.v.snapshot, 2); // Onto B.
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, onto), "not free"));
        const Plan outside = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{7, 1}, {8, 1}}).done(f.v.snapshot, 2);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, outside), "[8, 9) is not free"));
    }

    TEST(Validator, CatchesUnmovableFilesAndStreams) {
        const auto v = pictureVolume("#A..");
        const FileIndex mft = v.files.at('#');
        const Plan plan = PlanMaker().move(mft, {{0, 1}}, {{2, 1}}).done(v.snapshot, 1);
        const Validation result = validatePlan(v.snapshot, plan);
        EXPECT_TRUE(mentions(result, "must not move"));
        EXPECT_TRUE(mentions(result, "sources are not")); // Unmovable extents are never a move's sources.
    }

    TEST(Validator, CatchesStaleSourcesAndSecondMoves) {
        Fixture f;
        const Plan twice = PlanMaker()
                               .move(f.a, {{0, 1}, {2, 1}}, {{3, 2}})
                               .move(f.a, {{0, 1}, {2, 1}}, {{0, 2}}) // Stale: A is at [3,5) now.
                               .done(f.v.snapshot, 2, 0);
        const Validation v = validatePlan(f.v.snapshot, twice);
        EXPECT_TRUE(mentions(v, "move 2 (\\A): its sources are not the file's current movable runs"));
        EXPECT_TRUE(mentions(v, "moves a file a second time"));
        const Plan reordered = PlanMaker().move(f.a, {{2, 1}, {0, 1}}, {{3, 2}}).done(f.v.snapshot, 2); // VCN order.
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, reordered), "sources are not"));
    }

    TEST(Validator, CatchesLostClusters) {
        Fixture f;
        const Plan plan = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{3, 1}}).done(f.v.snapshot, 2);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, plan), "does not keep the file's cluster count"));
    }

    TEST(Validator, CatchesMovesOutsideTheirZone) {
        const auto v = pictureVolume("..A.A.BB", {.classes = {{'A', "photos"}, {'B', "archives"}}});
        const auto s = test::script({test::zone("Photos", {"photos"}, strategy::ZoneAction::FastFill),
                                     test::zone("Archives", {"archives"}, strategy::ZoneAction::FastFill)});
        const TargetLayout layout =
            resolveLayout(v.snapshot, s, test::assign(v, s), v.classes, classify::builtinCatalog(), {});
        const FileIndex a = v.files.at('A');
        const Plan in = PlanMaker().move(a, {{2, 1}, {4, 1}}, {{0, 2}}, MoveReason::Relocate, 0).done(v.snapshot, 2);
        EXPECT_TRUE(validatePlan(v.snapshot, in, {.layout = &layout}).ok());
        const Plan out =
            PlanMaker().move(a, {{2, 1}, {4, 1}}, {{0, 1}, {3, 1}}, MoveReason::Relocate, 0).done(v.snapshot, 2);
        EXPECT_TRUE(mentions(validatePlan(v.snapshot, out, {.layout = &layout}), "outside its zone"));
        EXPECT_TRUE(validatePlan(v.snapshot, out).ok()); // Without a layout, zones are not checked.
    }

    TEST(Validator, ParkingReturnsOnceAndIsReportedAsExtra) {
        Fixture f;
        const Plan parked = PlanMaker()
                                .move(f.b, {{6, 2}}, {{3, 2}}, MoveReason::Park)
                                .unpark(f.b, {{3, 2}}, {{4, 2}}) // Overlaps its own scratch run: not free yet.
                                .done(f.v.snapshot, 2, 2);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, parked), "[4, 6) is not free"));
        const Plan good = PlanMaker()
                              .move(f.b, {{6, 2}}, {{3, 2}}, MoveReason::Park)
                              .unpark(f.b, {{3, 2}}, {{6, 2}})
                              .done(f.v.snapshot, 2, 2);
        EXPECT_TRUE(validatePlan(f.v.snapshot, good).ok());
        const Plan lost = PlanMaker().move(f.b, {{6, 2}}, {{3, 2}}, MoveReason::Park).done(f.v.snapshot, 0, 2);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, lost), "never return from parking"));
        const Plan stray = PlanMaker().unpark(f.b, {{6, 2}}, {{3, 2}}).done(f.v.snapshot, 2);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, stray), "without having been parked"));
        const Plan unreported = PlanMaker()
                                    .move(f.b, {{6, 2}}, {{3, 2}}, MoveReason::Park)
                                    .unpark(f.b, {{3, 2}}, {{6, 2}})
                                    .done(f.v.snapshot, 2, 0);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, unreported), "the plan reports 0 extra"));
    }

    TEST(Validator, CatchesMovesBeyondTheLowerBound) {
        Fixture f;
        const Plan over = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{3, 2}}).done(f.v.snapshot, 1);
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, over), "the plan moves 2 clusters; lower bound 1"));
        Plan accounted = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{3, 2}}).done(f.v.snapshot, 6);
        accounted.skipped.push_back({.file = f.b, .reason = SkipReason::NoSpace, .clusters = 2});
        accounted.skippedClusters = 2;
        accounted.deferredClusters = 2;
        EXPECT_TRUE(validatePlan(f.v.snapshot, accounted).ok());
        accounted.skippedClusters = 3;
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, accounted), "skipped files hold 2 clusters"));
    }

    TEST(Validator, CatchesTotalsThatDisagree) {
        Fixture f;
        Plan plan = PlanMaker().move(f.a, {{0, 1}, {2, 1}}, {{3, 2}}).done(f.v.snapshot, 2);
        plan.totals.fragmentsAfter = 2;
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, plan), "totals"));
        plan.moves[0].file = 99;
        EXPECT_TRUE(mentions(validatePlan(f.v.snapshot, plan), "names no file"));
    }

}} // namespace icd::core::planner
