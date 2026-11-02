// The icd-cli execute report (T104): built from a run on a volume in memory, it parses as JSON and carries the plan,
// the revalidation with every drop, the totals with every move that did not complete, the snapshot note, the zones
// and, when asked, every record; the text form names the same totals.

#include "services/execute/ExecuteReport.h"

#include "SimDevice.h"
#include "core/classify/RuleEngine.h"
#include "core/format/JsonReader.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    TEST(ExecuteReport, JsonCarriesEverySection) {
        core::SynthOptions options;
        options.seed = 31;
        options.files = 800;
        options.geometry.totalClusters = 1ULL << 18U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.3;
        auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest planRequest;
        planRequest.script = *core::strategy::builtinPresets().begin();
        planRequest.now = 20'000 * day;
        core::InlineExecutor serial;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        planRequest.classes = engine.classify(*snapshot, {.now = planRequest.now, .churn = {}}, serial);
        planRequest.snapshot = snapshot;
        const PlanOutcome outcome = planLevels(planRequest, serial);
        core::DriveInfo drive;
        drive.root = "T:\\";
        drive.layout.volumeName = "\\\\?\\Volume{t}\\";
        drive.layout.ntfs = core::NtfsLayout{};
        const auto level = core::strategy::Effort::Moderate;
        ExecutionRequest request = freshExecution(drive, executionPlanFor(outcome, planRequest, drive, level));
        ASSERT_GT(request.plan.moves.size(), 2U);

        // One file is locked, so one move does not complete.
        testing::SimVolume volume(*snapshot);
        testing::SimDevice device(volume);
        device.locked.insert(request.plan.moves[1].fileReference);
        ExecutionOutcome result;
        result.revalidation.replay = core::execute::replayPlan(request.plan, request.order, volume.freeRuns(), volume);
        result.report = executePlan(request.plan, result.revalidation.replay.kept, device, {}, {});
        result.snapshotNote = "kept";
        result.log = "x.jsonl";
        const ExecuteReportInput input{&request, &result, nullptr, zoneSpans(outcome, planRequest, level), true};

        const core::JsonValue json = core::parseJson(executeJson(input));
        EXPECT_EQ(json.at("volume").asString(), "T:\\");
        EXPECT_TRUE(json.at("refused").isNull());
        EXPECT_EQ(json.at("plan").at("level").asString(), "moderate");
        EXPECT_EQ(json.at("plan").at("moves").asUnsigned(), request.plan.moves.size());
        EXPECT_EQ(json.at("revalidation").at("kept").asUnsigned(), request.plan.moves.size());
        const auto& execution = json.at("execution");
        EXPECT_EQ(execution.at("skipped").asUnsigned(), result.report.totals.skipped);
        const auto& problems = execution.at("problems").asArray();
        ASSERT_FALSE(problems.empty());
        EXPECT_EQ(problems.front().at("reason").asString(), "locked");
        EXPECT_EQ(json.at("records").asArray().size(), result.report.records.size());
        EXPECT_FALSE(json.at("zones").asArray().empty());
        EXPECT_EQ(json.at("snapshot").at("note").asString(), "kept");
        EXPECT_EQ(json.find("rescan"), nullptr);

        const std::string text = executeText(input);
        EXPECT_NE(text.find(std::format("{} moved", result.report.totals.moved)), std::string::npos);
        EXPECT_NE(text.find("moderate"), std::string::npos);
    }

}} // namespace icd::services
