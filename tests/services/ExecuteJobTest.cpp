// The GUI's view of an execution (T110): the feed follows the mover (counts, bytes, the current file, moves for the
// live map) and lists what did not go as planned in words; the summary gives the Log tab and status line texts.

#include "services/execute/ExecuteJob.h"

#include "SimDevice.h"
#include "core/classify/RuleEngine.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace icd::services { namespace {

    using core::execute::DropReason;
    using core::execute::ExecutionPlan;
    using testing::SimDevice;
    using testing::SimVolume;

    // A planned synthetic volume and the volume in memory to run it on.
    struct Bed {
        std::shared_ptr<core::Snapshot> snapshot; // Planned volume.
        std::shared_ptr<ExecutionPlan> plan; // Its Moderate plan.
        std::unique_ptr<SimVolume> volume; // Where it runs.
    };

    // Plans a seeded volume with the default preset at Moderate.
    Bed bed(std::uint64_t seed) {
        core::SynthOptions options;
        options.seed = seed;
        options.files = 800;
        options.geometry.totalClusters = 1ULL << 18U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.3;
        Bed b;
        b.snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        r.script = core::strategy::builtinPresets().front();
        r.now = 20'000LL * 864'000'000'000LL;
        core::InlineExecutor serial;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        r.classes = engine.classify(*b.snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = b.snapshot;
        const PlanOutcome outcome = planLevels(r, serial);
        core::execute::PlanIdentity id;
        id.geometry = b.snapshot->geometry;
        b.plan = std::make_shared<ExecutionPlan>(core::execute::executionPlanOf(
            outcome.levels.at(static_cast<std::size_t>(core::strategy::Effort::Moderate)).result, *b.snapshot, id));
        b.volume = std::make_unique<SimVolume>(*b.snapshot);
        return b;
    }

    // A revalidation that keeps every move of order except `dropped`, which is dropped as gone.
    Revalidation keepAllBut(const ExecutionPlan& plan, std::uint32_t dropped) {
        Revalidation v;
        for (const std::uint32_t move : core::execute::allMoves(plan)) {
            if (move == dropped) {
                v.replay.dropped.push_back({move, DropReason::FileGone});
            }
            else {
                v.replay.kept.push_back(move);
            }
        }
        v.replay.pendingClusters = core::execute::plannedClusters(plan);
        return v;
    }

    // Whether a line about a file with a result is among lines.
    bool hasLine(const std::vector<ExecutionLogLine>& lines, const std::string& file, const std::string& result) {
        return std::ranges::any_of(lines,
                                   [&](const ExecutionLogLine& l) { return l.file == file && l.result == result; });
    }

    TEST(ExecutionFeed, FollowsTheMoverAndListsWhatDidNotGoAsPlanned) {
        Bed b = bed(21);
        ASSERT_GT(b.plan->moves.size(), 10U);
        SimDevice device(*b.volume);
        device.locked.insert(b.plan->moves[2].fileReference);
        ExecutionFeed feed;
        feed.setPlan(b.plan);
        const Revalidation revalidation = keepAllBut(*b.plan, 5);
        feed.revalidated(revalidation);
        EXPECT_TRUE(feed.counters().moving);
        EXPECT_EQ(feed.counters().moves, b.plan->moves.size() - 1);
        EXPECT_EQ(feed.counters().dropped, 1U);

        const auto report = executePlan(*b.plan, revalidation.replay.kept, device, {},
                                        {.token = {}, .pause = nullptr, .progress = nullptr, .hooks = feed.hooks()});
        const ExecutionCounters c = feed.counters();
        const std::uint32_t bpc = b.snapshot->geometry.bytesPerCluster;
        EXPECT_EQ(c.finished, report.records.size());
        EXPECT_EQ(c.moved, report.totals.moved + report.totals.alreadyDone);
        EXPECT_EQ(c.skipped, report.totals.skipped);
        EXPECT_EQ(c.bytesMoved, report.totals.clustersMoved * bpc);
        EXPECT_EQ(c.bytesPending, core::execute::plannedClusters(*b.plan) * bpc);
        EXPECT_EQ(c.currentFile, b.plan->paths[revalidation.replay.kept.back()]);

        const auto lines = feed.takeLines();
        EXPECT_TRUE(hasLine(lines, b.plan->paths[5], "dropped"));
        EXPECT_TRUE(hasLine(lines, b.plan->paths[2], "skipped"));
        const auto locked = std::ranges::find(lines, b.plan->paths[2], &ExecutionLogLine::file);
        EXPECT_NE(locked->details.find("locked"), std::string::npos) << locked->details;
        EXPECT_TRUE(feed.takeLines().empty()); // Taken once.

        const auto moved = feed.takeMoved();
        const auto withClusters =
            std::ranges::count_if(report.records, [](const MoveRecord& r) { return r.clustersMoved != 0; });
        EXPECT_EQ(static_cast<std::ptrdiff_t>(moved.size()), withClusters);
        EXPECT_TRUE(std::ranges::all_of(moved, [](const MoveRecord& r) { return !r.runsAfter.empty(); }));

        EXPECT_FALSE(feed.summary());
        ExecutionOutcome outcome;
        outcome.revalidation = revalidation;
        outcome.report = report;
        outcome.log = "run.jsonl";
        outcome.snapshotNote = "the saved snapshot now includes the moves";
        feed.finish(outcome);
        const auto summary = feed.summary();
        ASSERT_TRUE(summary);
        EXPECT_EQ(summary->dropped, 1U);
        EXPECT_EQ(summary->bytesPerCluster, bpc);
        const std::string status = summaryStatus(*summary, "T: ICD-TEST");
        EXPECT_NE(status.find("did not go as planned"), std::string::npos) << status;
        const auto end = summaryLines(*summary);
        EXPECT_TRUE(std::ranges::any_of(end, [](const ExecutionLogLine& l) {
            return l.result == "run" && l.details == "execution log: run.jsonl";
        }));
    }

    // Past maxLines the feed only counts, and the end points to the execution log for the rest.
    TEST(ExecutionFeed, CapsTheLogTab) {
        auto plan = std::make_shared<ExecutionPlan>();
        ExecutionFeed feed;
        feed.setPlan(plan);
        Revalidation v;
        for (std::uint32_t move = 0; move < ExecutionFeed::maxLines + 3; ++move) {
            v.replay.dropped.push_back({move, DropReason::FileChanged});
        }
        feed.revalidated(v);
        EXPECT_EQ(feed.takeLines().size(), ExecutionFeed::maxLines);
        feed.finish({});
        const auto rest = feed.takeLines();
        ASSERT_EQ(rest.size(), 1U);
        EXPECT_NE(rest.front().details.find("3 more files"), std::string::npos) << rest.front().details;
    }

    TEST(ExecutionText, RecordsAndRunsInWords) {
        EXPECT_EQ(reasonText("locked"), "the file is in use and locked");
        EXPECT_EQ(reasonText("something-new"), "something-new"); // Unknown keys stay readable.
        EXPECT_EQ(reasonText(""), "no reason given");

        ExecutionPlan plan;
        plan.paths = {"T:\\a.bin"};
        MoveRecord r;
        r.outcome = MoveOutcome::Moved;
        r.verified = true;
        EXPECT_FALSE(recordLine(r, plan)); // As planned: no line.
        r.replaced = true;
        EXPECT_EQ(recordLine(r, plan)->result, "moved elsewhere");
        r.verified = false;
        EXPECT_EQ(recordLine(r, plan)->result, "not verified");
        r.outcome = MoveOutcome::Partial;
        r.reason = "cancelled";
        r.clustersMoved = 1234;
        const auto partial = recordLine(r, plan);
        EXPECT_EQ(partial->file, "T:\\a.bin");
        EXPECT_EQ(partial->details, "1,234 clusters moved, then it stopped: cancelled; the file is intact");
        r.outcome = MoveOutcome::Skipped;
        r.reason = "failed";
        r.win32Error = 87;
        EXPECT_EQ(recordLine(r, plan)->details, "the move failed (Windows error 87)");
        EXPECT_EQ(dropLine({0, DropReason::DestinationInUse}, plan).details,
                  "not run: its destination is taken and no other free space in its zone holds the rest");

        ExecutionSummary s;
        s.totals.moved = 3;
        s.totals.clustersMoved = 256; // 1 MB at 4 KB clusters.
        s.seconds = 2.0;
        EXPECT_EQ(summaryStatus(s, "T:"), "Plan executed on T: in 2.0 s: 3 moves done (1.00 MB)");
        s.cancelled = true;
        EXPECT_NE(summaryStatus(s, "T:").find("cancelled after 2.0 s"), std::string::npos);
        EXPECT_TRUE(std::ranges::any_of(summaryLines(s),
                                        [](const ExecutionLogLine& l) { return l.details.starts_with("cancelled:"); }));
        s.refused = "the plan is older than 24 hours";
        EXPECT_EQ(summaryStatus(s, "T:"), "The plan for T: was not run: the plan is older than 24 hours");
        EXPECT_EQ(summaryLines(s).size(), 1U);
    }

}} // namespace icd::services
