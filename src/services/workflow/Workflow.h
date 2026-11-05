#pragma once

#include "core/model/DriveInfo.h"
#include "services/job/JobRunner.h"

#include <cstdint>

namespace icd::services {

    // Where the selected drive is in the workflow of plan.md section 11:
    // Analyze -> Choose strategy -> Build plan -> Review on map -> Execute.
    enum class WorkflowState : std::uint8_t {
        NoDrive, // Nothing selected.
        Unsupported, // Selected volume cannot be optimized (not NTFS).
        SsdReady, // SSD selected: TRIM is the only action.
        Trimming, // TRIM running.
        NotAnalyzed, // HDD selected, no analysis yet.
        Analyzing, // Analysis running.
        Analyzed, // Analysis done: choose a strategy and build a plan.
        Planning, // Plan being built.
        Reviewing, // Plan built and shown on the map; Execute is possible.
        Executing, // Plan being executed.
        Paused, // Execution paused between moves.
        Executed, // Execution ended; the layout changed, so the next step is a new analysis.
    };

    // Things the user can ask for; each maps to menu items and buttons.
    enum class WorkflowAction : std::uint8_t {
        SelectDrive, // Pick another row in the all-disks panel.
        RefreshDrives, // Reload the drive list.
        Analyze, // Start an analysis.
        Quick, // Analyze, then build a Fast plan, then stop at review.
        Trim, // TRIM an SSD.
        ChooseStrategy, // Change strategy or effort (drops a built plan).
        BuildPlan, // Build a plan.
        Review, // Show the plan on the map.
        Execute, // Run the reviewed plan (after confirmation).
        Pause, // Pause execution.
        Resume, // Continue a paused execution.
        Cancel, // Stop the running job.
        DefragmentFile, // Defragment the file selected on the map on its own (T128); runs like an execution.
        // Continue an execution that was cancelled or cut off by a crash, from its log (T112). Analyzes first when no
        // map is shown, since the live map needs one; runs like an execution.
        ResumeRun,
    };

    // Background job the caller must start after an action or a finished step.
    enum class WorkflowJob : std::uint8_t { None, Analyze, Plan, Execute, Trim };

    // What a selected drive allows, from its media, file system and storage setup.
    enum class DriveKind : std::uint8_t {
        None, // Nothing selected.
        Unsupported, // Not NTFS: nothing to do.
        Ssd, // TRIM only.
        Hdd, // The full workflow.
        AnalyzeOnly, // NTFS on an HDD whose setup blocks moves (core::moveBlocks()): analysis and plans, no Execute.
    };

    // The kind of drive (None for null).
    [[nodiscard]] DriveKind driveKind(const core::DriveInfo* drive);

    // The workflow state machine. It decides which actions are allowed and which job to start, but runs
    // nothing itself, so every transition is unit-tested and the UI only maps it to widgets. Used on one
    // thread (the UI thread).
    class Workflow {
    public:
        // Current state.
        [[nodiscard]] WorkflowState state() const { return state_; }

        // Whether a job is running (or paused), which locks drive selection.
        [[nodiscard]] bool busy() const;

        // Whether the current run is a Quick run (analysis followed by a Fast plan).
        [[nodiscard]] bool quick() const { return quick_; }

        // Whether the running or starting execution resumes an unfinished run (ResumeRun), not the reviewed plan.
        [[nodiscard]] bool resuming() const { return resuming_; }

        // Whether the selected drive has an unfinished run to resume; the caller looks for one off the UI thread and
        // says so here. Selecting a drive and ending an execution clear it until the caller looks again.
        void setResumable(bool resumable) { resumable_ = resumable; }

        // Whether Cancel was requested for the running job.
        [[nodiscard]] bool cancelRequested() const { return cancelRequested_; }

        // Whether an action is allowed now.
        [[nodiscard]] bool can(WorkflowAction action) const;

        // A new drive was selected; ignored while busy. Returns whether it was taken.
        bool selectDrive(DriveKind kind);

        // Performs an allowed action and returns the job to start; disallowed actions change nothing.
        WorkflowJob apply(WorkflowAction action);

        // The running job ended; returns a follow-up job (the plan of a Quick run) or None.
        WorkflowJob finish(JobOutcome outcome);

    private:
        // Ends an analysis; starts the plan of a Quick run or the resumed run that waited for it.
        WorkflowJob finishAnalysis(bool succeeded);

        // Applies ResumeRun: the run starts now when a map is shown, else after an analysis.
        WorkflowJob applyResume();

        WorkflowState state_ = WorkflowState::NoDrive; // Current state.
        bool quick_ = false; // Quick run in progress.
        bool cancelRequested_ = false; // Cancel asked for the running job.
        bool movesBlocked_ = false; // The selected drive is AnalyzeOnly: Execute is never allowed.
        bool resumable_ = false; // The selected drive has an unfinished run (setResumable()).
        bool resuming_ = false; // ResumeRun was applied: the analysis it waits for or the execution resumes the run.
    };

} // namespace icd::services
