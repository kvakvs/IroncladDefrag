// The executor (T102, plan.md 10.4) on a volume in memory: a plan runs move for move and is verified; chunked calls,
// pause and resume, cancel mid-run (the volume stays valid and the rest of the plan finishes after a revalidation),
// a destination taken just before its move (moved elsewhere in its region, nothing else disturbed), locked and changed
// files skipped with reasons, a throughput limit, and the snapshot after execution equals the volume.

#include "services/execute/ExecutedSnapshot.h"
#include "services/execute/Executor.h"
#include "services/execute/Revalidate.h"

#include "SimDevice.h"
#include "core/classify/RuleEngine.h"
#include "core/snapshot/FreeRunList.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/plan/PlanJob.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <thread>

namespace icd::services { namespace {

    using core::ClusterCount;
    using core::ClusterRun;
    using core::execute::ExecutionPlan;
    using testing::SimDevice;
    using testing::SimVolume;

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // A synthetic volume, its plan at one level, and the volume in memory to run it on.
    struct Bed {
        std::shared_ptr<core::Snapshot> snapshot; // Planned volume.
        ExecutionPlan plan; // The level's plan.
        std::unique_ptr<SimVolume> volume; // Where it runs.
    };

    // Plans a seeded volume with the default preset at a level.
    Bed bed(std::uint64_t seed, core::strategy::Effort level) {
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
        id.geometry = b.snapshot->geometry;
        b.plan =
            core::execute::executionPlanOf(outcome.levels.at(static_cast<std::size_t>(level)).result, *b.snapshot, id);
        b.volume = std::make_unique<SimVolume>(*b.snapshot);
        return b;
    }

    // Options without the prefetch thread (deterministic), small chunks so moves take several calls.
    ExecuteOptions smallChunks() {
        ExecuteOptions o;
        o.chunkBytes = 16 * 4096;
        return o;
    }

    // Each file's last destinations in the plan.
    std::map<std::uint64_t, std::vector<ClusterRun>> finalRuns(const ExecutionPlan& plan) {
        std::map<std::uint64_t, std::vector<ClusterRun>> out;
        for (const auto& m : plan.moves) {
            const auto d = plan.destinations(m);
            out[m.fileReference] = core::execute::lcnRunsOf(core::execute::asStreamRuns(d));
        }
        return out;
    }

    // Whether every planned file lies at its last destinations.
    void expectAtTargets(const SimVolume& volume, const ExecutionPlan& plan) {
        for (const auto& [reference, runs] : finalRuns(plan)) {
            const auto now = volume.runsOf(reference);
            ASSERT_TRUE(now.has_value());
            EXPECT_EQ(core::execute::lcnRunsOf(*now), runs) << reference;
        }
    }

    TEST(Executor, RunsAPlanAndVerifiesEveryMove) {
        for (const bool prefetch : {false, true}) {
            Bed b = bed(11, core::strategy::Effort::Maximum);
            SimDevice device(*b.volume);
            ExecuteOptions o = smallChunks();
            o.prefetch = prefetch;
            const auto order = core::execute::allMoves(b.plan);
            const ExecutionReport report = executePlan(b.plan, order, device, o, {});
            EXPECT_FALSE(report.cancelled);
            ASSERT_EQ(report.records.size(), b.plan.moves.size());
            EXPECT_EQ(report.totals.moved, b.plan.moves.size());
            EXPECT_EQ(report.totals.unverified, 0U);
            EXPECT_EQ(report.totals.replaced, 0U);
            EXPECT_EQ(report.totals.clustersMoved, core::execute::plannedClusters(b.plan));
            EXPECT_GT(device.calls, b.plan.moves.size()); // Moves of more than 16 clusters took several calls.
            expectAtTargets(*b.volume, b.plan);
        }
    }

    TEST(Executor, PauseHoldsAndResumeContinues) {
        Bed b = bed(12, core::strategy::Effort::Moderate);
        SimDevice device(*b.volume);
        core::PauseGate gate;
        std::vector<std::string> events;
        std::jthread resumer;
        ExecutionControls c;
        c.pause = &gate;
        c.hooks.onEvent = [&](std::string_view e) { events.emplace_back(e); };
        c.hooks.onRecord = [&](const MoveRecord& r) {
            if (r.move == 3) {
                gate.pause();
                resumer = std::jthread([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    gate.resume();
                });
            }
        };
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, smallChunks(), c);
        EXPECT_EQ(events, (std::vector<std::string>{"paused", "resumed"}));
        EXPECT_EQ(report.totals.moved, b.plan.moves.size());
        expectAtTargets(*b.volume, b.plan);
    }

    TEST(Executor, CancelMidMoveLeavesAValidVolumeAndTheRestFinishesLater) {
        Bed b = bed(13, core::strategy::Effort::Maximum);
        SimDevice device(*b.volume);
        core::CancelSource cancel;
        // Cancel inside a move of several calls: after its first call went through.
        std::uint64_t cutAt = 0;
        device.beforeCall = [&](std::uint64_t, const core::execute::MoveCall&) {
            if (device.calls == 40 && cutAt == 0) {
                cutAt = device.calls;
                cancel.cancel();
            }
        };
        ExecutionControls c;
        c.token = cancel.token();
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, smallChunks(), c);
        ASSERT_TRUE(report.cancelled);
        ASSERT_LT(report.records.size(), order.size());

        // Every file still has all its clusters; the moves not finished revalidate and run.
        std::vector<std::uint32_t> rest;
        for (std::uint32_t i = 0; i < order.size(); ++i) {
            const bool finished = std::ranges::any_of(
                report.records, [&](const MoveRecord& r) { return r.move == i && r.outcome != MoveOutcome::Partial; });
            if (!finished) {
                rest.push_back(i);
            }
        }
        const auto replay = core::execute::replayPlan(b.plan, rest, b.volume->freeRuns(), *b.volume);
        EXPECT_TRUE(replay.dropped.empty());
        const auto second = executePlan(b.plan, replay.kept, device, smallChunks(), {});
        EXPECT_FALSE(second.cancelled);
        EXPECT_EQ(second.totals.skipped + second.totals.partial, 0U);
        expectAtTargets(*b.volume, b.plan);
    }

    TEST(Executor, TakenDestinationMovesElsewhereInItsRegion) {
        Bed b = bed(14, core::strategy::Effort::Moderate);
        SimDevice device(*b.volume);
        // Just before move 5 starts, another program takes its first destination cluster.
        const std::uint32_t victim = 5;
        const ClusterRun taken{b.plan.destinations(b.plan.moves[victim]).front().start, 1};
        ExecutionControls c;
        c.hooks.onBegin = [&](std::uint32_t move) {
            if (move == victim && b.volume->isFree(taken)) {
                b.volume->occupy(taken);
            }
        };
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, smallChunks(), c);
        const MoveRecord& r = report.records.at(victim);
        ASSERT_EQ(r.move, victim);
        EXPECT_TRUE(r.replaced);
        EXPECT_EQ(r.outcome, MoveOutcome::Moved);
        EXPECT_TRUE(r.verified);
        const auto& region = b.plan.moves[victim].region;
        for (const auto& run : r.runsAfter) {
            EXPECT_GE(run.lcn, region.start);
            EXPECT_LE(run.lcn + run.length, region.end());
        }
        EXPECT_EQ(b.volume->ownerOf(taken.start), SimVolume::systemOwner);
        EXPECT_EQ(report.totals.skipped, 0U); // The replacement avoided every later destination.
        EXPECT_EQ(report.totals.replaced, 1U);
    }

    TEST(Executor, RefusedCallKeepsWhatNtfsMovedBeforeIt) {
        // NTFS moves a large call in pieces and keeps those done before it refuses (T104b): the executor must count
        // them, move only the rest elsewhere, and leave a snapshot that matches the volume.
        Bed b = bed(18, core::strategy::Effort::Moderate);
        SimDevice device(*b.volume);
        device.pieceClusters = 8;
        std::uint32_t victim = 0;
        while (victim < b.plan.moves.size() &&
               (b.plan.moves[victim].clusters < 40 || b.plan.destinations(b.plan.moves[victim]).front().length < 40)) {
            ++victim;
        }
        ASSERT_LT(victim, b.plan.moves.size());
        const ClusterRun blocked{b.plan.destinations(b.plan.moves[victim]).front().start + 20, 1};
        ExecutionControls c;
        c.hooks.onBegin = [&](std::uint32_t move) {
            if (move == victim && b.volume->isFree(blocked)) {
                b.volume->occupy(blocked);
            }
        };
        ExecuteOptions o;
        o.prefetch = false; // 16 MB calls: the move is one call, refused part way through.
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, o, c);
        const MoveRecord& r = report.records.at(victim);
        EXPECT_TRUE(r.verified);
        EXPECT_GE(r.clustersMoved, 16U); // The two pieces before the blocked cluster.
        EXPECT_EQ(r.outcome == MoveOutcome::Moved, r.clustersMoved == b.plan.moves[victim].clusters);
        EXPECT_EQ(report.totals.unverified, 0U);
        core::Snapshot base = *b.snapshot;
        core::InlineExecutor serial;
        const auto volumeFree = b.volume->freeRuns();
        const auto after = snapshotAfter(base, b.plan, report.records, serial);
        ASSERT_TRUE(after.has_value());
        // The probe cluster is not a file of the snapshot: count it as free there.
        std::vector<ClusterRun> expected = volumeFree;
        expected.push_back(blocked);
        const core::FreeRunList merged(std::move(expected), b.snapshot->geometry.totalClusters);
        EXPECT_EQ(std::vector(after->freeSpace.runs().begin(), after->freeSpace.runs().end()),
                  std::vector(merged.runs().begin(), merged.runs().end()));
    }

    TEST(Executor, LockedAndChangedFilesAreSkippedWithReasons) {
        Bed b = bed(15, core::strategy::Effort::Moderate);
        SimDevice device(*b.volume);
        const auto& lockedMove = b.plan.moves[2];
        device.locked.insert(lockedMove.fileReference);
        const auto& changedMove = b.plan.moves[7];
        const auto first = b.volume->clustersOf(changedMove.fileReference).front();
        const ClusterRun spare = b.volume->freeRuns().back();
        ASSERT_TRUE(b.volume->move(changedMove.fileReference, first.stream, first.vcn, spare.end() - 1, 1));
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, smallChunks(), {});
        EXPECT_EQ(report.records.size(), order.size()); // Never stops for one bad file.
        const auto reasonOf = [&](std::uint32_t move) {
            return std::ranges::find(report.records, move, &MoveRecord::move)->reason;
        };
        EXPECT_EQ(reasonOf(2), "locked");
        EXPECT_EQ(reasonOf(7), "file-changed");
        EXPECT_GE(report.totals.skipped, 2U);
        for (const MoveRecord& r : report.records) {
            EXPECT_EQ(r.outcome == MoveOutcome::Skipped, !r.reason.empty()) << r.move;
        }
    }

    TEST(Executor, ThroughputLimitWaits) {
        EXPECT_DOUBLE_EQ(throttleDelaySeconds({1000, 0.5}, 0), 0.0);
        EXPECT_DOUBLE_EQ(throttleDelaySeconds({1000, 0.5}, 1000), 0.5);
        EXPECT_DOUBLE_EQ(throttleDelaySeconds({1000, 2.0}, 1000), 0.0);

        Bed b = bed(16, core::strategy::Effort::Fast);
        SimDevice device(*b.volume);
        ExecuteOptions o = smallChunks();
        const ClusterCount clusters = core::execute::plannedClusters(b.plan);
        o.throughputLimit = clusters * 4096 * 5; // The whole plan in 0.2 s.
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, o, {});
        EXPECT_GE(report.seconds, 0.15);
        EXPECT_EQ(report.totals.moved, order.size());
    }

    TEST(Executor, SnapshotAfterExecutionEqualsTheVolume) {
        Bed b = bed(17, core::strategy::Effort::Maximum);
        SimDevice device(*b.volume);
        const auto order = core::execute::allMoves(b.plan);
        const auto report = executePlan(b.plan, order, device, smallChunks(), {});
        core::Snapshot base = *b.snapshot;
        core::InlineExecutor serial;
        const auto after = snapshotAfter(base, b.plan, report.records, serial);
        ASSERT_TRUE(after.has_value());
        EXPECT_EQ(std::vector(after->freeSpace.runs().begin(), after->freeSpace.runs().end()), b.volume->freeRuns());
        const core::execute::SnapshotFiles files(*after, b.plan);
        for (const auto& m : b.plan.moves) {
            EXPECT_EQ(core::execute::lcnRunsOf(*files.runsOf(m.fileReference)),
                      core::execute::lcnRunsOf(*b.volume->runsOf(m.fileReference)));
        }
        // A record without runs read back cannot be trusted: no snapshot.
        auto records = report.records;
        records.front().runsAfter.clear();
        core::Snapshot again = *b.snapshot;
        EXPECT_FALSE(snapshotAfter(again, b.plan, records, serial).has_value());
    }

}} // namespace icd::services
