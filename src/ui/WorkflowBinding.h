#pragma once

#include "core/job/RateMeter.h"
#include "core/job/StageClock.h"
#include "core/map/World.h"
#include "core/model/DriveInfo.h"
#include "services/job/JobRunner.h"
#include "services/trim/TrimJob.h"
#include "services/workflow/FakeResults.h"
#include "services/workflow/Workflow.h"
#include "ui/ExecutionView.h"
#include "ui/dialogs/ConfirmExecuteDialog.h"

#include <memory>
#include <optional>
#include <string>

class wxCommandEvent;
class wxFrame;
class wxThreadEvent;

namespace icd::core {
    class PauseGate;
}

namespace icd::services {
    class AppServices;
    struct AnalysisOutput;
    struct PlanOutcome;
    struct PlanOutput;
    struct PlanRequest;
} // namespace icd::services

namespace icd::ui {

    class BottomPanel;
    class DrivesPanel;
    class JobEventBridge;
    class MapPanel;
    struct ProgressDetails;

    // Connects the workflow state machine to the window: routes menu and button commands to it, starts the
    // job each step needs, shows progress on the status line and results on the tabs, and enables every
    // command from the workflow state. With the fake drive list the analysis is synthetic and execution simulated.
    class WorkflowBinding {
    public:
        // The widgets it drives; children of the frame, so they outlive the binding.
        struct Widgets {
            DrivesPanel& drives; // All-disks panel.
            MapPanel& map; // Map area.
            BottomPanel& bottom; // Tabs, buttons and status line.
        };

        // Binds the commands on frame; services must outlive the binding.
        WorkflowBinding(wxFrame& frame, services::AppServices& services, Widgets widgets);

        // Detaches the job event bridge.
        ~WorkflowBinding();

        WorkflowBinding(const WorkflowBinding&) = delete;
        WorkflowBinding& operator=(const WorkflowBinding&) = delete;

        // Cancels a running job and stops event delivery; call when the window closes.
        void shutdown();

        // Selects the drive with this root ("T", "T:" or the full root), as the --drive start option asks; unknown
        // roots are ignored.
        void selectDrive(const std::string& root);

    private:
        // Binds menu and button commands and job events on the frame.
        void bindEvents();

        // View menu and map keys: stubs until the map engine (Phase 2) owns them.
        void onViewCommand(wxCommandEvent& event);

        // Handles every workflow command; other ids are passed on.
        void onCommand(wxCommandEvent& event);

        // Carries out a user action.
        void perform(services::WorkflowAction action);

        // Side effects of an applied action (pause gate, cancel request, tabs).
        void afterAction(services::WorkflowAction action);

        // Asks the user to confirm execution of the reviewed plan; keeps the chosen options.
        [[nodiscard]] bool confirmExecute();

        // What the confirmation shows for the reviewed plan at the chosen level.
        [[nodiscard]] ExecuteConfirmation confirmation(const services::PlanSummary& plan) const;

        // Whether an action may start: actions that read volumes or move files need administrator rights, so
        // without them the user is offered a restart as administrator (which closes this window).
        [[nodiscard]] bool elevationReady(services::WorkflowAction action);

        // Starts the job for a workflow step; None does nothing.
        void startJob(services::WorkflowJob job);

        // Clears results that a new job of this kind makes stale.
        void resetFor(services::WorkflowJob job);

        // Builds the job function of a step on drive: the real one, else the fake stand-in.
        [[nodiscard]] services::JobFunction makeJob(services::WorkflowJob job, const core::DriveInfo& drive);

        // The real job of a step, or null where only a fake one exists (planning without a map, the fake drives'
        // execution).
        [[nodiscard]] services::JobFunction makeRealJob(services::WorkflowJob job, const core::DriveInfo& drive);

        // The fake parallel job of a step, with step-specific stage names (the UI shell and the fake drive list).
        [[nodiscard]] services::JobFunction makeFakeJob(services::WorkflowJob job, const core::DriveInfo& drive);

        // Shows throttled progress of the active job.
        void onJobProgress(wxThreadEvent& event);

        // Takes the end of the active job.
        void onJobFinished(wxThreadEvent& event);

        // Feeds a job's end to the workflow, shows results and starts a follow-up job.
        void completeJob(services::WorkflowJob job, const services::JobResult& result);

        // Fills the tabs after a successful step.
        void showResults(services::WorkflowJob job);

        // Fills the Summary tab and the drive row after an analysis.
        void showAnalysis(core::DriveInfo& drive);

        // Fills the Plan tab and the map overlays after planning.
        void showPlan(const core::DriveInfo& drive);

        // Forgets the plan under review and its map overlays, with a hint for the Plan tab.
        void dropPlan(const char* hint);

        // Whether an action can run now: the workflow allows it, and a plan needs a strategy without problems.
        [[nodiscard]] bool allowed(services::WorkflowAction action) const;

        // A job that plans the shown map with the Strategy tab's script, or null without a map to plan.
        [[nodiscard]] services::JobFunction makePlanJob(const core::DriveInfo& drive);

        // A job that executes the reviewed plan's chosen level, or null when there is no real plan (fake drives).
        [[nodiscard]] services::JobFunction makeExecuteJob(const core::DriveInfo& drive);

        // The Progress tab fields of a progress report (elapsed, estimate, workers, and the execution's own fields).
        [[nodiscard]] ProgressDetails progressDetails(const core::Progress& progress, const core::StageClock& clock,
                                                      std::chrono::steady_clock::time_point now,
                                                      std::optional<double>& fraction);

        // The share of the pool's workers busy over the last seconds.
        [[nodiscard]] std::string workersText(std::chrono::steady_clock::time_point now);

        // Fills the Log tab and the drive row after an execution; the old figures are now stale.
        void showExecution(core::DriveInfo& drive);

        // An execution ended however it ended: its summary goes into the Log tab, the stale Summary and plan are
        // dropped. Returns the status line (the run's own, or `status` without a real run).
        [[nodiscard]] std::string executionEnded(const std::string& status);

        // Moves TRIM output collected so far into the Log tab.
        void takeTrimOutput();

        // Shows the newest map a running analysis has left (bitmap first, then partial scans).
        void takeAnalysisMap();

        // Clears the map after a cancelled or failed analysis, so a half-read volume never looks analysed.
        void dropPartialMap(services::WorkflowJob job);

        // Status line text after a plan was built.
        [[nodiscard]] std::string planReadyText() const;

        // Status line text after a successful step.
        [[nodiscard]] std::string successText(services::WorkflowJob job, const services::JobResult& result) const;

        // A different drive was selected in the all-disks panel.
        void onDriveSelected();

        // The user changed preset or effort on the Strategy tab.
        void onStrategyChanged();

        // Shows the strategy last chosen for drive_, or the default when none is remembered.
        void restoreStrategy();

        // Remembers the Strategy tab's choice for drive_ in the settings file, for the next selection or run.
        void rememberStrategy();

        // Reloads the drive list.
        void refreshDrives();

        // Brings every widget in line with the workflow state.
        void refresh();

        // Enables each command from the workflow state.
        void refreshCommands();

        // Sets the map caption and pattern from the state.
        void refreshMap();

        // Whether a command can run now.
        [[nodiscard]] bool commandEnabled(int id) const;

        // Aspect of the screen the window is on, for laying out a new map (plan.md 7.1).
        [[nodiscard]] core::ScreenAspect screenAspect() const;

        // "D: Data", or empty without a drive.
        [[nodiscard]] std::string driveLabel() const;

        wxFrame& frame_; // Window whose commands are bound.
        services::AppServices& services_; // Job runner, caps, drive provider.
        Widgets widgets_; // Panels to update.
        ExecutionView execution_; // Follows a real execution: Progress fields, Log rows, final status.
        ExecuteChoices executeChoices_; // Priority and throughput limit from the last confirmation.
        core::RateMeter busy_; // Worker busy time per second, for the Progress tab's utilisation.
        services::Workflow workflow_; // The state machine.
        std::shared_ptr<JobEventBridge> bridge_; // Delivers job events to the frame.
        std::optional<services::JobId> activeJob_; // Running job, if any.
        services::WorkflowJob activeKind_ = services::WorkflowJob::None; // Step the running job belongs to.
        std::shared_ptr<core::PauseGate> pause_; // Pause control of the running execution.
        std::optional<core::StageClock> clock_; // Timing of the running job.
        std::optional<core::DriveInfo> drive_; // Selected drive, copied so a refresh cannot change it mid-job.
        std::optional<services::AnalysisSummary> analysis_; // Last analysis of drive_.
        std::optional<services::PlanSummary> plan_; // Plan under review.
        std::shared_ptr<services::PlanOutput> planOutput_; // Where the running plan job leaves its outcome.
        std::shared_ptr<const services::PlanOutcome> planOutcome_; // The reviewed plan's levels, for Execute.
        std::shared_ptr<const services::PlanRequest> planRequest_; // What it was planned from, for Execute.
        std::shared_ptr<services::AnalysisOutput> analysisOutput_; // Where the running analysis leaves its map.
        std::shared_ptr<services::TrimOutput> trimOutput_; // Output lines of the running TRIM.
    };

} // namespace icd::ui
