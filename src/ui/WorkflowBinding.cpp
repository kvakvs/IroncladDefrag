#include "ui/WorkflowBinding.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/DriveText.h"
#include "core/format/Duration.h"
#include "core/job/PauseGate.h"
#include "core/log/Log.h"
#include "core/map/MapData.h"
#include "core/strategy/Presets.h"
#include "services/AppServices.h"
#include "services/Elevation.h"
#include "services/exec/ThreadPool.h"
#include "services/execute/ExecuteJob.h"
#include "services/execute/FileDefrag.h"
#include "services/job/FakeJob.h"
#include "services/plan/PlanJob.h"
#include "services/scan/AnalysisJob.h"
#include "services/settings/Settings.h"
#include "services/workflow/SyntheticAnalysis.h"
#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/MainMenu.h"
#include "ui/bridge/JobEvents.h"
#include "ui/dialogs/ScheduleDialog.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/dialogs/VssWarning.h"
#include "ui/panels/BottomPanel.h"
#include "ui/panels/DrivesPanel.h"
#include "ui/panels/MapPanel.h"
#include "ui/panels/PlanPage.h"
#include "ui/panels/StrategyPage.h"
#include "ui/panels/SummaryPage.h"
#include "ui/tabs/LogTab.h"
#include "ui/tabs/ProgressTab.h"

#include <wx/display.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <iterator>

namespace icd::ui {

    namespace {

        using services::WorkflowAction;
        using services::WorkflowJob;
        using services::WorkflowState;

        // A command id and the workflow action it triggers.
        struct CommandAction {
            int id = 0; // Menu item and button id.
            WorkflowAction action = WorkflowAction::Analyze; // What it asks the workflow for.
        };

        constexpr std::array commandActions{
            CommandAction{ID_ANALYZE, WorkflowAction::Analyze},
            CommandAction{ID_QUICK, WorkflowAction::Quick},
            CommandAction{ID_TRIM, WorkflowAction::Trim},
            CommandAction{ID_CHOOSE_STRATEGY, WorkflowAction::ChooseStrategy},
            CommandAction{ID_BUILD_PLAN, WorkflowAction::BuildPlan},
            CommandAction{ID_REVIEW_PLAN, WorkflowAction::Review},
            CommandAction{ID_EXECUTE, WorkflowAction::Execute},
            CommandAction{ID_PAUSE, WorkflowAction::Pause},
            CommandAction{ID_CANCEL, WorkflowAction::Cancel},
            CommandAction{ID_REFRESH_DRIVES, WorkflowAction::RefreshDrives},
            CommandAction{ID_DEFRAG_FILE, WorkflowAction::DefragmentFile},
            CommandAction{ID_RESUME_RUN, WorkflowAction::ResumeRun},
        };

        // The action of a command in the current state (Pause becomes Resume while paused), if it has one.
        std::optional<WorkflowAction> actionFor(int id, WorkflowState state) {
            for (const CommandAction& entry : commandActions) {
                if (entry.id != id) {
                    continue;
                }
                const bool resume = entry.action == WorkflowAction::Pause && state == WorkflowState::Paused;
                return resume ? WorkflowAction::Resume : entry.action;
            }
            return std::nullopt;
        }

        // Names of a workflow step, for the status line and the log.
        struct StepText {
            const char* title = ""; // "Analysis", for results.
            const char* verb = ""; // "Analyzing", for progress.
            const char* serialStage = ""; // First fake stage.
            const char* parallelStage = ""; // Second fake stage.
            std::size_t chunksPerWorker = 0; // Fake work size, so a step takes about as long on any machine.
        };

        // Texts indexed by WorkflowJob (None has none).
        constexpr std::array<StepText, 5> stepTexts{{
            {},
            {"Analysis", "Analyzing", "Reading volume bitmap", "Reading MFT", 40},
            {"Planning", "Building plan for", "Classifying files", "Planning moves", 30},
            {"Execution", "Executing plan on", "Checking the plan", "Moving files", 80},
            {"TRIM", "Trimming", "Preparing TRIM", "Trimming free space", 30},
        }};

        // Texts of a step.
        const StepText& textOf(WorkflowJob job) { return stepTexts.at(static_cast<std::size_t>(job)); }

        // Hint for the Summary tab when a drive of this kind is selected but not analysed.
        const char* summaryHint(services::DriveKind kind) {
            switch (kind) {
            case services::DriveKind::AnalyzeOnly:
                return "Analyze the drive to see its layout. Moves are blocked on this drive (see the badge in the "
                       "drive list); plans can still be built and reviewed.";
            case services::DriveKind::Hdd:
                return "Analyze the drive to see fragmentation, free-space runs and per-class counts and bytes.";
            case services::DriveKind::Ssd:
                return "SSDs are not analysed or defragmented; use TRIM.";
            case services::DriveKind::Unsupported:
                return "Only NTFS volumes can be analysed.";
            case services::DriveKind::None:
                break;
            }
            return "Select a drive in the list above.";
        }

        // A fake file path for item index of an execution, for the "current file" field.
        std::string fakeFile(const std::string& root, std::size_t index) {
            return std::format("{}Data\\Photos\\{}\\IMG_{:05}.jpg", root, 2015 + index % 10, index);
        }

        // A drive root without its trailing backslash, in upper case ("t:\" becomes "T:"), for --drive.
        std::string rootKey(std::string root) {
            while (!root.empty() && (root.back() == '\\' || root.back() == '/')) {
                root.pop_back();
            }
            if (root.size() == 1) {
                root += ':';
            }
            std::ranges::transform(root, root.begin(), [](char c) {
                return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            });
            return root;
        }

        // Whether an action reads volumes or moves files, which needs administrator rights (plan.md 4).
        bool needsAdministrator(WorkflowAction action) {
            return action == WorkflowAction::Analyze || action == WorkflowAction::Quick ||
                action == WorkflowAction::Trim || action == WorkflowAction::Execute ||
                action == WorkflowAction::DefragmentFile || action == WorkflowAction::ResumeRun;
        }

        // Whether a warning of the Plan tab belongs to a level: general ones and the level's own ("Fast: ...").
        bool warningFor(const std::string& warning, services::EffortLevel level) {
            constexpr std::array levels{services::EffortLevel::Fast, services::EffortLevel::Moderate,
                                        services::EffortLevel::Maximum};
            return std::ranges::none_of(levels, [&warning, level](services::EffortLevel other) {
                return other != level && warning.starts_with(std::string(services::effortName(other)) + ":");
            });
        }

        // Seconds with one decimal, for result texts.
        std::string seconds(const services::JobResult& result) {
            return std::format("{:.1f} s", static_cast<double>(result.elapsed.count()) / 1000.0);
        }

        // The status line after a TRIM: how much Windows trimmed when it recorded it, since a retrim of a fast SSD
        // ends in about a second and would otherwise look like nothing happened.
        std::string trimDoneText(const std::string& drive, const services::JobResult& result,
                                 const std::optional<services::TrimResult>& trim) {
            const std::string done = std::format("TRIM of {} finished in {}", drive, seconds(result));
            if (!trim || !trim->bytesTrimmed) {
                return done + " (Windows output in the Log tab)";
            }
            if (*trim->bytesTrimmed == 0) {
                return done + ": Windows found no free space that needed trimming";
            }
            return std::format("{}: Windows trimmed {} of free space", done, core::formatBytes(*trim->bytesTrimmed));
        }

    } // namespace

    WorkflowBinding::WorkflowBinding(wxFrame& frame, services::AppServices& services, Widgets widgets) :
        frame_(frame), services_(services), widgets_(widgets), execution_(widgets.bottom),
        strategyActions_(frame, widgets.bottom.strategy(), services.strategies()), live_(widgets.map, services.pool()),
        resumeCheck_(frame, services.pool(),
                     [this](std::optional<services::UnfinishedRun> run) { onUnfinishedRun(std::move(run)); }),
        bridge_(std::make_shared<JobEventBridge>(&frame)) {
        bindEvents();
        widgets_.drives.setSelectionHandler([this](std::optional<std::size_t>) { onDriveSelected(); });
        widgets_.drives.setContextMenuHandler([this] { showDriveMenu(); });
        StrategyPage& strategy = widgets_.bottom.strategy();
        strategy.setChangeHandler([this] { onStrategyChanged(); });
        strategy.setCommandHandler([this](StrategyCommand command) {
            if (strategyActions_.run(command)) {
                onStrategyChanged();
            }
        });
        strategyActions_.showLibrary({});
        widgets_.bottom.plan().setVssHandler([this] { onVssOptionChanged(); });
        widgets_.map.setSelectionHandler([this] { onFileSelected(); });
        widgets_.drives.setDrives(services_.drives().listDrives());
        refresh();
    }

    WorkflowBinding::~WorkflowBinding() { bridge_->detach(); }

    void WorkflowBinding::shutdown() {
        if (activeJob_) {
            services_.jobs().cancel(*activeJob_); // AppServices waits for it when the app exits.
        }
        bridge_->detach();
    }

    void WorkflowBinding::bindEvents() {
        for (const CommandAction& entry : commandActions) {
            frame_.Bind(wxEVT_MENU, &WorkflowBinding::onCommand, this, entry.id);
            frame_.Bind(wxEVT_BUTTON, &WorkflowBinding::onCommand, this, entry.id);
        }
        frame_.Bind(wxEVT_MENU, &WorkflowBinding::onViewCommand, this, ID_COLOR_CLASS, ID_ZOOM_CLUSTER);
        frame_.Bind(wxEVT_MENU, &WorkflowBinding::onViewCommand, this, ID_GOTO_FILE, ID_PREVIOUS_FRAGMENT);
        frame_.Bind(wxEVT_MENU, &WorkflowBinding::onScheduleCommand, this, ID_WINDOWS_SCHEDULE);
        frame_.Bind(wxEVT_MENU, &WorkflowBinding::onSettingsCommand, this, ID_RESOURCES);
        frame_.Bind(wxEVT_MENU, &WorkflowBinding::onSettingsCommand, this, wxID_PREFERENCES);
        frame_.Bind(jobProgressEvent(), &WorkflowBinding::onJobProgress, this);
        frame_.Bind(jobFinishedEvent(), &WorkflowBinding::onJobFinished, this);
    }

    void WorkflowBinding::onFileSelected() {
        const std::optional<core::FileIndex> file = widgets_.map.selectedFile();
        if (file && file != selectedFile_ && !workflow_.busy()) {
            widgets_.bottom.showTab(BottomTab::File); // A newly picked file: show what it is. Not during a job.
        }
        selectedFile_ = file;
        refreshCommands(); // Defragment this file follows the selection.
    }

    void WorkflowBinding::onViewCommand(wxCommandEvent& event) {
        const int id = event.GetId();
        if (!commandEnabled(id)) {
            return; // Map keys arrive even when the menu item is disabled.
        }
        if (id == ID_SHOW_LEGEND) {
            widgets_.bottom.showTab(BottomTab::MapColors);
            return;
        }
        if (!widgets_.map.handleViewCommand(id)) {
            return;
        }
        // The panel owns the display state; the check marks follow it however the command arrived (menu, toolbar,
        // accelerator or map key).
        wxMenuBar& menu = *frame_.GetMenuBar();
        menu.Check(ID_SHOW_MINIMAP, widgets_.map.minimapShown());
        menu.Check(widgets_.map.colorModeCommand(), true);
    }

    void WorkflowBinding::onCommand(wxCommandEvent& event) {
        const auto action = actionFor(event.GetId(), workflow_.state());
        if (!action) {
            event.Skip();
            return;
        }
        perform(*action);
    }

    void WorkflowBinding::onSettingsCommand(wxCommandEvent& event) {
        if (!commandEnabled(event.GetId())) {
            return; // The accelerator arrives even while a job runs.
        }
        const SettingsPage page = event.GetId() == ID_RESOURCES ? SettingsPage::Resources : SettingsPage::Planning;
        wxString status;
        if (editSettings(&frame_, services_, page, status)) {
            refreshResources();
            widgets_.bottom.setStatus(status);
        }
    }

    void WorkflowBinding::refreshResources() {
        services_.refreshCaps();
        widgets_.bottom.progress().setResources(
            wxString::FromUTF8(core::describeCaps(services_.resources(), services_.caps())));
    }

    bool WorkflowBinding::scheduleChangeable() const {
        return drive_ && !workflow_.busy() && core::canOptimize(*drive_) &&
            drive_->badges.scheduledOptimization.has_value();
    }

    void WorkflowBinding::onScheduleCommand(wxCommandEvent& /*event*/) {
        if (!scheduleChangeable() || !drive_) {
            return;
        }
        core::DriveInfo& drive = *drive_;
        const bool scheduled = drive.badges.scheduledOptimization.value_or(false);
        ScheduleDialog dialog(&frame_,
                              {.drive = driveLabel(), .scheduled = scheduled, .simulated = services_.fakeDrives()});
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        const std::string error = services_.drives().setScheduledOptimization(drive, !scheduled);
        if (!error.empty()) {
            core::logging::warning("windows schedule not changed", {{"drive", drive.root}, {"error", error}});
            wxMessageBox(wxString::FromUTF8("The Windows schedule was not changed: " + error),
                         "Windows scheduled optimization", wxOK | wxICON_WARNING, &frame_);
            return;
        }
        takeScheduleBadge(drive);
        widgets_.bottom.setStatus(wxString::FromUTF8(
            std::format("{} {} Windows' scheduled optimization", driveLabel(),
                        drive.badges.scheduledOptimization.value_or(false) ? "is back on" : "was taken out of")));
        refresh();
    }

    void WorkflowBinding::takeScheduleBadge(core::DriveInfo& drive) {
        widgets_.drives.setDrives(services_.drives().listDrives());
        for (const core::DriveInfo& listed : widgets_.drives.drives()) {
            if (listed.root == drive.root) {
                drive.badges.scheduledOptimization = listed.badges.scheduledOptimization;
            }
        }
        widgets_.drives.updateDrive(drive); // Keep this session's analysis results in the row.
    }

    void WorkflowBinding::perform(WorkflowAction action) {
        if (action == WorkflowAction::RefreshDrives) {
            refreshDrives();
            return;
        }
        if (action == WorkflowAction::ChooseStrategy) {
            widgets_.bottom.showTab(BottomTab::Strategy); // The change itself happens on the tab.
            return;
        }
        if (!allowed(action) || !elevationReady(action) || !confirmed(action)) {
            return;
        }
        core::logging::info("workflow action",
                            {{"action", static_cast<int>(action)}, {"state", static_cast<int>(workflow_.state())}});
        const WorkflowJob job = workflow_.apply(action);
        afterAction(action);
        startJob(job);
        refresh();
    }

    void WorkflowBinding::afterAction(WorkflowAction action) {
        BottomPanel& bottom = widgets_.bottom;
        switch (action) {
        case WorkflowAction::Quick:
            bottom.strategy().reset(); // Quick always uses the default preset at the Fast level.
            break;
        case WorkflowAction::Review:
            widgets_.map.setColorMode(core::ColorMode::PlanPreview);
            frame_.GetMenuBar()->Check(widgets_.map.colorModeCommand(), true);
            bottom.showTab(BottomTab::Plan);
            bottom.setStatus(wxString::FromUTF8("Reviewing the plan for " + driveLabel() + " on the map"));
            break;
        case WorkflowAction::Pause:
            if (pause_) {
                pause_->pause();
            }
            bottom.setStatus("Execution paused between moves");
            break;
        case WorkflowAction::Resume:
            if (pause_) {
                pause_->resume();
            }
            execution_.resumed();
            eta_.reset(); // The estimates made while paused were too long.
            break;
        case WorkflowAction::Cancel:
            if (activeJob_) {
                services_.jobs().cancel(*activeJob_);
            }
            bottom.setStatus("Cancelling...");
            break;
        default:
            break;
        }
    }

    bool WorkflowBinding::confirmExecute() {
        if (!plan_ || !confirmShadowCopies(*plan_)) {
            return false;
        }
        ConfirmExecuteDialog dialog(&frame_, confirmation(*plan_));
        if (dialog.ShowModal() != wxID_OK) {
            return false;
        }
        executeChoices_ = dialog.choices();
        core::logging::info(
            "execution confirmed",
            {{"lowPriority", executeChoices_.lowPriority}, {"limitMbPerSecond", executeChoices_.limitMbPerSecond}});
        return true;
    }

    bool WorkflowBinding::confirmShadowCopies(const services::PlanSummary& plan) {
        const services::PlanLevel& level =
            plan.levels.at(static_cast<std::size_t>(widgets_.bottom.strategy().effort()));
        if (!plan.shadowCopies || plan.unalignedBlocked || level.unalignedMoves == 0) {
            return true;
        }
        VssWarningDialog dialog(&frame_,
                                {.drive = driveLabel(),
                                 .unalignedMoves = level.unalignedMoves,
                                 .moves = level.moves,
                                 .growthBytes = level.shadowGrowthBytes});
        const int choice = dialog.ShowModal();
        core::logging::info("vss warning", {{"unaligned", level.unalignedMoves}, {"choice", choice}});
        if (choice == VssWarningDialog::blockAndReplan) {
            widgets_.bottom.plan().setBlockUnaligned(true);
            onVssOptionChanged();
            frame_.CallAfter([this] { perform(WorkflowAction::BuildPlan); }); // After this Execute has returned.
            return false;
        }
        return choice == wxID_OK;
    }

    void WorkflowBinding::onVssOptionChanged() {
        if (!workflow_.can(WorkflowAction::ChooseStrategy)) {
            return;
        }
        const bool hadPlan = workflow_.state() == WorkflowState::Reviewing;
        workflow_.apply(WorkflowAction::ChooseStrategy);
        if (hadPlan) {
            dropPlan("The VSS option changed: build the plan again.");
            widgets_.bottom.setStatus("VSS option changed; the previous plan was dropped");
        }
        refresh();
    }

    ExecuteConfirmation WorkflowBinding::confirmation(const services::PlanSummary& plan) const {
        const StrategyPage& strategy = widgets_.bottom.strategy();
        ExecuteConfirmation what;
        what.drive = driveLabel();
        what.strategy = strategy.script().name;
        what.level = plan.levels.at(static_cast<std::size_t>(strategy.effort()));
        std::ranges::copy_if(plan.warnings, std::back_inserter(what.warnings),
                             [&what](const std::string& w) { return warningFor(w, what.level.level); });
        what.simulated = services_.fakeDrives() || !planOutcome_;
        if (unfinished_) {
            what.warnings.push_back("This drive has an interrupted run; once this plan runs, it is no longer offered "
                                    "for resuming (this plan already takes the drive as it is now).");
        }
        return what;
    }

    bool WorkflowBinding::elevationReady(WorkflowAction action) {
        if (!needsAdministrator(action) || services_.fakeDrives() || services::runningElevated()) {
            return true;
        }
        wxMessageDialog dialog(&frame_,
                               "Reading volumes and moving files need administrator rights, and IroncladDefrag was "
                               "started without them.\n\nRestart it as administrator? This window closes; the drive "
                               "stays selected, but the analysis and plan must be made again.",
                               "Administrator rights needed", wxYES_NO | wxICON_WARNING);
        dialog.SetYesNoLabels("&Restart as administrator", "&Not now");
        if (dialog.ShowModal() != wxID_YES) {
            return false;
        }
        const std::string arguments = drive_ ? "--drive " + rootKey(drive_->root) : std::string();
        if (services::restartElevated(arguments)) {
            frame_.Close();
        }
        else {
            widgets_.bottom.setStatus("Not restarted: administrator rights were not granted");
        }
        return false;
    }

    void WorkflowBinding::startJob(WorkflowJob job) {
        if (job == WorkflowJob::None || !drive_) {
            return;
        }
        const core::DriveInfo& drive = *drive_;
        resetFor(job);
        refreshResources(); // The job gets the caps for what the machine offers now.
        const StepText& text = textOf(job);
        clock_.emplace(std::chrono::steady_clock::now());
        activeKind_ = job;
        activeJob_ = services_.jobs().start(std::format("{} {}", text.title, drive.root), makeJob(job, drive), bridge_);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setStatus(wxString::FromUTF8(std::format("{} {}...", text.verb, driveLabel())));
        bottom.setProgress(0.0);
        bottom.progress().show({});
        busy_.reset();
        eta_.reset();
        etaStage_.clear();
        bottom.showTab(BottomTab::Progress);
    }

    void WorkflowBinding::resetFor(WorkflowJob job) {
        runKind_ = RunKind::Plan; // Set again when this job is a single-file run or a resume.
        BottomPanel& bottom = widgets_.bottom;
        if (job != WorkflowJob::Execute) {
            live_.stop(); // A new analysis or plan replaces what the live map follows.
        }
        if (job == WorkflowJob::Analyze) {
            analysis_.reset();
            bottom.summary().clear("Analysis running...");
        }
        if (job == WorkflowJob::Analyze || job == WorkflowJob::Plan) {
            dropPlan(job == WorkflowJob::Plan ? "Building the plan..." : "Build a plan after the analysis.");
        }
        if (job == WorkflowJob::Execute) {
            bottom.log().clear();
            pause_ = std::make_shared<core::PauseGate>();
            widgets_.map.setPlanOverlays(nullptr, nullptr); // They show the layout before the moves.
        }
        if (job == WorkflowJob::Trim) {
            bottom.log().clear();
        }
    }

    services::JobFunction WorkflowBinding::makeJob(WorkflowJob job, const core::DriveInfo& drive) {
        if (auto real = makeRealJob(job, drive); real) {
            return real;
        }
        return makeFakeJob(job, drive);
    }

    services::JobFunction WorkflowBinding::makeRealJob(WorkflowJob job, const core::DriveInfo& drive) {
        switch (job) {
        case WorkflowJob::Analyze:
            analysisOutput_ = std::make_shared<services::AnalysisOutput>();
            if (services_.fakeDrives()) {
                return services::makeSyntheticAnalysis(drive, screenAspect(), analysisOutput_);
            }
            analysisOutput_->previous = widgets_.map.currentMap(); // Used only when it shows the same volume.
            return services::makeAnalysisJob(drive, screenAspect(), analysisOutput_,
                                             services_.caps().memoryBudgetBytes);
        case WorkflowJob::Trim:
            trimOutput_ = std::make_shared<services::TrimOutput>();
            trimResult_.reset();
            return services::makeTrimJob(drive.root, drive.layout.volumeName, trimOutput_);
        case WorkflowJob::Plan:
            return makePlanJob(drive);
        case WorkflowJob::Execute:
            return makeExecuteJob(drive);
        case WorkflowJob::None:
            break;
        }
        return {};
    }

    services::JobFunction WorkflowBinding::makeFakeJob(WorkflowJob job, const core::DriveInfo& drive) {
        const StepText& text = textOf(job);
        services::FakeJobOptions options;
        options.chunks = text.chunksPerWorker * services_.caps().workerThreads;
        options.spinIterations = 20000;
        options.sleepPerChunk = std::chrono::milliseconds(25);
        options.serialStage = text.serialStage;
        options.parallelStage = text.parallelStage;
        if (job == WorkflowJob::Plan) {
            options.parallelStage += std::format(" ({})", services::effortName(widgets_.bottom.strategy().effort()));
        }
        if (job == WorkflowJob::Execute) {
            options.itemLabel = [root = drive.root](std::size_t index) { return fakeFile(root, index); };
            options.pause = pause_;
        }
        return services::makeFakeParallelJob(std::move(options));
    }

    services::JobFunction WorkflowBinding::makePlanJob(const core::DriveInfo& drive) {
        const std::shared_ptr<const core::MapData> map = widgets_.map.currentMap();
        if (map == nullptr) {
            return {}; // The UI shell without an analysed map plans nothing real.
        }
        // The snapshot shares the map's lifetime, so the job keeps the analysed volume alive without a copy.
        std::shared_ptr<const core::Snapshot> snapshot(map, &map->snapshot());
        const auto classes = map->fileClasses();
        planOutput_ = std::make_shared<services::PlanOutput>();
        services::PlanRequest request = services::makePlanRequest(
            drive, std::move(snapshot), {classes.begin(), classes.end()}, widgets_.bottom.strategy().script());
        request.blockUnaligned = widgets_.bottom.plan().blockUnaligned();
        request.fastBudgetMultiplier = services_.settings().current().fastBudgetMultiplier;
        return services::makePlanJob(std::move(request), planOutput_);
    }

    services::JobFunction WorkflowBinding::makeFileDefragJob(const core::DriveInfo& drive) {
        const std::shared_ptr<const core::MapData> map = widgets_.map.currentMap();
        const core::FileIndex file = *std::exchange(fileToDefrag_, std::nullopt);
        if (map == nullptr) {
            return {};
        }
        services::FileDefragRequest request;
        request.drive = drive;
        request.snapshot = std::shared_ptr<const core::Snapshot>(map, &map->snapshot()); // Lives with the map.
        request.file = file;
        request.now = services::fileTimeNow();
        request.options.lowPriority = executeChoices_.lowPriority;
        request.options.throughputLimit = executeChoices_.limitMbPerSecond << 20U;
        runKind_ = RunKind::File;
        services::JobFunction job = execution_.startFile(std::move(request), pause_);
        live_.follow(execution_.feed());
        return job;
    }

    services::JobFunction WorkflowBinding::makeExecuteJob(const core::DriveInfo& drive) {
        if (fileToDefrag_) {
            return makeFileDefragJob(drive);
        }
        if (workflow_.resuming()) {
            return makeResumeJob(drive);
        }
        if (services_.fakeDrives() || !planOutcome_ || !planRequest_) {
            return {}; // The fake drive list simulates execution.
        }
        services::ExecuteJobRequest request;
        request.drive = drive;
        request.outcome = planOutcome_;
        request.plan = planRequest_;
        request.level = static_cast<core::strategy::Effort>(widgets_.bottom.strategy().effort());
        request.options.lowPriority = executeChoices_.lowPriority;
        request.options.throughputLimit = executeChoices_.limitMbPerSecond << 20U;
        services::JobFunction job = execution_.start(std::move(request), pause_);
        live_.follow(execution_.feed());
        return job;
    }

    services::JobFunction WorkflowBinding::makeResumeJob(const core::DriveInfo& drive) {
        if (!unfinished_) {
            return {}; // Not reached: the workflow resumes only once a run was found.
        }
        services::ResumeJobRequest request;
        request.drive = drive;
        request.log = unfinished_->log;
        request.options.lowPriority = executeChoices_.lowPriority;
        request.options.throughputLimit = executeChoices_.limitMbPerSecond << 20U;
        runKind_ = RunKind::Resume;
        services::JobFunction job = execution_.startResume(std::move(request), pause_);
        live_.follow(execution_.feed());
        return job;
    }

    void WorkflowBinding::lookForUnfinishedRun(bool announce) {
        resumeCheck_.forget();
        unfinished_.reset();
        workflow_.setResumable(false);
        announceUnfinished_ = announce;
        const services::DriveKind kind = services::driveKind(drive_ ? &*drive_ : nullptr);
        if (drive_ && kind == services::DriveKind::Hdd && !services_.fakeDrives() &&
            !drive_->layout.volumeName.empty()) {
            resumeCheck_.look(drive_->layout.volumeName);
        }
    }

    void WorkflowBinding::onUnfinishedRun(std::optional<services::UnfinishedRun> run) {
        unfinished_ = std::move(run);
        workflow_.setResumable(unfinished_.has_value());
        if (unfinished_) {
            core::logging::info("unfinished run found",
                                {{"log", unfinished_->log.filename().string()},
                                 {"left", unfinished_->movesLeft},
                                 {"moves", unfinished_->moves}});
            if (announceUnfinished_ && !workflow_.busy()) {
                widgets_.bottom.setStatus(wxString::FromUTF8(unfinishedText(*unfinished_)));
            }
        }
        announceUnfinished_ = false;
        refresh();
    }

    std::string WorkflowBinding::unfinishedText(const services::UnfinishedRun& run) const {
        return std::format("{}: {} run has {} of {} moves left ({}); Resume run continues it", driveLabel(),
                           run.cancelled ? "a cancelled" : "an interrupted", core::formatCount(run.movesLeft),
                           core::formatCount(run.moves), core::formatBytes(run.bytesLeft));
    }

    std::string WorkflowBinding::strategyName(const std::string& id) const {
        const core::strategy::ZoneScript* script = services_.strategies().find(id);
        return script != nullptr ? script->name : id;
    }

    bool WorkflowBinding::confirmResume() {
        if (!unfinished_) {
            return false;
        }
        const ResumeConfirmation what{.drive = driveLabel(),
                                      .strategy = strategyName(unfinished_->strategy),
                                      .run = *unfinished_,
                                      .analyzeFirst = workflow_.state() == WorkflowState::NotAnalyzed};
        ConfirmExecuteDialog dialog(&frame_, what);
        if (dialog.ShowModal() != wxID_OK) {
            return false;
        }
        executeChoices_ = dialog.choices();
        core::logging::info("resume confirmed",
                            {{"log", unfinished_->log.filename().string()},
                             {"lowPriority", executeChoices_.lowPriority},
                             {"limitMbPerSecond", executeChoices_.limitMbPerSecond}});
        return true;
    }

    void WorkflowBinding::onJobProgress(wxThreadEvent& event) {
        const auto payload = event.GetPayload<JobProgressPayload>();
        if (payload.id != activeJob_ || !clock_ || workflow_.state() == WorkflowState::Paused) {
            return; // A late event from an earlier job, or one that raced the pause.
        }
        const core::Progress& progress = payload.progress;
        const auto now = std::chrono::steady_clock::now();
        clock_->update(progress, now);
        std::optional<double> fraction = progress.fraction();
        const ProgressDetails details = progressDetails(progress, *clock_, now, fraction);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setStatus(wxString::FromUTF8(std::format("{} {}: {}, {}", textOf(activeKind_).verb, driveLabel(),
                                                        progress.stage, details.done.utf8_string())));
        bottom.setProgress(fraction);
        bottom.progress().show(details);
        takeTrimOutput();
        takeAnalysisMap();
    }

    ProgressDetails WorkflowBinding::progressDetails(const core::Progress& progress, const core::StageClock& clock,
                                                     std::chrono::steady_clock::time_point now,
                                                     std::optional<double>& fraction) {
        std::string count = core::formatCount(progress.done);
        if (progress.total != 0) {
            count += " of " + core::formatCount(progress.total);
        }
        std::optional<std::chrono::seconds> remaining = clock.remaining(now);
        ProgressDetails details{.stage = wxString::FromUTF8(progress.stage),
                                .done = wxString::FromUTF8(count),
                                .elapsed = wxString::FromUTF8(core::formatDuration(clock.elapsed(now))),
                                .eta = {},
                                .throughput = {},
                                .workers = wxString::FromUTF8(workersText(now)),
                                .currentFile = wxString::FromUTF8(progress.message),
                                .moves = {}};
        if (activeKind_ == WorkflowJob::Execute) {
            execution_.fill(details, fraction, remaining, now);
        }
        if (progress.stage != etaStage_) {
            eta_.reset(); // Another stage: its estimate has nothing to do with the last one's.
            etaStage_ = progress.stage;
        }
        const std::optional<std::chrono::seconds> shown = eta_.update(now, remaining);
        details.eta = shown ? wxString::FromUTF8(core::formatDuration(*shown)) : wxString();
        return details;
    }

    std::string WorkflowBinding::workersText(std::chrono::steady_clock::time_point now) {
        services::ThreadPool& pool = services_.pool();
        busy_.add(now, std::chrono::duration<double>(pool.busyTime()).count());
        const auto busy = busy_.perSecond(); // Busy workers on average.
        if (!busy) {
            return {};
        }
        const double workers = static_cast<double>(pool.concurrency());
        return std::format("{:.0f}% of {} threads busy", std::clamp(*busy / workers, 0.0, 1.0) * 100.0,
                           pool.concurrency());
    }

    void WorkflowBinding::takeAnalysisMap() {
        if (activeKind_ != WorkflowJob::Analyze || !analysisOutput_) {
            return;
        }
        if (auto map = analysisOutput_->take(); map) {
            widgets_.map.updateMap(std::move(map)); // Fills in while the files are read; the view stays.
        }
    }

    void WorkflowBinding::onJobFinished(wxThreadEvent& event) {
        const auto payload = event.GetPayload<JobFinishedPayload>();
        if (payload.id != activeJob_) {
            return;
        }
        const WorkflowJob job = activeKind_;
        takeTrimOutput();
        activeJob_.reset();
        activeKind_ = WorkflowJob::None;
        pause_.reset();
        trimOutput_.reset();
        completeJob(job, payload.result);
    }

    void WorkflowBinding::completeJob(WorkflowJob job, const services::JobResult& result) {
        const WorkflowJob next = workflow_.finish(result.outcome);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setProgress(std::nullopt);
        bottom.progress().show({});
        std::string status;
        switch (result.outcome) {
        case services::JobOutcome::Succeeded:
            showResults(job);
            status = successText(job, result);
            break;
        case services::JobOutcome::Cancelled:
            dropPartialMap(job);
            status = std::format("{} of {} cancelled after {}", textOf(job).title, driveLabel(), seconds(result));
            break;
        case services::JobOutcome::Failed:
            dropPartialMap(job);
            status = std::format("{} of {} failed: {}", textOf(job).title, driveLabel(), result.error);
            break;
        }
        if (job == WorkflowJob::Execute) {
            status = executionEnded(status);
        }
        bottom.setStatus(wxString::FromUTF8(status));
        startJob(next); // The plan of a Quick run.
        refresh();
    }

    void WorkflowBinding::dropPartialMap(WorkflowJob job) {
        if (job == WorkflowJob::Analyze) {
            widgets_.map.setMap(nullptr); // A half-read volume must not look analysed.
            analysisOutput_.reset();
        }
    }

    void WorkflowBinding::showResults(WorkflowJob job) {
        if (!drive_) {
            return;
        }
        if (job == WorkflowJob::Analyze) {
            showAnalysis(*drive_);
        }
        else if (job == WorkflowJob::Plan) {
            showPlan(*drive_);
        }
        else if (job == WorkflowJob::Execute) {
            showExecution(*drive_);
        }
        else if (job == WorkflowJob::Trim) {
            if (trimResult_ && trimResult_->lastRun) {
                drive_->lastOptimized = trimResult_->lastRun; // The row's "trimmed just now".
            }
            widgets_.bottom.showTab(BottomTab::Log);
        }
        widgets_.drives.updateDrive(*drive_);
    }

    void WorkflowBinding::showAnalysis(core::DriveInfo& drive) {
        std::optional<services::AnalysisSummary> real;
        if (analysisOutput_) {
            if (auto map = analysisOutput_->take(); map) {
                widgets_.map.updateMap(std::move(map));
            }
            real = analysisOutput_->takeSummary();
        }
        const services::AnalysisSummary analysis = real ? *real : services::fakeAnalysis(drive);
        drive.fragmentationPercent = analysis.fragmentationPercent;
        drive.lastAnalysed = std::chrono::system_clock::now();
        BottomPanel& bottom = widgets_.bottom;
        bottom.summary().show(analysis);
        bottom.showTab(workflow_.quick() ? BottomTab::Progress : BottomTab::Summary);
        analysis_ = analysis;
    }

    void WorkflowBinding::showPlan(const core::DriveInfo& drive) {
        if (!analysis_) {
            return;
        }
        BottomPanel& bottom = widgets_.bottom;
        const auto outcome = planOutput_ ? planOutput_->take() : nullptr;
        planOutcome_ = outcome;
        planRequest_ = planOutput_ ? planOutput_->takeRequest() : nullptr;
        planOutput_.reset();
        const services::PlanSummary plan = outcome ? services::planSummary(*outcome)
                                                   : services::fakePlan(drive, *analysis_, bottom.strategy().preset());
        bottom.plan().show(plan, bottom.strategy().effort());
        bottom.showTab(BottomTab::Plan);
        plan_ = plan;
        if (outcome) {
            const auto& overlays = outcome->overlays.at(static_cast<std::size_t>(bottom.strategy().effort()));
            widgets_.map.setPlanOverlays(overlays.zoneFit, overlays.preview);
        }
    }

    void WorkflowBinding::dropPlan(const char* hint) {
        plan_.reset();
        planOutput_.reset();
        planOutcome_.reset();
        planRequest_.reset();
        widgets_.map.setPlanOverlays(nullptr, nullptr);
        widgets_.bottom.plan().clear(hint);
    }

    bool WorkflowBinding::allowed(WorkflowAction action) const {
        if (action == WorkflowAction::BuildPlan && !widgets_.bottom.strategy().problems().empty()) {
            return false; // The Strategy tab lists what to fix first.
        }
        if (action == WorkflowAction::DefragmentFile && !fileDefragTarget()) {
            return false; // No real, fragmented, movable file selected.
        }
        return workflow_.can(action);
    }

    bool WorkflowBinding::confirmed(WorkflowAction action) {
        if (action == WorkflowAction::Execute) {
            return confirmExecute();
        }
        if (action == WorkflowAction::ResumeRun) {
            return confirmResume();
        }
        return action != WorkflowAction::DefragmentFile || confirmFileDefrag();
    }

    std::optional<core::FileIndex> WorkflowBinding::fileDefragTarget() const {
        const std::shared_ptr<const core::MapData> map = widgets_.map.currentMap();
        const std::optional<core::FileIndex> file = widgets_.map.selectedFile();
        if (services_.fakeDrives() || !drive_ || map == nullptr || !file ||
            services::fileDefragRefusal(map->snapshot(), *file)) {
            return std::nullopt; // The fake drive list has no files to move.
        }
        return file;
    }

    bool WorkflowBinding::confirmFileDefrag() {
        const std::shared_ptr<const core::MapData> map = widgets_.map.currentMap();
        const std::optional<core::FileIndex> file = fileDefragTarget();
        if (map == nullptr || !file) {
            return false;
        }
        const core::Snapshot& snapshot = map->snapshot();
        const std::string path = widgets_.map.selectedPath();
        const std::string question = std::format(
            "Defragment this file now?\n\n{}\n{} in {} fragments\n\nIt moves into one free run near where it starts, "
            "or "
            "a few runs of 64 MB or more when no run is large enough. The volume is checked first: a file that changed "
            "since the analysis is left alone. The move can be paused or cancelled, and it is logged.",
            path.empty() ? snapshot.path(*file) : path,
            core::formatBytes(snapshot.files.allocatedClusters(*file) * snapshot.geometry.bytesPerCluster),
            core::formatCount(core::planner::movableFragments(snapshot, *file)));
        const int answer = wxMessageBox(wxString::FromUTF8(question), "Defragment file",
                                        wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, &frame_);
        if (answer != wxYES) {
            return false;
        }
        fileToDefrag_ = file;
        return true;
    }

    void WorkflowBinding::showExecution(core::DriveInfo& drive) {
        BottomPanel& bottom = widgets_.bottom;
        if (runKind_ == RunKind::Plan && !planOutcome_) { // The simulated run of the fake drive list.
            for (const auto& [file, reason] : services::fakeSkippedFiles(drive)) {
                bottom.log().add(wxString::FromUTF8(file), "skipped", wxString::FromUTF8(reason));
            }
            if (plan_) {
                const auto effort = static_cast<std::size_t>(bottom.strategy().effort());
                drive.fragmentationPercent = plan_->levels.at(effort).fragmentationAfter;
            }
        }
        bottom.showTab(BottomTab::Log);
    }

    std::string WorkflowBinding::executionEnded(const std::string& status) {
        live_.finish(); // The last moves still reach the map.
        const std::optional<std::string> real = execution_.finish(driveLabel());
        if (real) {
            widgets_.bottom.showTab(BottomTab::Log); // Also after a cancel: what ran and what did not.
        }
        widgets_.bottom.summary().clear("The layout changed: analyze again to refresh these figures.");
        dropPlan("The plan was executed. Analyze again before building a new one.");
        lookForUnfinishedRun(false); // A cancelled run can be resumed; a completed one is no longer offered.
        return real.value_or(status);
    }

    void WorkflowBinding::takeTrimOutput() {
        if (!trimOutput_) {
            return;
        }
        const wxString drive = wxString::FromUTF8(driveLabel());
        for (const std::string& line : trimOutput_->take()) {
            widgets_.bottom.log().add(drive, "TRIM", wxString::FromUTF8(line));
        }
        if (auto result = trimOutput_->result(); result) {
            trimResult_ = result;
        }
    }

    std::string WorkflowBinding::successText(WorkflowJob job, const services::JobResult& result) const {
        const std::string label = driveLabel();
        switch (job) {
        case WorkflowJob::Analyze:
            return std::format("{} analysed in {}: {:.1f}% fragmented", label, seconds(result),
                               analysis_ ? analysis_->fragmentationPercent : 0.0);
        case WorkflowJob::Plan:
            return planReadyText();
        case WorkflowJob::Execute:
            return std::format("Plan executed on {} in {}; some files were skipped (see Log)", label, seconds(result));
        case WorkflowJob::Trim:
            return trimDoneText(label, result, trimResult_);
        case WorkflowJob::None:
            break;
        }
        return {};
    }

    std::string WorkflowBinding::planReadyText() const {
        if (!plan_) {
            return {};
        }
        const auto effort = widgets_.bottom.strategy().effort();
        const auto& level = plan_->levels.at(static_cast<std::size_t>(effort));
        if (const std::string blocked = drive_ ? core::movesBlockedText(*drive_) : std::string(); !blocked.empty()) {
            return std::format("{} plan for {} ready: {} to move. {}", services::effortName(effort), driveLabel(),
                               core::formatBytes(level.bytesToMove), blocked);
        }
        return std::format("{} plan for {} ready: {} to move. Review it on the map, then execute.",
                           services::effortName(effort), driveLabel(), core::formatBytes(level.bytesToMove));
    }

    void WorkflowBinding::selectDrive(const std::string& root) {
        const std::string wanted = rootKey(root);
        const auto& drives = widgets_.drives.drives();
        const auto found =
            std::ranges::find_if(drives, [&wanted](const core::DriveInfo& d) { return rootKey(d.root) == wanted; });
        if (found != drives.end()) {
            widgets_.drives.select(static_cast<std::size_t>(found - drives.begin()));
        }
    }

    void WorkflowBinding::onDriveSelected() {
        const core::DriveInfo* selected = widgets_.drives.selected();
        const std::string root = selected != nullptr ? selected->root : std::string();
        if (root == (drive_ ? drive_->root : std::string())) {
            return; // Same drive (e.g. after a refresh): keep its results.
        }
        const services::DriveKind kind = services::driveKind(selected);
        if (!workflow_.selectDrive(kind)) {
            return; // Busy; the panel is disabled, so this does not normally happen.
        }
        drive_ = selected != nullptr ? std::optional(*selected) : std::nullopt;
        live_.stop();
        widgets_.map.setVolumeRoot(drive_ ? drive_->root : std::string());
        widgets_.map.setMap(nullptr);
        const bool placed = drive_ && drive_->layout.diskBytes != 0;
        widgets_.map.setDisk(
            placed ? std::optional(core::DiskPlacement{drive_->layout.partitionOffset, drive_->layout.diskBytes})
                   : std::nullopt);
        core::logging::info("drive selected", {{"root", root}});
        analysis_.reset();
        dropPlan("Build a plan after the analysis.");
        BottomPanel& bottom = widgets_.bottom;
        bottom.summary().clear(summaryHint(kind));
        bottom.plan().setShadowCopies(drive_ && drive_->badges.vssPresent);
        restoreStrategy();
        lookForUnfinishedRun(true);
        refresh();
    }

    void WorkflowBinding::addMenuCommand(wxMenu& menu, int id, const wxString& label) const {
        menu.Append(id, withShortcut(id, label));
        menu.Enable(id, commandEnabled(id));
    }

    void WorkflowBinding::addWorkflowCommands(wxMenu& menu) const {
        const bool paused = workflow_.state() == WorkflowState::Paused;
        for (const WorkflowCommand& command : workflowCommands()) {
            if (command.onlyWhenEnabled && !commandEnabled(command.id)) {
                continue;
            }
            if (command.startsGroup) {
                menu.AppendSeparator();
            }
            const bool resume = command.id == ID_PAUSE && paused;
            addMenuCommand(menu, command.id, resume ? wxString("&Resume") : wxString(command.label));
        }
    }

    void WorkflowBinding::showDriveMenu() {
        wxMenu menu;
        addWorkflowCommands(menu);
        if (allowed(WorkflowAction::Trim)) { // SSDs: their only action.
            menu.AppendSeparator();
            addMenuCommand(menu, ID_TRIM, "&TRIM");
        }
        if (scheduleChangeable()) {
            menu.AppendSeparator();
            addMenuCommand(menu, ID_WINDOWS_SCHEDULE, "Windows &schedule...");
        }
        // The chosen item's command event travels up to the frame, where the buttons' handler takes it.
        widgets_.drives.PopupMenu(&menu);
    }

    void WorkflowBinding::restoreStrategy() {
        StrategyPage& page = widgets_.bottom.strategy();
        const std::optional<services::DriveStrategy> saved =
            drive_ ? services_.settings().driveStrategy(services::driveSettingsKey(*drive_)) : std::nullopt;
        if (!saved) {
            page.reset();
            return;
        }
        // A strategy that no longer exists (a deleted custom one) falls back to the default; the effort level applies.
        page.select(saved->preset, saved->effort);
    }

    void WorkflowBinding::rememberStrategy() {
        if (!drive_) {
            return;
        }
        const StrategyPage& page = widgets_.bottom.strategy();
        const services::DriveStrategy choice{page.presetId(), page.effort()};
        services_.settings().rememberDriveStrategy(services::driveSettingsKey(*drive_), choice);
    }

    void WorkflowBinding::onStrategyChanged() {
        rememberStrategy();
        if (!workflow_.can(WorkflowAction::ChooseStrategy)) {
            return;
        }
        const bool hadPlan = workflow_.state() == WorkflowState::Reviewing;
        workflow_.apply(WorkflowAction::ChooseStrategy);
        if (hadPlan) {
            dropPlan("The strategy changed: build the plan again.");
            widgets_.bottom.setStatus("Strategy changed; the previous plan was dropped");
        }
        refresh();
    }

    void WorkflowBinding::refreshDrives() {
        if (!workflow_.can(WorkflowAction::RefreshDrives)) {
            return;
        }
        widgets_.drives.setDrives(services_.drives().listDrives());
        if (drive_) {
            widgets_.drives.updateDrive(*drive_); // Keep this session's analysis results in the row.
        }
        widgets_.bottom.setStatus(
            wxString::Format("Drive list refreshed: %zu drives", widgets_.drives.drives().size()));
    }

    void WorkflowBinding::refresh() {
        refreshCommands();
        const bool paused = workflow_.state() == WorkflowState::Paused;
        frame_.GetMenuBar()->Check(ID_PAUSE, paused);
        widgets_.bottom.setPauseLabel(paused);
        widgets_.drives.Enable(!workflow_.busy());
        widgets_.bottom.strategy().setEditable(workflow_.can(WorkflowAction::ChooseStrategy));
    }

    void WorkflowBinding::refreshCommands() {
        for (const int id : stateDependentCommands()) {
            enableCommand(frame_, id, commandEnabled(id));
        }
        for (const WorkflowCommand& command : workflowCommands()) {
            if (command.onlyWhenEnabled) {
                widgets_.bottom.showCommand(command.id, commandEnabled(command.id));
            }
        }
    }

    bool WorkflowBinding::commandEnabled(int id) const {
        if (id == ID_WINDOWS_SCHEDULE) {
            return scheduleChangeable();
        }
        if (id == ID_RESOURCES || id == wxID_PREFERENCES) {
            return !workflow_.busy(); // Caps change only between jobs.
        }
        if (id == ID_SHOW_LEGEND) {
            return true; // The tab exists without a map too.
        }
        if (isMapCommand(id)) {
            return widgets_.map.hasMap() && widgets_.map.commandAvailable(id);
        }
        const auto action = actionFor(id, workflow_.state());
        return action.has_value() && allowed(*action);
    }

    core::ScreenAspect WorkflowBinding::screenAspect() const {
        const int index = wxDisplay::GetFromWindow(&frame_);
        const wxRect screen = wxDisplay(index == wxNOT_FOUND ? 0U : static_cast<unsigned>(index)).GetGeometry();
        return {.width = static_cast<double>(screen.width), .height = static_cast<double>(screen.height)};
    }

    std::string WorkflowBinding::driveLabel() const { return drive_ ? core::driveName(*drive_) : std::string(); }

} // namespace icd::ui
