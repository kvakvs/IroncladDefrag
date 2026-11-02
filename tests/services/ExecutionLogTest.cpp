// Execution log and resume (T103, plan.md 10.4): the log carries the whole plan and reads back equal; a line cut short
// by a crash is tolerated only at the end; a run that dies in the middle of a move is resumed from its log plus a fresh
// look at the volume and completes the plan; unfinished logs are found by volume.

#include "services/execute/Execution.h"
#include "services/execute/ExecutionLog.h"

#include "SimDevice.h"
#include "core/classify/RuleEngine.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/plan/PlanJob.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace icd::services { namespace {

    using core::ClusterRun;
    using core::execute::ExecutionPlan;
    using testing::SimDevice;
    using testing::SimVolume;

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // A synthetic volume and its Maximum plan (parking included).
    struct Bed {
        std::shared_ptr<core::Snapshot> snapshot; // Planned volume.
        ExecutionPlan plan; // The plan.
    };

    // Plans a seeded volume.
    Bed bed(std::uint64_t seed) {
        core::SynthOptions options;
        options.seed = seed;
        options.files = 1200;
        options.geometry.totalClusters = 1ULL << 19U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.3;
        Bed b;
        b.snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        r.script = core::strategy::builtinPresets().front();
        r.now = 20'000 * day;
        core::InlineExecutor serial;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        r.classes = engine.classify(*b.snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = b.snapshot;
        const PlanOutcome outcome = planLevels(r, serial);
        core::execute::PlanIdentity id;
        id.volumeName = "\\\\?\\Volume{0a1b2c3d-1111-2222-3333-444455556666}\\";
        id.volumeSerial = 0xFEDC'BA98'7654'3210ULL;
        id.geometry = b.snapshot->geometry;
        id.usn = core::UsnCursor{0x01D9'0000'1234'5678ULL, 987'654'321'000LL};
        id.madeAt = 133'000'000'000'000'000LL;
        id.strategy = "mixed";
        id.effort = core::strategy::Effort::Maximum;
        b.plan = core::execute::executionPlanOf(outcome.levels[2].result, *b.snapshot, id, {{100, 50}});
        return b;
    }

    // A fresh directory for one test's logs.
    std::filesystem::path scratch(const std::string& name) {
        const auto dir = std::filesystem::temp_directory_path() / ("icd-exec-log-" + name);
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        return dir;
    }

    // The text of a file.
    std::string textOf(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream s;
        s << in.rdbuf();
        return s.str();
    }

    TEST(ExecutionLog, PlanReadsBackEqual) {
        const Bed b = bed(21);
        const auto dir = scratch("roundtrip");
        const auto file = dir / "a.jsonl";
        {
            ExecutionLogWriter log(file, b.plan);
            log.event("run-started");
            log.dropped({4, core::execute::DropReason::FileChanged});
            log.begin(0);
            MoveRecord r;
            r.move = 0;
            r.outcome = MoveOutcome::Moved;
            r.verified = true;
            r.clustersMoved = b.plan.moves[0].clusters;
            r.runsAfter = {{0, 0, 1234, 5}, {1, 0, 99, 1}};
            log.outcome(r);
            log.begin(1);
        }
        const ExecutionLogContents c = readExecutionLog(file);
        const auto& a = c.plan.identity;
        const auto& e = b.plan.identity;
        EXPECT_EQ(a.volumeName, e.volumeName);
        EXPECT_EQ(a.volumeSerial, e.volumeSerial);
        EXPECT_EQ(a.geometry.bytesPerCluster, e.geometry.bytesPerCluster);
        EXPECT_EQ(a.geometry.totalClusters, e.geometry.totalClusters);
        ASSERT_TRUE(a.usn.has_value());
        EXPECT_EQ(a.usn->journalId, e.usn->journalId);
        EXPECT_EQ(a.usn->nextUsn, e.usn->nextUsn);
        EXPECT_EQ(a.madeAt, e.madeAt);
        EXPECT_EQ(a.strategy, e.strategy);
        EXPECT_EQ(a.effort, e.effort);
        EXPECT_EQ(c.plan.reserved, b.plan.reserved);
        EXPECT_EQ(c.plan.runs, b.plan.runs);
        EXPECT_EQ(c.plan.paths, b.plan.paths);
        ASSERT_EQ(c.plan.moves.size(), b.plan.moves.size());
        for (std::size_t i = 0; i < c.plan.moves.size(); ++i) {
            const auto& x = c.plan.moves[i];
            const auto& y = b.plan.moves[i];
            ASSERT_EQ(std::tie(x.fileReference, x.reason, x.fromParking, x.extra, x.clusters, x.fileFlags,
                               x.region.start, x.region.length),
                      std::tie(y.fileReference, y.reason, y.fromParking, y.extra, y.clusters, y.fileFlags,
                               y.region.start, y.region.length))
                << i;
        }
        EXPECT_TRUE(c.settled[0]);
        EXPECT_TRUE(c.settled[4]);
        EXPECT_FALSE(c.settled[1]);
        EXPECT_EQ(c.inFlight, 1U);
        EXPECT_FALSE(c.ended);
        EXPECT_FALSE(c.truncated);
        const auto rest = movesToResume(c);
        EXPECT_EQ(rest.size(), b.plan.moves.size() - 2);
        EXPECT_EQ(rest.front(), 1U);
        std::filesystem::remove_all(dir);
    }

    TEST(ExecutionLog, OnlyTheLastLineMayBeCutShort) {
        const Bed b = bed(22);
        const auto dir = scratch("truncated");
        const auto file = dir / "a.jsonl";
        {
            ExecutionLogWriter log(file, b.plan);
            log.begin(0);
        }
        const std::string text = textOf(file);
        const std::string cut = text + "{\"type\":\"outcome\",\"index\":0,\"outc";
        const auto c = parseExecutionLog(cut);
        EXPECT_TRUE(c.truncated);
        EXPECT_EQ(c.inFlight, 0U);

        std::string damaged = text;
        damaged.insert(damaged.find('\n') + 1, "{\"type\":\"move\",\"ind\n");
        EXPECT_THROW((void)parseExecutionLog(damaged), ExecutionLogError);
        EXPECT_THROW((void)parseExecutionLog(text.substr(text.find('\n') + 1)), ExecutionLogError); // No plan.
        std::string other = text;
        other.replace(other.find("\"version\":1"), 11, "\"version\":7");
        EXPECT_THROW((void)parseExecutionLog(other), ExecutionLogError);
        const std::size_t lastMove = text.rfind("{\"type\":\"move\"");
        EXPECT_THROW((void)parseExecutionLog(text.substr(0, lastMove)), ExecutionLogError); // A move line missing.
        std::filesystem::remove_all(dir);
    }

    // Whether every planned file lies at its last destinations.
    void expectAtTargets(const SimVolume& volume, const ExecutionPlan& plan) {
        std::map<std::uint64_t, std::vector<ClusterRun>> last;
        for (const auto& m : plan.moves) {
            last[m.fileReference] = core::execute::lcnRunsOf(core::execute::asStreamRuns(plan.destinations(m)));
        }
        for (const auto& [reference, runs] : last) {
            EXPECT_EQ(core::execute::lcnRunsOf(*volume.runsOf(reference)), runs) << reference;
        }
    }

    TEST(ExecutionLog, CrashedRunResumesAndCompletesThePlan) {
        const Bed b = bed(23);
        const auto dir = scratch("resume");
        const auto file = newExecutionLogPath(dir, b.plan.identity.volumeName);
        SimVolume volume(*b.snapshot);
        SimDevice device(volume);
        ExecutionRunControls controls;
        controls.options.chunkBytes = 16 * 4096;
        controls.options.prefetch = false;

        // The process dies in the middle of a move of several calls.
        device.beforeCall = [&](std::uint64_t, const core::execute::MoveCall& call) {
            if (device.calls >= 50 && call.vcn != 0) {
                throw testing::SimulatedCrash(); // Not the first call of its move.
            }
        };
        const auto order = core::execute::allMoves(b.plan);
        {
            ExecutionLogWriter log(file, b.plan);
            const auto replay = core::execute::replayPlan(b.plan, order, volume.freeRuns(), volume);
            EXPECT_THROW((void)executeLogged(b.plan, replay, device, log, controls), testing::SimulatedCrash);
        }
        EXPECT_EQ(unfinishedExecutionLog(dir, b.plan.identity.volumeName), file);

        // Resume: the log gives the plan and what is left; the volume says how far the interrupted move got.
        device.beforeCall = nullptr;
        const ExecutionLogContents crashed = readExecutionLog(file);
        ASSERT_TRUE(crashed.inFlight.has_value());
        const auto rest = movesToResume(crashed);
        EXPECT_LT(rest.size(), order.size());
        EXPECT_EQ(rest.front(), *crashed.inFlight);
        const auto replay = core::execute::replayPlan(crashed.plan, rest, volume.freeRuns(), volume);
        EXPECT_TRUE(replay.dropped.empty());
        EXPECT_EQ(replay.partlyDone, 1U);
        {
            ExecutionLogWriter log(file);
            const auto report = executeLogged(crashed.plan, replay, device, log, controls);
            EXPECT_EQ(report.totals.skipped + report.totals.partial, 0U);
        }
        expectAtTargets(volume, b.plan);
        const ExecutionLogContents done = readExecutionLog(file);
        EXPECT_TRUE(done.ended);
        EXPECT_TRUE(movesToResume(done).empty());
        EXPECT_FALSE(unfinishedExecutionLog(dir, b.plan.identity.volumeName).has_value());
        std::filesystem::remove_all(dir);
    }

}} // namespace icd::services
