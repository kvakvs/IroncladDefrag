// Fast level (T091): hot spots from churn, small holes and fragmentation; Fast plans move only files inside hot
// spots, send big long-lived files to their zone, validate, and stay within the 2% / 15 minute budget.

#include "core/planner/Validator.h"
#include "core/planner/levels/FastLevel.h"
#include "core/snapshot/Stripes.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <map>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::ZoneAction;

    // A mixed strategy small enough for tests.
    strategy::ZoneScript mixedScript() {
        return test::script({test::zone("Photos", {"photos"}, ZoneAction::FastFill, Anchor::Front),
                             test::zone("Temp", {"temp", "build"}, ZoneAction::KeepInPlace),
                             test::zone("Rest",
                                        {"unknown", "archives", "disc-images", "vm-disks", "backups", "source",
                                         "dependencies", "repository", "catalogs"},
                                        ZoneAction::FastFill),
                             test::zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back)});
    }

    // The plan input of a snapshot.
    PlanInput inputOf(const Snapshot& snapshot, const strategy::ZoneScript& s,
                      std::span<const classify::ClassId> classes, std::span<const std::uint8_t> churn = {}) {
        return {.snapshot = &snapshot,
                .script = &s,
                .catalog = &classify::builtinCatalog(),
                .classes = classes,
                .churn = churn,
                .now = test::now};
    }

    // Whether runs lie inside one of the ranges.
    bool within(std::span<const ClusterRun> runs, const std::vector<ClusterRun>& ranges) {
        return std::ranges::all_of(runs, [&](const ClusterRun& r) {
            return std::ranges::any_of(ranges,
                                       [&](const ClusterRun& h) { return r.start >= h.start && r.end() <= h.end(); });
        });
    }

    TEST(FastLevel, HotSpotsFollowChurnHolesAndFragments) {
        // Four stripes of 4096 clusters: churn in the second, small holes in the fourth.
        SnapshotBuilder b({.bytesPerCluster = 4096, .totalClusters = 4 * 4096});
        const FileIndex root = b.addFile(
            {.name = "", .parent = noFile, .sizeBytes = 0, .flags = FileDirectory, .times = {}, .fileReference = 0});
        const auto add = [&](ClusterRun run) {
            const FileIndex f = b.addFile({.name = "f",
                                           .parent = root,
                                           .sizeBytes = run.length * 4096,
                                           .flags = 0,
                                           .times = {},
                                           .fileReference = 0});
            b.addExtent({.run = run, .vcn = 0, .file = f, .stream = 0, .flags = 0});
            return f;
        };
        (void)add({0, 4096});
        const FileIndex churning = add({4096, 4096});
        (void)add({8192, 4096});
        Lcn at = 3 * 4096;
        while (at < 4 * 4096) { // 64 one-cluster holes between files.
            (void)add({at, 63});
            b.addFreeRun({at + 63, 1});
            at += 64;
        }
        const Snapshot s = std::move(b).build();
        std::vector<std::uint8_t> churn(s.files.size(), 0);
        churn[churning] = 1;
        const auto script = test::script({test::zone("All", {}, ZoneAction::FastFill)});
        const auto classes = test::classesOf(s);
        const ZoneAssignment a = test::assignSnapshot(s, classes, script);
        InlineExecutor serial;
        const auto scores = scoreStripes(s, a, churn, {}, serial);
        ASSERT_EQ(scores.size(), 4U);
        EXPECT_DOUBLE_EQ(scores[1].churn, 1.0);
        EXPECT_DOUBLE_EQ(scores[3].holes, 1.0);
        EXPECT_DOUBLE_EQ(scores[0].score, 0.0);
        const auto spots = findHotSpots(s, a, churn, {}, serial);
        EXPECT_EQ(spots, (std::vector<ClusterRun>{{4096, 3 * 4096}})); // The cold stripe between them is bridged.
    }

    TEST(FastLevel, BigLongLivedFilesGoStraightToTheirZone) {
        //  0 1 2 3 4 5 6 7 8     one cluster = 64 MB; the whole volume is one hot stripe.
        //  A A . B . . C . .     B (video, old) splits the free space: it goes to the video zone at the end;
        //                        C fills the hole at 2.
        const auto v = test::pictureVolume("AA.B..C..", {.classes = {{'B', "video"}}, .clusterBytes = 64U << 20U});
        const auto script = mixedScript();
        PlanInput input = inputOf(v.snapshot, script, v.classes);
        input.fastBudgetMultiplier = 100;
        InlineExecutor serial;
        const ZoneAssignment a = assignInput(input, serial);
        const PlanResult r = planFast(input, a, serial, {}, {.threshold = 0.0});
        const Validation check = validatePlan(v.snapshot, r.plan, {.layout = &r.layout});
        ASSERT_TRUE(check.ok()) << check.issues.front().message;
        ASSERT_EQ(r.plan.moves.size(), 2U);
        std::map<FileIndex, ClusterRun> to;
        for (const Move& m : r.plan.moves) {
            to[m.file] = r.plan.destinations(m).front();
        }
        EXPECT_EQ(to[v.files.at('B')], (ClusterRun{8, 1}));
        EXPECT_EQ(to[v.files.at('C')], (ClusterRun{2, 1}));
        EXPECT_FALSE(r.plan.notes.empty()); // "Run it again".
    }

    TEST(FastLevel, SyntheticPlansTouchOnlyHotSpotsAndStayInBudget) {
        for (const std::uint64_t seed : {51ULL, 52ULL, 53ULL}) {
            SynthOptions options;
            options.seed = seed;
            options.files = 3000;
            options.geometry.totalClusters = 1ULL << 21U;
            options.fillPercent = 70;
            options.fragmentedShare = 0.3;
            options.freePattern = FreePattern::Scattered;
            const Snapshot snapshot = generateSyntheticVolume(options);
            const auto classes = test::classesOf(snapshot);
            const auto script = mixedScript();
            const PlanInput input = inputOf(snapshot, script, classes);
            InlineExecutor serial;
            const ZoneAssignment a = assignInput(input, serial);
            const PlanResult r = planFast(input, a, serial);
            const Validation check = validatePlan(snapshot, r.plan, {.layout = &r.layout});
            ASSERT_TRUE(check.ok()) << check.issues.front().message;
            ASSERT_FALSE(r.hotSpots.empty());
            ASSERT_FALSE(r.plan.moves.empty());
            const LevelPolicy policy = fastPolicy(input);
            EXPECT_LE(r.plan.totals.clustersMoved, *policy.budget.maxClusters);
            EXPECT_LE(r.metrics.seconds, *policy.budget.maxSeconds);
            for (const Move& m : r.plan.moves) {
                EXPECT_TRUE(within(r.plan.sources(m), r.hotSpots)) << seed;
                const ZoneTarget* t = r.layout.target(m.zone);
                const bool toZone = t != nullptr &&
                    std::ranges::all_of(r.plan.destinations(m), [&](const ClusterRun& d) { return t->holds(d); });
                EXPECT_TRUE(toZone || within(r.plan.destinations(m), r.hotSpots)) << seed;
            }
            EXPECT_LE(r.metrics.fragmentedAfter, r.metrics.fragmentedBefore);
        }
    }

    TEST(FastLevel, HotVolumesArePlannedInPiecesUpToTheBudget) {
        // Every stripe is hot: Fast plans pieces of at most eight stripes, hottest first, only until their movers reach
        // twice the budget, instead of the whole volume.
        SynthOptions options;
        options.seed = 61;
        options.files = 4000;
        options.geometry.totalClusters = 1ULL << 21U;
        options.fillPercent = 70;
        options.fragmentedShare = 0.3;
        const Snapshot snapshot = generateSyntheticVolume(options);
        const auto classes = test::classesOf(snapshot);
        const auto script = mixedScript();
        const PlanInput input = inputOf(snapshot, script, classes);
        InlineExecutor serial;
        const ZoneAssignment a = assignInput(input, serial);
        const PlanResult r = planFast(input, a, serial, {}, {.threshold = 0.0});
        const Validation check = validatePlan(snapshot, r.plan, {.layout = &r.layout});
        ASSERT_TRUE(check.ok()) << check.issues.front().message;
        const StripeLayout stripes(snapshot.geometry.totalClusters);
        ClusterCount planned = 0;
        for (const ClusterRun& spot : r.hotSpots) {
            EXPECT_LE(spot.length, 8 * stripes.stripeClusters());
            planned += spot.length;
        }
        EXPECT_FALSE(r.hotSpots.empty());
        EXPECT_LT(planned, snapshot.geometry.totalClusters / 2);
        EXPECT_LE(r.plan.totals.clustersMoved, *fastPolicy(input).budget.maxClusters);
    }

}} // namespace icd::core::planner
