#include "services/workflow/Workflow.h"

#include <array>
#include <optional>

namespace icd::services {

    namespace {

        // Set of states as a bit mask.
        using StateMask = std::uint32_t;

        // The mask of one state.
        constexpr StateMask bit(WorkflowState state) { return StateMask{1} << static_cast<unsigned>(state); }

        // States in which a job runs.
        constexpr StateMask busyStates = bit(WorkflowState::Trimming) | bit(WorkflowState::Analyzing) |
            bit(WorkflowState::Planning) | bit(WorkflowState::Executing) | bit(WorkflowState::Paused);

        // States from which a new analysis may start (re-analysis drops the plan).
        constexpr StateMask analyzable = bit(WorkflowState::NotAnalyzed) | bit(WorkflowState::Analyzed) |
            bit(WorkflowState::Reviewing) | bit(WorkflowState::Executed);

        // States in which a strategy can be chosen and a plan built.
        constexpr StateMask planable = bit(WorkflowState::Analyzed) | bit(WorkflowState::Reviewing);

        // Every state.
        constexpr StateMask allStates = bit(WorkflowState::Executed) * 2 - 1;

        // Where an action may run and what it does.
        struct Rule {
            StateMask allowed = 0; // States in which the action is allowed.
            std::optional<WorkflowState> next; // State after the action; empty keeps the state.
            WorkflowJob job = WorkflowJob::None; // Job the caller starts.
        };

        // Rules indexed by WorkflowAction.
        constexpr std::array<Rule, 12> rules{{
            {allStates & ~busyStates, std::nullopt, WorkflowJob::None}, // SelectDrive
            {allStates & ~busyStates, std::nullopt, WorkflowJob::None}, // RefreshDrives
            {analyzable, WorkflowState::Analyzing, WorkflowJob::Analyze}, // Analyze
            {analyzable, WorkflowState::Analyzing, WorkflowJob::Analyze}, // Quick
            {bit(WorkflowState::SsdReady), WorkflowState::Trimming, WorkflowJob::Trim}, // Trim
            {planable, WorkflowState::Analyzed, WorkflowJob::None}, // ChooseStrategy
            {planable, WorkflowState::Planning, WorkflowJob::Plan}, // BuildPlan
            {bit(WorkflowState::Reviewing), std::nullopt, WorkflowJob::None}, // Review
            {bit(WorkflowState::Reviewing), WorkflowState::Executing, WorkflowJob::Execute}, // Execute
            {bit(WorkflowState::Executing), WorkflowState::Paused, WorkflowJob::None}, // Pause
            {bit(WorkflowState::Paused), WorkflowState::Executing, WorkflowJob::None}, // Resume
            {busyStates, std::nullopt, WorkflowJob::None}, // Cancel
        }};

        // The rule of an action.
        constexpr const Rule& ruleOf(WorkflowAction action) { return rules.at(static_cast<std::size_t>(action)); }

        // The state a freshly selected drive starts in.
        WorkflowState initialState(DriveKind kind) {
            switch (kind) {
            case DriveKind::Hdd:
            case DriveKind::AnalyzeOnly:
                return WorkflowState::NotAnalyzed;
            case DriveKind::Ssd:
                return WorkflowState::SsdReady;
            case DriveKind::Unsupported:
                return WorkflowState::Unsupported;
            case DriveKind::None:
                break;
            }
            return WorkflowState::NoDrive;
        }

    } // namespace

    DriveKind driveKind(const core::DriveInfo* drive) {
        if (drive == nullptr) {
            return DriveKind::None;
        }
        if (core::isSsd(*drive)) {
            return DriveKind::Ssd;
        }
        if (!core::canOptimize(*drive)) {
            return DriveKind::Unsupported;
        }
        return core::canMove(*drive) ? DriveKind::Hdd : DriveKind::AnalyzeOnly;
    }

    bool Workflow::busy() const { return (busyStates & bit(state_)) != 0; }

    bool Workflow::can(WorkflowAction action) const {
        if (action == WorkflowAction::Cancel && cancelRequested_) {
            return false; // Already asked; the job is stopping.
        }
        if (action == WorkflowAction::Execute && movesBlocked_) {
            return false;
        }
        return (ruleOf(action).allowed & bit(state_)) != 0;
    }

    bool Workflow::selectDrive(DriveKind kind) {
        if (!can(WorkflowAction::SelectDrive)) {
            return false;
        }
        state_ = initialState(kind);
        movesBlocked_ = kind == DriveKind::AnalyzeOnly;
        quick_ = false;
        return true;
    }

    WorkflowJob Workflow::apply(WorkflowAction action) {
        if (!can(action)) {
            return WorkflowJob::None;
        }
        const Rule& rule = ruleOf(action);
        if (action == WorkflowAction::Cancel) {
            cancelRequested_ = true;
            quick_ = false; // A cancelled Quick run does not go on to plan.
        }
        else if (rule.job == WorkflowJob::Analyze) {
            quick_ = action == WorkflowAction::Quick;
        }
        state_ = rule.next.value_or(state_);
        return rule.job;
    }

    WorkflowJob Workflow::finish(JobOutcome outcome) {
        const bool succeeded = outcome == JobOutcome::Succeeded;
        cancelRequested_ = false;
        switch (state_) {
        case WorkflowState::Analyzing:
            return finishAnalysis(succeeded);
        case WorkflowState::Planning:
            state_ = succeeded ? WorkflowState::Reviewing : WorkflowState::Analyzed;
            quick_ = false; // A Quick run always stops at review.
            break;
        case WorkflowState::Executing:
        case WorkflowState::Paused:
            state_ = WorkflowState::Executed; // Even a partial run changed the layout.
            break;
        case WorkflowState::Trimming:
            state_ = WorkflowState::SsdReady;
            break;
        default:
            break; // No job was running.
        }
        return WorkflowJob::None;
    }

    WorkflowJob Workflow::finishAnalysis(bool succeeded) {
        if (!succeeded) {
            state_ = WorkflowState::NotAnalyzed;
            quick_ = false;
            return WorkflowJob::None;
        }
        state_ = quick_ ? WorkflowState::Planning : WorkflowState::Analyzed;
        return quick_ ? WorkflowJob::Plan : WorkflowJob::None;
    }

} // namespace icd::services
