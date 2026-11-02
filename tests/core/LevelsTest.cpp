// Moderate and Maximum levels (T092): both plans validate against their target; Moderate reaches a subset of
// Maximum's goals (zone fit, fragmentation) for no more bytes; Maximum sorts exactly and parks instead of dropping.

#include "core/planner/Validator.h"
#include "core/planner/levels/FastLevel.h"
#include "core/planner/levels/MaximumLevel.h"
#include "core/planner/levels/ModerateLevel.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    using strategy::Anchor;
    using strategy::Effort;
    using strategy::ZoneAction;
    using strategy::ZoneOrder;

    // Photos sorted by folder and name at the front, the rest fast-filled, video packed at the back (age at Maximum).
    strategy::ZoneScript mixedScript() {
        strategy::Zone photos = test::zone("Photos", {"photos"}, ZoneAction::Sort, Anchor::Front);
        photos.order = ZoneOrder::DirectoryName;
        photos.gapAfter.freePercent = 2;
        strategy::Zone rest = test::zone("Rest",
                                         {"unknown", "archives", "disc-images", "vm-disks", "backups", "source",
                                          "dependencies", "repository", "catalogs", "temp", "build"},
                                         ZoneAction::FastFill);
        strategy::Zone video = test::zone("Video", {"video"}, ZoneAction::PackToBack, Anchor::Back);
        video.orderByLevel[static_cast<std::size_t>(Effort::Maximum)] = ZoneOrder::Age;
        return test::script({photos, rest, video});
    }

    // A synthetic volume with photos, video and fragmented files.
    Snapshot volume(std::uint64_t seed, double fill) {
        SynthOptions options;
        options.seed = seed;
        options.files = 2500;
        options.geometry.totalClusters = 1ULL << 21U;
        options.fillPercent = fill;
        options.fragmentedShare = 0.3;
        return generateSyntheticVolume(options);
    }

    // The three levels of one volume.
    struct Levels {
        PlanResult fast;
        PlanResult moderate;
        PlanResult maximum;
    };

    // Plans every level.
    Levels planAll(const Snapshot& snapshot, const strategy::ZoneScript& s,
                   std::span<const classify::ClassId> classes) {
        const PlanInput input{.snapshot = &snapshot,
                              .script = &s,
                              .catalog = &classify::builtinCatalog(),
                              .classes = classes,
                              .churn = {},
                              .now = test::now};
        InlineExecutor serial;
        const ZoneAssignment a = assignInput(input, serial);
        return {planFast(input, a, serial), planModerate(input, a, serial), planMaximum(input, a, serial)};
    }

    TEST(Levels, ModerateIsASubsetOfMaximum) {
        for (const auto& [seed, fill] : {std::pair{61ULL, 50.0}, std::pair{62ULL, 65.0}, std::pair{63ULL, 80.0}}) {
            SCOPED_TRACE(seed);
            const Snapshot snapshot = volume(seed, fill);
            const auto classes = test::classesOf(snapshot);
            const auto script = mixedScript();
            const Levels l = planAll(snapshot, script, classes);
            for (const PlanResult* r : {&l.fast, &l.moderate, &l.maximum}) {
                const Validation v = validatePlan(snapshot, r->plan, {.layout = &r->layout});
                ASSERT_TRUE(v.ok()) << strategy::effortKey(r->plan.effort) << ": " << v.issues.front().message;
            }
            EXPECT_LE(l.moderate.plan.totals.clustersMoved, l.maximum.plan.totals.clustersMoved);
            EXPECT_LE(l.moderate.plan.lowerBound, l.maximum.plan.lowerBound);
            EXPECT_LE(l.moderate.metrics.zoneFitAfter, l.maximum.metrics.zoneFitAfter + 0.5);
            EXPECT_GE(l.moderate.metrics.fragmentedAfter + 0.5, l.maximum.metrics.fragmentedAfter);
            EXPECT_GE(l.maximum.metrics.zoneFitAfter, l.maximum.metrics.zoneFitBefore);
            EXPECT_LE(l.maximum.metrics.fragmentedAfter, l.maximum.metrics.fragmentedBefore);
            const ClusterCount used = usedClusters(snapshot);
            EXPECT_LE(l.moderate.plan.totals.clustersMoved, used / 5 + 1); // 20% budget.
            EXPECT_EQ(l.maximum.plan.deferredClusters, 0U); // No budget at Maximum.
        }
    }

    TEST(Levels, MaximumParksWhereModerateDrops) {
        // Photos in reverse name order fill their zone exactly: sorting them needs a swap, a cycle.
        const test::PictureFacts facts{.classes = {{'A', "photos"}, {'B', "photos"}},
                                       .names = {{'A', "b.jpg"}, {'B', "a.jpg"}}};
        const auto v = test::pictureVolume("AABB....", facts);
        strategy::Zone photos = test::zone("Photos", {"photos"}, ZoneAction::Sort, Anchor::Front);
        photos.order = ZoneOrder::Name;
        const auto s = test::script({photos});
        const Levels l = planAll(v.snapshot, s, v.classes);
        const Validation max = validatePlan(v.snapshot, l.maximum.plan, {.layout = &l.maximum.layout});
        ASSERT_TRUE(max.ok()) << max.issues.front().message;
        EXPECT_GT(l.maximum.plan.extra, 0U); // A park or a given-up anchor.
        EXPECT_GE(l.maximum.plan.moves.size(), 2U);
        const Validation mod = validatePlan(v.snapshot, l.moderate.plan, {.layout = &l.moderate.layout});
        ASSERT_TRUE(mod.ok()) << mod.issues.front().message;
        EXPECT_TRUE(l.moderate.plan.moves.empty()); // Half the zone out of order: past Moderate's tolerance.
    }

    TEST(Levels, PoliciesFollowTheTable) {
        const auto v = test::pictureVolume("AAAA....");
        const auto s = test::script({test::zone("All", {}, ZoneAction::FastFill)});
        const PlanInput input{.snapshot = &v.snapshot, .script = &s, .catalog = &classify::builtinCatalog()};
        const LevelPolicy fast = fastPolicy(input);
        const LevelPolicy moderate = moderatePolicy(input);
        const LevelPolicy maximum = maximumPolicy(input);
        EXPECT_EQ(fast.budget.maxClusters, 1U); // ceil(2% of 4).
        EXPECT_DOUBLE_EQ(*fast.budget.maxSeconds, 900.0);
        EXPECT_EQ(moderate.budget.maxClusters, 1U); // ceil(20% of 4).
        EXPECT_FALSE(maximum.budget.maxClusters);
        EXPECT_FALSE(moderate.allowParking);
        EXPECT_TRUE(maximum.allowParking);
        EXPECT_TRUE(maximum.goodEnough.wholeFiles);
        EXPECT_FALSE(moderate.goodEnough.wholeFiles);
        PlanInput x4 = input;
        x4.fastBudgetMultiplier = 4;
        EXPECT_DOUBLE_EQ(*fastPolicy(x4).budget.maxSeconds, 3600.0);
    }

}} // namespace icd::core::planner
