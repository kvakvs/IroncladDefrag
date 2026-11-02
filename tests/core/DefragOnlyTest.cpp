// Placement helpers, the good-enough filter and defragment-only planning (T084): destinations avoid the file's own
// runs, plans validate, and the untouched files are exactly the good-enough set.

#include "core/planner/CandidateFilter.h"
#include "core/planner/DefragOnly.h"
#include "core/planner/Placement.h"
#include "core/planner/Validator.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <set>

namespace icd::core::planner { namespace {

    using strategy::ZoneAction;

    TEST(Placement, FitsAvoidTheFilesOwnRuns) {
        const HoleSet pool(std::vector<ClusterRun>{{0, 10}, {20, 3}, {30, 6}});
        const std::vector<ClusterRun> own{{2, 2}, {32, 1}};
        const SpaceRequest three{.clusters = 3, .own = own, .window = {0, 40}};
        EXPECT_EQ(findFirst(pool, three), (ClusterRun{4, 3})); // [0,2) is too short, [2,4) is the file itself.
        EXPECT_EQ(findLast(pool, three), (ClusterRun{33, 3}));
        EXPECT_EQ(findNear(pool, three, 21), (ClusterRun{20, 3})); // The hole around 21 holds it.
        EXPECT_EQ(findNear(pool, three, 29), (ClusterRun{33, 3})); // 4 after beats 6 before ([7,10) ends at 10).
        EXPECT_EQ(findNear(pool, three, 15), (ClusterRun{20, 3})); // 5 after, 5 before: forward wins the tie.
        const SpaceRequest seven{.clusters = 7, .own = own, .window = {0, 40}};
        EXPECT_FALSE(findFirst(pool, seven));
        const auto pieces = findPieces(pool, seven, 3);
        ASSERT_TRUE(pieces);
        EXPECT_EQ(*pieces, (std::vector<ClusterRun>{{4, 4}, {20, 3}})); // Leaves no piece under 3.
        EXPECT_FALSE(findPieces(pool, {.clusters = 30, .own = own, .window = {0, 40}}, 3));
        EXPECT_EQ(findFirst(pool, {.clusters = 3, .own = own, .window = {5, 3}}), (ClusterRun{5, 3}));
        EXPECT_FALSE(findFirst(pool, {.clusters = 3, .own = own, .window = {1, 4}})); // Only [1,2) and [4,5) there.
    }

    TEST(DefragOnly, JoinsFragmentsNearTheFile) {
        //  0 1 2 3 4 5 6 7 8 9
        //  U . U . . . X . X X
        const auto v = test::pictureVolume("U.U...X.XX");
        HoleSet pool(v.snapshot.freeSpace.runs());
        Plan plan;
        const std::vector<FileIndex> files{v.files.at('U'), v.files.at('X')};
        planDefragment(v.snapshot, files, pool, {}, plan);
        ASSERT_EQ(plan.moves.size(), 2U);
        // X (3 clusters) goes first: [3,6) is the closest run of three to its start at 6.
        EXPECT_EQ(plan.moves[0].file, v.files.at('X'));
        EXPECT_EQ(plan.destinations(plan.moves[0])[0], (ClusterRun{3, 3}));
        // U's own clusters 0 and 2 split [0,3), so its closest two free clusters start at 6, vacated by X.
        EXPECT_EQ(plan.destinations(plan.moves[1])[0], (ClusterRun{6, 2}));
        plan.lowerBound = 5;
        finishTotals(plan, v.snapshot);
        EXPECT_TRUE(validatePlan(v.snapshot, plan).ok());
    }

    TEST(DefragOnly, SkipsFilesThatFitNowhere) {
        const auto v = test::pictureVolume("U.U.U.");
        HoleSet pool(v.snapshot.freeSpace.runs());
        Plan plan;
        const std::vector<FileIndex> files{v.files.at('U')};
        planDefragment(v.snapshot, files, pool, {}, plan);
        EXPECT_TRUE(plan.moves.empty());
        ASSERT_EQ(plan.skipped.size(), 1U);
        EXPECT_EQ(plan.skipped[0].reason, SkipReason::NoSpace);
        EXPECT_EQ(plan.skippedClusters, 3U);
    }

    // A small synthetic volume with plenty of fragmented files.
    Snapshot syntheticVolume(std::uint64_t seed) {
        SynthOptions options;
        options.seed = seed;
        options.files = 1500;
        options.geometry.totalClusters = 1ULL << 18U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.4;
        return generateSyntheticVolume(options);
    }

    TEST(DefragOnly, UntouchedFilesAreExactlyTheGoodEnoughSet) {
        for (const bool whole : {false, true}) {
            const Snapshot snapshot = syntheticVolume(whole ? 3 : 4);
            const auto classes = test::classesOf(snapshot);
            const auto script = test::script({test::zone("Everything", {}, ZoneAction::DefragmentOnly)});
            const ZoneAssignment a = test::assignSnapshot(snapshot, classes, script);
            InlineExecutor serial;
            const TargetLayout layout = resolveLayout(snapshot, script, a, classes, classify::builtinCatalog(), {});
            const GoodEnough rules{.wholeFiles = whole};
            const LowerBound bound = lowerBound(snapshot, script, a, layout, rules, serial);
            const Candidates candidates = filterCandidates(bound, a, script.zones.size());
            ASSERT_GT(candidates.defragment.size(), 100U);
            EXPECT_EQ(candidates.clusters, bound.clusters);
            HoleSet pool(snapshot.freeSpace.runs());
            Plan plan;
            plan.lowerBound = bound.clusters;
            planDefragment(snapshot, candidates.defragment, pool, {.fragmentOk = fragmentOkFor(script, 0, rules)},
                           plan);
            finishTotals(plan, snapshot);
            const Validation v = validatePlan(snapshot, plan);
            ASSERT_TRUE(v.ok()) << v.issues.front().message;
            EXPECT_TRUE(plan.skipped.empty());
            std::set<FileIndex> moved;
            for (const Move& m : plan.moves) {
                moved.insert(m.file);
            }
            for (std::size_t f = 0; f < snapshot.files.size(); ++f) {
                EXPECT_EQ(moved.contains(static_cast<FileIndex>(f)), bound.mustMove[f] != 0) << f;
            }
            EXPECT_LT(v.totals.fragmentsAfter, v.totals.fragmentsBefore);
            EXPECT_EQ(v.totals.fragmentsAfter, plan.moves.size()); // Every moved file is one fragment now.
        }
    }

}} // namespace icd::core::planner
