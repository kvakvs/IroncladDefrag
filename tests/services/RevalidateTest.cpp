// Revalidation before execution (T101, plan.md 10.3): foreign, reformatted, resized and stale plans are refused; a
// replay on the unchanged volume keeps every move; scripted changes between planning and execution (a planned file
// deleted, another changed, a destination taken by a new file) drop exactly what they must, and every move kept still
// runs in order on the changed volume; moves cut short are recognised.

#include "services/execute/Revalidate.h"

#include "SimVolume.h"
#include "core/classify/RuleEngine.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/plan/PlanJob.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>

namespace icd::services { namespace {

    using core::ClusterCount;
    using core::ClusterRun;
    using core::execute::DropReason;
    using core::execute::ExecutionPlan;
    using core::execute::MovePiece;
    using testing::SimVolume;

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // A seeded synthetic volume and its plans.
    struct Planned {
        std::shared_ptr<core::Snapshot> snapshot; // The planned volume.
        PlanOutcome outcome; // Its three levels.
    };

    // Plans a synthetic volume with the default preset.
    Planned planned(std::uint64_t seed) {
        core::SynthOptions options;
        options.seed = seed;
        options.files = 3000;
        options.geometry.totalClusters = 1ULL << 20U;
        options.fillPercent = 55;
        options.fragmentedShare = 0.3;
        Planned out;
        out.snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        r.script = core::strategy::builtinPresets().front();
        r.now = 20'000 * day;
        core::InlineExecutor serial;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        r.classes = engine.classify(*out.snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = out.snapshot;
        out.outcome = planLevels(r, serial);
        return out;
    }

    // The execution plan of one level.
    ExecutionPlan executionPlan(const Planned& p, core::strategy::Effort level) {
        core::execute::PlanIdentity id;
        id.volumeName = "\\\\?\\Volume{test}\\";
        id.geometry = p.snapshot->geometry;
        id.madeAt = 20'000 * day;
        id.effort = level;
        return core::execute::executionPlanOf(p.outcome.levels.at(static_cast<std::size_t>(level)).result, *p.snapshot,
                                              id);
    }

    // The pieces a kept move still has on the simulated volume, as the executor will find them.
    std::vector<MovePiece> piecesOn(const SimVolume& sim, const ExecutionPlan& plan, std::uint32_t index) {
        const auto& m = plan.moves[index];
        const auto now = sim.runsOf(m.fileReference);
        EXPECT_TRUE(now.has_value());
        auto pieces = core::execute::matchMove(*now, {plan.sources(m), plan.destinations(m)});
        if (!pieces && m.fromParking) {
            pieces = core::execute::remapMove(*now, plan.destinations(m));
        }
        EXPECT_TRUE(pieces.has_value()) << "move " << index;
        return pieces.value_or(std::vector<MovePiece>{});
    }

    // Runs the kept moves on the simulated volume in order; every piece must find its destination free.
    void runKept(SimVolume& sim, const ExecutionPlan& plan, std::span<const std::uint32_t> kept) {
        for (const std::uint32_t i : kept) {
            for (const MovePiece& p : piecesOn(sim, plan, i)) {
                if (!p.done) {
                    ASSERT_TRUE(sim.move(plan.moves[i].fileReference, p.stream, p.vcn, p.to, p.length))
                        << "move " << i << " piece at " << p.to;
                }
            }
        }
    }

    // The planned identity of a drive.
    core::DriveInfo driveFor(const core::execute::PlanIdentity& id) {
        core::DriveInfo d;
        d.layout.volumeName = id.volumeName;
        d.layout.bytesPerCluster = id.geometry.bytesPerCluster;
        d.layout.totalClusters = id.geometry.totalClusters;
        d.layout.ntfs = core::NtfsLayout{};
        d.layout.ntfs->serialNumber = id.volumeSerial;
        return d;
    }

    TEST(Revalidate, RefusesForeignAndStalePlans) {
        core::execute::PlanIdentity id;
        id.volumeName = "\\\\?\\Volume{a}\\";
        id.volumeSerial = 42;
        id.geometry = {4096, 1000};
        id.madeAt = 100 * fileTimeTicksPerHour;
        const PlanAge fresh{id.madeAt + fileTimeTicksPerHour, 24 * fileTimeTicksPerHour};
        EXPECT_FALSE(planRefusal(id, driveFor(id), fresh));

        core::DriveInfo other = driveFor(id);
        other.layout.volumeName = "\\\\?\\Volume{b}\\";
        EXPECT_TRUE(planRefusal(id, other, fresh));
        core::DriveInfo reformatted = driveFor(id);
        reformatted.layout.ntfs->serialNumber = 43;
        EXPECT_TRUE(planRefusal(id, reformatted, fresh));
        core::DriveInfo resized = driveFor(id);
        resized.layout.totalClusters = 1001;
        EXPECT_TRUE(planRefusal(id, resized, fresh));
        core::DriveInfo noNtfs = driveFor(id);
        noNtfs.layout.ntfs.reset();
        EXPECT_TRUE(planRefusal(id, noNtfs, fresh));

        const PlanAge late{id.madeAt + 25 * fileTimeTicksPerHour, 24 * fileTimeTicksPerHour};
        const auto stale = planRefusal(id, driveFor(id), late);
        ASSERT_TRUE(stale.has_value());
        EXPECT_NE(stale->find("hours old"), std::string::npos);
        EXPECT_FALSE(planRefusal(id, driveFor(id), {late.now, 0})); // 0: any age.
    }

    TEST(Revalidate, UnchangedVolumeKeepsEveryMove) {
        const Planned p = planned(5);
        for (const auto level : {core::strategy::Effort::Fast, core::strategy::Effort::Maximum}) {
            const ExecutionPlan plan = executionPlan(p, level);
            ASSERT_FALSE(plan.moves.empty());
            const auto order = core::execute::allMoves(plan);
            const auto replay = replayOnSnapshot(plan, order, *p.snapshot);
            EXPECT_TRUE(replay.dropped.empty());
            EXPECT_EQ(replay.kept, order);
            EXPECT_EQ(replay.pendingClusters, core::execute::plannedClusters(plan));
            EXPECT_EQ(replay.alreadyDone, 0U);

            SimVolume sim(*p.snapshot);
            const auto onSim = core::execute::replayPlan(plan, order, sim.freeRuns(), sim);
            EXPECT_EQ(onSim.kept, replay.kept);
            runKept(sim, plan, onSim.kept);
            // Once run, every file sits at its last destinations and a second replay has nothing left to do.
            const auto again = core::execute::replayPlan(plan, order, sim.freeRuns(), sim);
            EXPECT_EQ(again.pendingClusters, 0U);
        }
    }

    // A move whose file moves only once in the plan and which is not first, for scripted changes.
    std::vector<std::uint32_t> singleMoves(const ExecutionPlan& plan) {
        std::map<std::uint64_t, int> count;
        for (const auto& m : plan.moves) {
            ++count[m.fileReference];
        }
        std::vector<std::uint32_t> out;
        for (std::uint32_t i = 0; i < plan.moves.size(); ++i) {
            if (count[plan.moves[i].fileReference] == 1) {
                out.push_back(i);
            }
        }
        return out;
    }

    // The first of `candidates` from `from` on whose first destination cluster is free on the volume now.
    std::uint32_t withFreeDestination(const SimVolume& sim, const ExecutionPlan& plan,
                                      std::span<const std::uint32_t> candidates, std::size_t from) {
        for (std::size_t k = from; k < candidates.size(); ++k) {
            if (sim.isFree({plan.destinations(plan.moves[candidates[k]]).front().start, 1})) {
                return candidates[k];
            }
        }
        ADD_FAILURE() << "no move with a free destination";
        return candidates.back();
    }

    // A free cluster outside a move's destinations.
    core::Lcn spareFor(const SimVolume& sim, std::span<const ClusterRun> destinations) {
        const auto free = sim.freeRuns();
        for (auto r = free.rbegin(); r != free.rend(); ++r) {
            const bool inside =
                std::ranges::any_of(destinations, [&](const ClusterRun& d) { return d.contains(r->start); });
            if (!inside) {
                return r->start;
            }
        }
        ADD_FAILURE() << "no spare cluster";
        return 0;
    }

    TEST(Revalidate, ScriptedChangesAreDetected) {
        const Planned p = planned(6);
        const ExecutionPlan plan = executionPlan(p, core::strategy::Effort::Maximum);
        const auto singles = singleMoves(plan);
        ASSERT_GE(singles.size(), 30U);
        SimVolume sim(*p.snapshot);

        // A planned file deleted, another rewritten elsewhere (one cluster swapped for a free one), and the
        // destination of a third taken by a new file.
        const std::uint32_t gone = singles[3];
        sim.remove(plan.moves[gone].fileReference);
        const std::uint32_t changed = singles[10];
        const auto& c = plan.moves[changed];
        const auto first = sim.clustersOf(c.fileReference).front();
        ASSERT_TRUE(sim.move(c.fileReference, first.stream, first.vcn, spareFor(sim, plan.destinations(c)), 1));
        const std::uint32_t blocked = withFreeDestination(sim, plan, singles, 20);
        sim.occupy({plan.destinations(plan.moves[blocked]).front().start, 1});

        const auto order = core::execute::allMoves(plan);
        const auto replay = core::execute::replayPlan(plan, order, sim.freeRuns(), sim);
        const auto reasonOf = [&](std::uint32_t move) -> std::optional<DropReason> {
            const auto found = std::ranges::find(replay.dropped, move, &core::execute::DroppedMove::move);
            return found == replay.dropped.end() ? std::nullopt : std::optional(found->reason);
        };
        EXPECT_EQ(reasonOf(gone), DropReason::FileGone);
        EXPECT_EQ(reasonOf(changed), DropReason::FileChanged);
        EXPECT_EQ(reasonOf(blocked), DropReason::DestinationInUse);
        EXPECT_EQ(replay.kept.size() + replay.dropped.size(), plan.moves.size());
        EXPECT_LT(replay.dropped.size(), plan.moves.size() / 4); // Only what depended on the changes.
        runKept(sim, plan, replay.kept);
    }

    TEST(Revalidate, MovesCutShortAreKeptWithWhatIsLeft) {
        const Planned p = planned(7);
        const ExecutionPlan plan = executionPlan(p, core::strategy::Effort::Moderate);
        SimVolume sim(*p.snapshot);
        const auto order = core::execute::allMoves(plan);
        // The first move of a file with at least two clusters stops halfway through its first piece.
        std::uint32_t cut = 0;
        ClusterCount before = 0;
        while (cut < plan.moves.size() && plan.moves[cut].clusters < 2) {
            before += plan.moves[cut++].clusters;
        }
        ASSERT_LT(cut, plan.moves.size());
        runKept(sim, plan, std::span(order).first(cut));
        const MovePiece first = piecesOn(sim, plan, cut).front();
        const ClusterCount half = std::max<ClusterCount>(1, first.length / 2);
        ASSERT_TRUE(sim.move(plan.moves[cut].fileReference, first.stream, first.vcn, first.to, half));

        const std::vector<std::uint32_t> rest(order.begin() + cut, order.end());
        const auto replay = core::execute::replayPlan(plan, rest, sim.freeRuns(), sim);
        EXPECT_TRUE(replay.dropped.empty());
        EXPECT_EQ(replay.partlyDone + replay.alreadyDone, 1U);
        EXPECT_EQ(replay.pendingClusters, core::execute::plannedClusters(plan) - before - half);
        runKept(sim, plan, replay.kept);
    }

}} // namespace icd::services
