#include "services/plan/PlanJob.h"

#include "core/classify/Catalog.h"
#include "core/classify/RuleEngine.h"
#include "core/format/Count.h"
#include "core/map/ZoneFitColorMode.h"
#include "core/planner/SortZone.h"
#include "core/planner/Validator.h"
#include "core/planner/levels/FastLevel.h"
#include "core/planner/levels/MaximumLevel.h"
#include "core/planner/levels/ModerateLevel.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <format>

namespace icd::services {

    namespace {

        using core::planner::PlanResult;
        using core::strategy::Effort;

        // Seconds since a start time.
        double since(std::chrono::steady_clock::time_point start) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        // Plans one level.
        PlanResult planOne(Effort effort, const core::planner::PlanInput& input,
                           const core::planner::ZoneAssignment& assignment, core::Executor& executor,
                           const core::CancelToken& token) {
            switch (effort) {
            case Effort::Fast:
                return core::planner::planFast(input, assignment, executor, token);
            case Effort::Moderate:
                return core::planner::planModerate(input, assignment, executor, token);
            default:
                return core::planner::planMaximum(input, assignment, executor, token);
            }
        }

        // Validation findings of a planned level.
        std::vector<std::string> validate(const core::Snapshot& snapshot, const PlanResult& result,
                                          core::Executor& executor) {
            const auto v =
                core::planner::validatePlan(snapshot, result.plan, {.layout = &result.layout, .executor = &executor});
            std::vector<std::string> out;
            out.reserve(v.issues.size());
            for (const auto& issue : v.issues) {
                out.push_back(issue.message);
            }
            return out;
        }

        // The Plan tab column of one level.
        PlanLevel levelOf(const LevelOutcome& o, std::uint32_t bytesPerCluster, EffortLevel level) {
            const auto& plan = o.result.plan;
            PlanLevel out;
            out.level = level;
            out.filesToMove = plan.totals.files;
            out.bytesToMove = plan.totals.clustersMoved * bytesPerCluster;
            out.lowerBoundBytes = plan.lowerBound * bytesPerCluster;
            out.estimate = std::chrono::seconds(std::llround(o.result.metrics.seconds));
            out.fragmentationAfter = o.result.metrics.fragmentedAfter;
            out.zoneFitAfter = o.result.metrics.zoneFitAfter;
            return out;
        }

        // The warnings of one level.
        void warningsOf(const LevelOutcome& o, const std::string& name, std::vector<std::string>& out) {
            const auto& plan = o.result.plan;
            if (!plan.skipped.empty()) {
                out.push_back(std::format("{}: {} files cannot move and stay where they are.", name,
                                          core::formatCount(plan.skipped.size())));
            }
            for (const std::string& note : plan.notes) {
                out.push_back(std::format("{}: {}", name, note));
            }
            if (!o.issues.empty()) {
                out.push_back(
                    std::format("{}: the plan failed validation ({}); it will not run.", name, o.issues.front()));
            }
        }

    } // namespace

    std::int64_t fileTimeNow() {
        // FILETIME counts 100 ns ticks from 1601; the Unix epoch is 11,644,473,600 s later.
        constexpr std::int64_t unixEpoch = 116'444'736'000'000'000;
        const auto since = std::chrono::system_clock::now().time_since_epoch();
        return unixEpoch + std::chrono::duration_cast<std::chrono::nanoseconds>(since).count() / 100;
    }

    PlanRequest makePlanRequest(const core::DriveInfo& drive, std::shared_ptr<const core::Snapshot> snapshot,
                                std::vector<std::uint8_t> classes, core::strategy::ZoneScript script) {
        PlanRequest r;
        r.snapshot = std::move(snapshot);
        r.classes = std::move(classes);
        r.script = std::move(script);
        r.now = fileTimeNow();
        r.shadowCopies = drive.badges.vssPresent;
        if (const auto& ntfs = drive.layout.ntfs; ntfs && ntfs->mftZoneEnd > ntfs->mftZoneStart) {
            r.reserved.push_back({ntfs->mftZoneStart, ntfs->mftZoneEnd - ntfs->mftZoneStart});
        }
        return r;
    }

    PlanOutcome planLevels(const PlanRequest& request, core::Executor& executor, const core::CancelToken& token,
                           core::ProgressSink* progress) {
        const auto start = std::chrono::steady_clock::now();
        const core::Snapshot& snapshot = *request.snapshot;
        core::planner::SortKeyCache sortKeys; // The levels share the keys of each zone order.
        const core::planner::PlanInput input{.snapshot = &snapshot,
                                             .script = &request.script,
                                             .catalog = &core::classify::builtinCatalog(),
                                             .classes = request.classes,
                                             .churn = request.churn,
                                             .now = request.now,
                                             .reserved = request.reserved,
                                             .shadowCopies = request.shadowCopies,
                                             .fastBudgetMultiplier = request.fastBudgetMultiplier,
                                             .time = {},
                                             .sortKeys = &sortKeys};
        std::atomic<std::uint64_t> done{0};
        const auto report = [&](std::uint64_t count) {
            if (progress != nullptr) {
                progress->report({.stage = "Planning", .done = count, .total = 3, .message = request.script.name});
            }
        };
        report(0);
        PlanOutcome outcome;
        outcome.assignment = core::planner::assignInput(input, executor, token);
        const core::planner::ZoneAssignment& assignment = outcome.assignment;
        outcome.bytesPerCluster = snapshot.geometry.bytesPerCluster;
        executor.parallelFor(
            {.count = 3, .grain = request.parallelLevels ? 1U : 3U},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t level = begin; level < end; ++level) {
                    const auto levelStart = std::chrono::steady_clock::now();
                    LevelOutcome& o = outcome.levels.at(level);
                    o.result = planOne(core::strategy::allEfforts.at(level), input, assignment, executor, token);
                    if (request.validate) {
                        const core::planner::StageScope stage(o.result.stages, "validate");
                        o.issues = validate(snapshot, o.result, executor);
                    }
                    o.seconds = since(levelStart);
                    report(++done);
                }
            },
            token);
        outcome.seconds = since(start);
        return outcome;
    }

    PlanSummary planSummary(const PlanOutcome& outcome) {
        PlanSummary out;
        constexpr std::array levels{EffortLevel::Fast, EffortLevel::Moderate, EffortLevel::Maximum};
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const LevelOutcome& o = outcome.levels.at(i);
            out.levels.at(i) = levelOf(o, outcome.bytesPerCluster, levels.at(i));
            warningsOf(o, effortName(levels.at(i)), out.warnings);
        }
        return out;
    }

    std::array<LevelOverlays, 3> levelOverlays(const PlanOutcome& outcome, const core::Snapshot& snapshot) {
        std::array<LevelOverlays, 3> out;
        for (std::size_t level = 0; level < out.size(); ++level) {
            const auto& result = outcome.levels.at(level).result;
            out.at(level) = {.zoneFit = core::zoneFitOverlay(snapshot, outcome.assignment, result.layout),
                             .preview = core::planOverlay(result.plan)};
        }
        return out;
    }

    JobFunction makePlanJob(PlanRequest request, std::shared_ptr<PlanOutput> output) {
        return [request = std::move(request), output = std::move(output)](JobContext& context) mutable {
            const core::Snapshot& snapshot = *request.snapshot;
            if (request.classes.size() != snapshot.files.size()) {
                const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
                request.classes =
                    engine.classify(snapshot, {.now = request.now, .churn = {}}, context.executor, context.token);
            }
            auto outcome =
                std::make_shared<PlanOutcome>(planLevels(request, context.executor, context.token, &context.progress));
            outcome->overlays = levelOverlays(*outcome, snapshot);
            output->set(std::move(outcome), std::make_shared<const PlanRequest>(std::move(request)));
        };
    }

} // namespace icd::services
