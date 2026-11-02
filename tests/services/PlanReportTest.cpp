// Plan reports (T096): icd-cli plan prints the report the GUI's plan job produces for the same snapshot, with every
// level's figures, and the moves on request.

#include "services/plan/PlanReport.h"

#include "core/classify/Catalog.h"
#include "core/classify/RuleEngine.h"
#include "core/format/JsonReader.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    // A request for a synthetic volume under a built-in preset.
    PlanRequest request(std::string_view preset) {
        core::SynthOptions options;
        options.seed = 81;
        options.files = 3000;
        options.geometry.totalClusters = 1ULL << 20U;
        auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        r.script = core::strategy::builtinPresets()[*core::strategy::builtinPresetIndex(preset)];
        r.now = 133'000'000'000'000'000LL;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        core::InlineExecutor serial;
        r.classes = engine.classify(*snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = std::move(snapshot);
        return r;
    }

    TEST(PlanReport, CliReportEqualsTheGuiJobsOutcome) {
        const PlanRequest r = request("mixed");
        // The GUI runs the plan as a job on the pool...
        auto output = std::make_shared<PlanOutput>();
        JobFunction job = makePlanJob(r, output);
        struct Sink : core::ProgressSink {
            void report(const core::Progress&) override {}
        } sink;
        ThreadPool pool(4);
        JobContext context{.token = {}, .progress = sink, .executor = pool};
        job(context);
        const auto gui = output->take();
        ASSERT_TRUE(gui);
        // ...icd-cli plans the same request with its own executor.
        core::InlineExecutor serial;
        const PlanOutcome cli = planLevels(r, serial);
        const PlanReportOptions options{.level = std::nullopt, .moves = true, .timing = false};
        EXPECT_EQ(planJson(*gui, r, options), planJson(cli, r, options));
    }

    TEST(PlanReport, JsonCarriesEveryLevelAndTheMoves) {
        const PlanRequest r = request("video");
        core::InlineExecutor serial;
        const PlanOutcome outcome = planLevels(r, serial);
        const core::JsonValue all = core::parseJson(planJson(outcome, r, {}));
        EXPECT_EQ(all.at("strategy").at("id").asString(), "video");
        ASSERT_EQ(all.at("levels").asArray().size(), 3U);
        for (const core::JsonValue& level : all.at("levels").asArray()) {
            EXPECT_TRUE(level.at("valid").asBool());
            EXPECT_LE(level.at("ratioToLowerBound").asNumber(), 1.1 + 1e-9);
            EXPECT_EQ(level.find("moves")->isNumber(), true);
        }
        const core::JsonValue one =
            core::parseJson(planJson(outcome, r, {.level = core::strategy::Effort::Maximum, .moves = true}));
        ASSERT_EQ(one.at("levels").asArray().size(), 1U);
        const core::JsonValue& max = one.at("levels").asArray().front();
        EXPECT_EQ(max.at("level").asString(), "maximum");
        const auto& plan = outcome.levels[2].result.plan;
        EXPECT_EQ(max.at("bytesMoved").asUnsigned(), plan.totals.clustersMoved * 4096);
        EXPECT_EQ(max.at("moveList").asArray().size(), plan.moves.size());
        EXPECT_EQ(max.at("moves").asUnsigned(), plan.moves.size());
        const std::string text = planText(outcome, r, {});
        EXPECT_NE(text.find("maximum:"), std::string::npos);
        EXPECT_NE(text.find("Video storage"), std::string::npos);
    }

}} // namespace icd::services
