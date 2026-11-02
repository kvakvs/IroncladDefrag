#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "core/model/DriveInfo.h"
#include "core/planner/LevelPlanner.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"
#include "services/job/JobRunner.h"
#include "services/workflow/FakeResults.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace icd::services {

    // What planning a volume needs (the GUI and icd-cli fill it from an analysis).
    struct PlanRequest {
        std::shared_ptr<const core::Snapshot> snapshot; // The analysed volume; kept alive by the request.
        core::strategy::ZoneScript script; // The strategy (a preset or the user's edit).
        std::vector<std::uint8_t> classes; // Per file: its built-in catalog class.
        std::vector<std::uint8_t> churn; // Per file: churn from the journal; empty when unknown.
        std::int64_t now = 0; // FILETIME ages are measured from (the scan time).
        std::vector<core::ClusterRun> reserved; // The MFT zone, kept free when possible.
        bool shadowCopies = false; // The volume has shadow copies (plan.md 9.4).
        double fastBudgetMultiplier = 1.0; // Settings x1/x2/x4/x10 (plan.md 9.2.1).
        bool parallelLevels = true; // Plan the three levels at once (false when memory is short, plan.md 6.6).
        bool validate = true; // Replay every plan through the validator (plan.md 6.4 step 6).
    };

    // A request for a drive's analysed snapshot: the MFT zone as reserved runs, shadow copies from the drive's badge,
    // ages from now. The GUI and icd-cli both build requests this way.
    [[nodiscard]] PlanRequest makePlanRequest(const core::DriveInfo& drive,
                                              std::shared_ptr<const core::Snapshot> snapshot,
                                              std::vector<std::uint8_t> classes, core::strategy::ZoneScript script);

    // The current time as a FILETIME (100 ns ticks since 1601), what ages are measured from.
    [[nodiscard]] std::int64_t fileTimeNow();

    // One planned level.
    struct LevelOutcome {
        core::planner::PlanResult result; // Plan, layout, metrics.
        std::vector<std::string> issues; // Validator findings; empty when the plan is valid (or not validated).
        double seconds = 0.0; // Wall time spent planning (and validating) this level.
    };

    // The three levels of one plan request (plan.md 9.2.2): Fast, Moderate, Maximum.
    struct PlanOutcome {
        std::array<LevelOutcome, 3> levels; // By core::strategy::Effort.
        double seconds = 0.0; // Wall time of the whole request.
        std::uint32_t bytesPerCluster = 4096; // Of the planned volume, for byte figures.
    };

    // Plans all three levels (plan.md 6.4): zones assigned once, then the levels in parallel (or one after another),
    // each using the executor inside, then each validated. The same plans for any executor. Throws OperationCancelled
    // when cancelled.
    [[nodiscard]] PlanOutcome planLevels(const PlanRequest& request, core::Executor& executor,
                                         const core::CancelToken& token = {}, core::ProgressSink* progress = nullptr);

    // The Plan tab's figures of an outcome: per level files and bytes to move, lower bound, estimate, fragmentation and
    // zone fit after; warnings for skipped files, unreached goals and validation problems.
    [[nodiscard]] PlanSummary planSummary(const PlanOutcome& outcome);

    // Hands a finished plan from the job to the UI.
    struct PlanOutput {
        // Stores the outcome (worker side).
        void set(std::shared_ptr<const PlanOutcome> value) {
            const std::scoped_lock lock(mutex);
            outcome = std::move(value);
        }

        // Takes the outcome (UI side); null when none arrived.
        [[nodiscard]] std::shared_ptr<const PlanOutcome> take() {
            const std::scoped_lock lock(mutex);
            return std::exchange(outcome, nullptr);
        }

        std::mutex mutex; // Guards outcome.
        std::shared_ptr<const PlanOutcome> outcome; // The newest outcome.
    };

    // A job that plans a request and stores the outcome in `output`.
    [[nodiscard]] JobFunction makePlanJob(PlanRequest request, std::shared_ptr<PlanOutput> output);

} // namespace icd::services
