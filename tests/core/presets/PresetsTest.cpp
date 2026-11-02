// Strategy presets (T094): every built-in preset parses and validates, and property tests plan every preset at every
// level on random synthetic volumes: each plan replays cleanly (plan.md 9.5), accounts for its lower bound, never
// makes fragmentation worse, and Moderate never moves more than Maximum.

#include "core/planner/Validator.h"
#include "core/planner/levels/FastLevel.h"
#include "core/planner/levels/MaximumLevel.h"
#include "core/planner/levels/ModerateLevel.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

#include <set>

namespace icd::core::planner { namespace {

    using strategy::builtinPresets;

    TEST(Presets, EveryPresetParsesWithAUniqueId) {
        const auto& presets = builtinPresets();
        ASSERT_EQ(presets.size(), 14U);
        EXPECT_EQ(presets.front().id, "mixed"); // The default comes first.
        std::set<std::string> ids;
        for (const auto& p : presets) {
            EXPECT_TRUE(ids.insert(p.id).second) << p.id;
            EXPECT_FALSE(p.description.empty()) << p.id;
            EXPECT_TRUE(validateScript(p, classify::builtinCatalog()).empty()) << p.id;
            // Writing and reading back gives the same script.
            EXPECT_EQ(strategy::parseZoneScript(strategy::zoneScriptJson(p), classify::builtinCatalog()), p) << p.id;
        }
        for (const char* id : {"mixed", "development", "archive", "photos", "video", "defrag-only"}) {
            EXPECT_TRUE(strategy::builtinPresetIndex(id)) << id; // Ids remembered in settings since T075.
        }
        EXPECT_FALSE(strategy::builtinPresetIndex("nope"));
    }

    TEST(Presets, StoragePresetsFollowThePlan) {
        const auto& video = builtinPresets()[*strategy::builtinPresetIndex("video")];
        const auto& back = video.zones.back();
        EXPECT_EQ(back.action, strategy::ZoneAction::PackToBack);
        EXPECT_EQ(back.orderAt(strategy::Effort::Moderate), strategy::ZoneOrder::None);
        EXPECT_EQ(back.orderAt(strategy::Effort::Maximum), strategy::ZoneOrder::Age);
        const auto& dev = builtinPresets()[*strategy::builtinPresetIndex("development")];
        const auto churnGap =
            std::ranges::find_if(dev.zones, [](const auto& z) { return z.gapAfter.classMultiple > 0; });
        ASSERT_NE(churnGap, dev.zones.end());
        EXPECT_EQ(churnGap->gapAfter.volumePercent, 5.0); // max(5% of volume, 2 x build output + temp).
        EXPECT_EQ(churnGap->gapAfter.classMultiple, 2.0);
    }

    // The fill level of the random volume of a seed.
    double fill(std::uint64_t seed) { return 40.0 + static_cast<double>(seed % 6) * 9.0; }

    // A random synthetic volume.
    Snapshot randomVolume(std::uint64_t seed) {
        SynthOptions options;
        options.seed = seed;
        options.files = 1500 + static_cast<std::uint32_t>(seed % 5) * 400;
        options.geometry = {.bytesPerCluster = seed % 3 == 0 ? 16384U : 4096U, .totalClusters = 1ULL << 19U};
        options.fillPercent = fill(seed);
        options.fragmentedShare = 0.1 + static_cast<double>(seed % 4) * 0.1;
        options.freePattern = static_cast<FreePattern>(seed % 3);
        return generateSyntheticVolume(options);
    }

    // Checks one plan.
    void checkPlan(const Snapshot& snapshot, const PlanResult& r, const std::string& what) {
        const Validation v = validatePlan(snapshot, r.plan, {.layout = &r.layout});
        ASSERT_TRUE(v.ok()) << what << ": " << v.issues.front().message;
        EXPECT_LE(r.metrics.fragmentedAfter, r.metrics.fragmentedBefore + 1e-9) << what;
        EXPECT_LE(r.plan.totals.clustersMoved, r.plan.lowerBound + r.plan.extra) << what;
    }

    TEST(Presets, EveryPresetPlansValidlyAtEveryLevel) {
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            const Snapshot snapshot = randomVolume(seed);
            const auto classes = test::classesOf(snapshot);
            for (const auto& preset : builtinPresets()) {
                const std::string what = std::format("seed {} preset {}", seed, preset.id);
                const PlanInput input{.snapshot = &snapshot,
                                      .script = &preset,
                                      .catalog = &classify::builtinCatalog(),
                                      .classes = classes,
                                      .churn = {},
                                      .now = test::now,
                                      .reserved = {{1000, 2000}}}; // An MFT zone to keep free.
                InlineExecutor serial;
                const ZoneAssignment a = assignInput(input, serial);
                const PlanResult fast = planFast(input, a, serial);
                const PlanResult moderate = planModerate(input, a, serial);
                const PlanResult maximum = planMaximum(input, a, serial);
                checkPlan(snapshot, fast, what + " fast");
                checkPlan(snapshot, moderate, what + " moderate");
                checkPlan(snapshot, maximum, what + " maximum");
                // Below about two thirds full Maximum reaches everything Moderate does; fuller volumes are the low free
                // space case of plan.md 9.3.9, where Maximum may leave cycles it cannot park unreached.
                if (fill(seed) <= 65.0) {
                    EXPECT_LE(moderate.plan.totals.clustersMoved, maximum.plan.totals.clustersMoved) << what;
                }
            }
        }
    }

}} // namespace icd::core::planner
