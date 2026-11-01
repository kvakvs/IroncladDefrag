#pragma once

#include "core/job/StageClock.h"
#include "core/map/World.h"
#include "core/model/DriveInfo.h"
#include "services/job/JobRunner.h"
#include "services/trim/TrimJob.h"
#include "services/workflow/FakeResults.h"
#include "services/workflow/Workflow.h"

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
} // namespace icd::services

namespace icd::ui {

    class BottomPanel;
    class DrivesPanel;
    class JobEventBridge;
    class MapPanel;

    // Connects the workflow state machine to the window: routes menu and button commands to it, starts the
    // job each step needs, shows progress on the status line and results on the tabs, and enables every
    // command from the workflow state. In Phase 1 the jobs and results are fake; later phases swap in the
    // real services behind the same steps.
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

        // Asks the user to confirm execution of the reviewed plan.
        [[nodiscard]] bool confirmExecute();

        // Starts the job for a workflow step; None does nothing.
        void startJob(services::WorkflowJob job);

        // Clears results that a new job of this kind makes stale.
        void resetFor(services::WorkflowJob job);

        // Builds the (fake) job function of a step on drive.
        [[nodiscard]] services::JobFunction makeJob(services::WorkflowJob job, const core::DriveInfo& drive);

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

        // Fills the Plan tab after planning.
        void showPlan(const core::DriveInfo& drive);

        // Fills the Log tab and the drive row after an execution; the old figures are now stale.
        void showExecution(core::DriveInfo& drive);

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
        services::Workflow workflow_; // The state machine.
        std::shared_ptr<JobEventBridge> bridge_; // Delivers job events to the frame.
        std::optional<services::JobId> activeJob_; // Running job, if any.
        services::WorkflowJob activeKind_ = services::WorkflowJob::None; // Step the running job belongs to.
        std::shared_ptr<core::PauseGate> pause_; // Pause control of the running execution.
        std::optional<core::StageClock> clock_; // Timing of the running job.
        std::optional<core::DriveInfo> drive_; // Selected drive, copied so a refresh cannot change it mid-job.
        std::optional<services::AnalysisSummary> analysis_; // Last analysis of drive_.
        std::optional<services::PlanSummary> plan_; // Plan under review.
        std::shared_ptr<services::AnalysisOutput> analysisOutput_; // Where the running analysis leaves its map.
        std::shared_ptr<services::TrimOutput> trimOutput_; // Output lines of the running TRIM.
    };

} // namespace icd::ui
