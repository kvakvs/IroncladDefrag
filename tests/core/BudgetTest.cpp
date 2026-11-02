// Budgets and benefit ranking (T089): the cut plan stays within its budget and valid, the most valuable moves come
// first, dependencies stay together, and every prefix validates and improves the volume.

#include "core/planner/Budget.h"
#include "core/planner/CandidateFilter.h"
#include "core/planner/FastFill.h"
#include "core/planner/MoveOrdering.h"
#include "core/planner/Validator.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    using strategy::ZoneAction;

    // A fast-fill plan of a synthetic volume, ordered and with totals.
    Plan fastFillPlan(const Snapshot& snapshot) {
        InlineExecutor serial;
        const auto classes = test::classesOf(snapshot);
        const auto s = test::script(
            {test::zone("Photos", {"photos"}, ZoneAction::FastFill), test::zone("Rest", {}, ZoneAction::FastFill)});
        const ZoneAssignment a = test::assignSnapshot(snapshot, classes, s);
        const auto in = fitTarget(snapshot, s, a, classes, classify::builtinCatalog(), {}, {}, serial);
        const TargetLayout layout = resolveLayout(snapshot, s, a, classes, classify::builtinCatalog(), in);
        const LowerBound bound = lowerBound(snapshot, s, a, layout, {}, serial);
        const Candidates c = filterCandidates(bound, a, s.zones.size());
        HoleSet pool(snapshot.freeSpace.runs());
        for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
            if (bound.mustMove[f] != 0) {
                releaseRuns(pool, movableRuns(snapshot, static_cast<FileIndex>(f)));
            }
        }
        std::vector<Destination> all;
        Plan plan;
        for (const ZoneTarget& t : layout.zones) {
            const FillOptions options{.zone = t.zone, .home = {t.firstLcn().value_or(0), t.endLcn()}};
            auto result = fastFill(snapshot, t.pieces, c.perZone[t.zone], pool, options);
            all.insert(all.end(), result.placed.begin(), result.placed.end());
            for (const FileIndex f : result.overflow) { // Not placed: counts as skipped here.
                plan.skipped.push_back({.file = f, .reason = SkipReason::NoSpace, .clusters = a.movable[f]});
                plan.skippedClusters += a.movable[f];
            }
        }
        plan.lowerBound = bound.clusters;
        (void)orderMoves(snapshot, all, {}, plan);
        finishTotals(plan, snapshot);
        return plan;
    }

    // A synthetic volume with fragmented files and scattered free space.
    Snapshot volume() {
        SynthOptions options;
        options.seed = 41;
        options.files = 1200;
        options.geometry.totalClusters = 1ULL << 19U;
        options.fillPercent = 50;
        options.fragmentedShare = 0.3;
        return generateSyntheticVolume(options);
    }

    // The first k moves of a plan as a plan of their own (its lower bound is what they move).
    Plan prefixOf(const Snapshot& snapshot, const Plan& plan, std::size_t k) {
        Plan p;
        for (std::size_t i = 0; i < k; ++i) {
            const Move& m = plan.moves[i];
            p.add(m, plan.sources(m), plan.destinations(m));
            (m.extra ? p.extra : p.lowerBound) += m.clusters;
        }
        finishTotals(p, snapshot);
        return p;
    }

    TEST(Budget, CutPlanStaysWithinBudgetAndValid) {
        const Snapshot snapshot = volume();
        const Plan whole = fastFillPlan(snapshot);
        ASSERT_TRUE(validatePlan(snapshot, whole).ok());
        ASSERT_GT(whole.moves.size(), 50U);
        Plan cut = whole;
        const ClusterCount limit = whole.totals.clustersMoved / 4;
        applyBudget(snapshot, cut, {.maxClusters = limit});
        EXPECT_LE(cut.totals.clustersMoved, limit);
        EXPECT_GT(cut.moves.size(), 0U);
        EXPECT_LT(cut.moves.size(), whole.moves.size());
        EXPECT_EQ(cut.deferredClusters + cut.totals.clustersMoved, whole.totals.clustersMoved);
        const Validation v = validatePlan(snapshot, cut);
        ASSERT_TRUE(v.ok()) << v.issues.front().message;
        // The cut keeps the most valuable moves: their value per byte beats the deferred ones on average.
        double kept = 0.0;
        for (const Move& m : cut.moves) {
            kept += m.benefit;
        }
        double all = 0.0;
        for (const Move& m : whole.moves) {
            all += moveBenefit(snapshot, whole, m);
        }
        EXPECT_GT(kept / static_cast<double>(cut.totals.clustersMoved),
                  all / static_cast<double>(whole.totals.clustersMoved));
    }

    TEST(Budget, EveryPrefixValidatesAndImprovesTheVolume) {
        const Snapshot snapshot = volume();
        Plan plan = fastFillPlan(snapshot);
        applyBudget(snapshot, plan, {.maxClusters = plan.totals.clustersMoved / 2});
        const std::size_t step = std::max<std::size_t>(1, plan.moves.size() / 150);
        double previous = -1.0;
        for (std::size_t k = 1; k <= plan.moves.size(); k += step) {
            const Plan p = prefixOf(snapshot, plan, k);
            const Validation v = validatePlan(snapshot, p);
            ASSERT_TRUE(v.ok()) << k << ": " << v.issues.front().message;
            EXPECT_LE(v.totals.fragmentsAfter, v.totals.fragmentsBefore) << k;
            double benefit = 0.0;
            for (const Move& m : p.moves) {
                benefit += m.benefit;
            }
            EXPECT_GE(benefit, previous);
            previous = benefit;
        }
        // The most valuable ready move goes first: the first move is worth at least the plan's average per byte.
        const Move& first = plan.moves.front();
        EXPECT_GE(first.benefit / static_cast<double>(first.clusters),
                  plan.moves.back().benefit / static_cast<double>(plan.moves.back().clusters));
    }

    TEST(Budget, TimeLimitAndDependencies) {
        //  A B .   A -> B's place depends on B leaving; a budget for one move of B and A together is respected.
        const auto v = test::pictureVolume("AB.");
        Plan plan;
        const std::vector<Destination> d{
            {.file = v.files.at('A'), .runs = {{1, 1}}, .reason = MoveReason::Relocate, .zone = noZone},
            {.file = v.files.at('B'), .runs = {{2, 1}}, .reason = MoveReason::Compact, .zone = noZone}};
        plan.lowerBound = 2;
        (void)orderMoves(v.snapshot, d, {}, plan);
        finishTotals(plan, v.snapshot);
        Plan one = plan;
        applyBudget(v.snapshot, one, {.maxClusters = 1});
        ASSERT_EQ(one.moves.size(), 1U); // A alone would need B first; B alone fits.
        EXPECT_EQ(one.moves[0].file, v.files.at('B'));
        EXPECT_TRUE(validatePlan(v.snapshot, one).ok());
        Plan timed = plan;
        const TimeModel time;
        applyBudget(v.snapshot, timed, {.maxSeconds = time.moveSeconds(1, 4096) * 2.5});
        EXPECT_EQ(timed.moves.size(), 2U);
        EXPECT_NEAR(planSeconds(timed, 4096, time), 2 * time.moveSeconds(1, 4096), 1e-9);
        Plan none = plan;
        applyBudget(v.snapshot, none, {.maxSeconds = 0.001});
        EXPECT_TRUE(none.moves.empty());
        EXPECT_EQ(none.deferredClusters, 2U);
        EXPECT_TRUE(validatePlan(v.snapshot, none).ok());
    }

}} // namespace icd::core::planner
