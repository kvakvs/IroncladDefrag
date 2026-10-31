#include "ui/WorkflowBinding.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/DriveText.h"
#include "core/format/Duration.h"
#include "core/job/PauseGate.h"
#include "core/log/Log.h"
#include "services/AppServices.h"
#include "services/job/FakeJob.h"
#include "ui/Commands.h"
#include "ui/MainMenu.h"
#include "ui/bridge/JobEvents.h"
#include "ui/panels/BottomPanel.h"
#include "ui/panels/DrivesPanel.h"
#include "ui/panels/MapPlaceholder.h"
#include "ui/panels/PlanPage.h"
#include "ui/panels/StrategyPage.h"
#include "ui/panels/SummaryPage.h"

#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

#include <array>
#include <format>

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

        // How the map placeholder looks in a state: caption after the drive name and pattern variant.
        struct MapLook {
            const char* caption = ""; // Text after "D: Data: ".
            std::uint32_t variant = 0; // 0 = empty map; others pick a pattern.
        };

        // Looks indexed by WorkflowState.
        constexpr std::array<MapLook, 12> mapLooks{{
            {"", 0},
            {"not supported (only NTFS volumes are optimized)", 0},
            {"SSD, TRIM only (no map)", 0},
            {"TRIM running", 0},
            {"not analysed yet", 0},
            {"analysing...", 0},
            {"analysed (placeholder map)", 1},
            {"building plan...", 1},
            {"plan preview (placeholder)", 2},
            {"executing plan...", 2},
            {"execution paused", 2},
            {"plan executed; analyze again to refresh", 3},
        }};

        // Hint for the Summary tab when a drive of this kind is selected but not analysed.
        const char* summaryHint(services::DriveKind kind) {
            switch (kind) {
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

        // Seconds with one decimal, for result texts.
        std::string seconds(const services::JobResult& result) {
            return std::format("{:.1f} s", static_cast<double>(result.elapsed.count()) / 1000.0);
        }

    } // namespace

    WorkflowBinding::WorkflowBinding(wxFrame& frame, services::AppServices& services, Widgets widgets) :
        frame_(frame), services_(services), widgets_(widgets), bridge_(std::make_shared<JobEventBridge>(&frame)) {
        bindEvents();
        widgets_.drives.setSelectionHandler([this](std::optional<std::size_t>) { onDriveSelected(); });
        widgets_.bottom.strategy().setChangeHandler([this] { onStrategyChanged(); });
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
        frame_.Bind(jobProgressEvent(), &WorkflowBinding::onJobProgress, this);
        frame_.Bind(jobFinishedEvent(), &WorkflowBinding::onJobFinished, this);
    }

    void WorkflowBinding::onViewCommand(wxCommandEvent& event) {
        const int id = event.GetId();
        if (!commandEnabled(id)) {
            return; // Map keys arrive even when the menu item is disabled.
        }
        const wxMenuItem* item = frame_.GetMenuBar()->FindItem(id);
        const wxString label = item != nullptr ? item->GetItemLabelText() : wxString();
        if (id >= ID_COLOR_CLASS && id <= ID_COLOR_PLAN) {
            widgets_.map.setColorMode(id - ID_COLOR_CLASS);
            frame_.GetMenuBar()->Check(id, true);
        }
        widgets_.bottom.setStatus("View: " + label + " (placeholder until the map engine exists)");
    }

    void WorkflowBinding::onCommand(wxCommandEvent& event) {
        const auto action = actionFor(event.GetId(), workflow_.state());
        if (!action) {
            event.Skip();
            return;
        }
        perform(*action);
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
        if (!workflow_.can(action) || (action == WorkflowAction::Execute && !confirmExecute())) {
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
            frame_.GetMenuBar()->Check(ID_COLOR_PLAN, true);
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
        if (!plan_) {
            return false;
        }
        const auto& level = plan_->levels.at(static_cast<std::size_t>(widgets_.bottom.strategy().effort()));
        const std::string text = std::format(
            "Execute the {} plan on {}?\n\n{} files ({}) will be moved.\n\nThis is the UI shell: the run is "
            "simulated and nothing on the disk changes.",
            services::effortName(level.level), driveLabel(), core::formatCount(level.filesToMove),
            core::formatBytes(level.bytesToMove));
        wxMessageDialog dialog(&frame_, wxString::FromUTF8(text), "Execute plan",
                               wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
        dialog.SetYesNoLabels("&Execute", "&Cancel");
        return dialog.ShowModal() == wxID_YES;
    }

    void WorkflowBinding::startJob(WorkflowJob job) {
        if (job == WorkflowJob::None || !drive_) {
            return;
        }
        const core::DriveInfo& drive = *drive_;
        resetFor(job);
        const StepText& text = textOf(job);
        clock_.emplace(std::chrono::steady_clock::now());
        activeKind_ = job;
        activeJob_ = services_.jobs().start(std::format("{} {}", text.title, drive.root), makeJob(job, drive), bridge_);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setStatus(wxString::FromUTF8(std::format("{} {}...", text.verb, driveLabel())));
        bottom.setProgress(0.0);
        bottom.setProgressDetails({});
        bottom.showTab(BottomTab::Progress);
    }

    void WorkflowBinding::resetFor(WorkflowJob job) {
        BottomPanel& bottom = widgets_.bottom;
        if (job == WorkflowJob::Analyze) {
            analysis_.reset();
            bottom.summary().clear("Analysis running...");
        }
        if (job == WorkflowJob::Analyze || job == WorkflowJob::Plan) {
            plan_.reset();
            bottom.plan().clear(job == WorkflowJob::Plan ? "Building the plan..." : "Build a plan after the analysis.");
        }
        if (job == WorkflowJob::Execute) {
            bottom.clearLog();
            pause_ = std::make_shared<core::PauseGate>();
        }
    }

    services::JobFunction WorkflowBinding::makeJob(WorkflowJob job, const core::DriveInfo& drive) {
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

    void WorkflowBinding::onJobProgress(wxThreadEvent& event) {
        const auto payload = event.GetPayload<JobProgressPayload>();
        if (payload.id != activeJob_ || !clock_ || workflow_.state() == WorkflowState::Paused) {
            return; // A late event from an earlier job, or one that raced the pause.
        }
        const core::Progress& progress = payload.progress;
        const auto now = std::chrono::steady_clock::now();
        clock_->update(progress, now);
        std::string count = core::formatCount(progress.done);
        if (progress.total != 0) {
            count += " of " + core::formatCount(progress.total);
        }
        const auto remaining = clock_->remaining(now);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setStatus(wxString::FromUTF8(
            std::format("{} {}: {}, {}", textOf(activeKind_).verb, driveLabel(), progress.stage, count)));
        bottom.setProgress(progress.fraction());
        bottom.setProgressDetails({.stage = wxString::FromUTF8(progress.stage),
                                   .done = wxString::FromUTF8(count),
                                   .elapsed = wxString::FromUTF8(core::formatDuration(clock_->elapsed(now))),
                                   .throughput = {},
                                   .eta = remaining ? wxString::FromUTF8(core::formatDuration(*remaining)) : "",
                                   .currentFile = wxString::FromUTF8(progress.message)});
    }

    void WorkflowBinding::onJobFinished(wxThreadEvent& event) {
        const auto payload = event.GetPayload<JobFinishedPayload>();
        if (payload.id != activeJob_) {
            return;
        }
        const WorkflowJob job = activeKind_;
        activeJob_.reset();
        activeKind_ = WorkflowJob::None;
        pause_.reset();
        completeJob(job, payload.result);
    }

    void WorkflowBinding::completeJob(WorkflowJob job, const services::JobResult& result) {
        const WorkflowJob next = workflow_.finish(result.outcome);
        BottomPanel& bottom = widgets_.bottom;
        bottom.setProgress(std::nullopt);
        bottom.setProgressDetails({});
        std::string status;
        switch (result.outcome) {
        case services::JobOutcome::Succeeded:
            showResults(job);
            status = successText(job, result);
            break;
        case services::JobOutcome::Cancelled:
            status = std::format("{} of {} cancelled after {}", textOf(job).title, driveLabel(), seconds(result));
            break;
        case services::JobOutcome::Failed:
            status = std::format("{} of {} failed: {}", textOf(job).title, driveLabel(), result.error);
            break;
        }
        bottom.setStatus(wxString::FromUTF8(status));
        startJob(next); // The plan of a Quick run.
        refresh();
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
        widgets_.drives.updateDrive(*drive_);
    }

    void WorkflowBinding::showAnalysis(core::DriveInfo& drive) {
        const services::AnalysisSummary analysis = services::fakeAnalysis(drive);
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
        const services::PlanSummary plan = services::fakePlan(drive, *analysis_, bottom.strategy().preset());
        bottom.plan().show(plan, bottom.strategy().effort());
        bottom.showTab(BottomTab::Plan);
        plan_ = plan;
    }

    void WorkflowBinding::showExecution(core::DriveInfo& drive) {
        BottomPanel& bottom = widgets_.bottom;
        for (const auto& [file, reason] : services::fakeSkippedFiles(drive)) {
            bottom.addLogEntry(wxString::FromUTF8(file), wxString::FromUTF8(reason));
        }
        if (plan_) {
            const auto effort = static_cast<std::size_t>(bottom.strategy().effort());
            drive.fragmentationPercent = plan_->levels.at(effort).fragmentationAfter;
        }
        bottom.summary().clear("The layout changed: analyze again to refresh these figures.");
        bottom.plan().clear("The plan was executed. Analyze again before building a new one.");
        bottom.showTab(BottomTab::Log);
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
            return std::format("TRIM of {} finished in {}", label, seconds(result));
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
        return std::format("{} plan for {} ready: {} to move. Review it on the map, then execute.",
                           services::effortName(effort), driveLabel(), core::formatBytes(level.bytesToMove));
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
        core::logging::info("drive selected", {{"root", root}});
        analysis_.reset();
        plan_.reset();
        BottomPanel& bottom = widgets_.bottom;
        bottom.summary().clear(summaryHint(kind));
        bottom.plan().clear("Build a plan after the analysis.");
        bottom.strategy().reset();
        refresh();
    }

    void WorkflowBinding::onStrategyChanged() {
        if (!workflow_.can(WorkflowAction::ChooseStrategy)) {
            return;
        }
        const bool hadPlan = workflow_.state() == WorkflowState::Reviewing;
        workflow_.apply(WorkflowAction::ChooseStrategy);
        if (hadPlan) {
            plan_.reset();
            widgets_.bottom.plan().clear("The strategy changed: build the plan again.");
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
        refreshMap();
    }

    void WorkflowBinding::refreshCommands() {
        for (const int id : stateDependentCommands()) {
            enableCommand(frame_, id, commandEnabled(id));
        }
    }

    bool WorkflowBinding::commandEnabled(int id) const {
        if (id >= ID_COLOR_CLASS && id <= ID_ZOOM_CLUSTER) {
            return drive_.has_value() && mapLooks.at(static_cast<std::size_t>(workflow_.state())).variant != 0;
        }
        const auto action = actionFor(id, workflow_.state());
        return action.has_value() && workflow_.can(*action);
    }

    void WorkflowBinding::refreshMap() {
        MapPlaceholder& map = widgets_.map;
        if (!drive_) {
            map.setCaption("No drive selected");
            map.setPattern(0);
            return;
        }
        const MapLook& look = mapLooks.at(static_cast<std::size_t>(workflow_.state()));
        map.setCaption(wxString::FromUTF8(driveLabel() + ": " + look.caption));
        const auto letter = static_cast<std::uint32_t>(static_cast<unsigned char>(drive_->root.front()));
        map.setPattern(look.variant == 0 ? 0 : letter * 4 + look.variant);
    }

    std::string WorkflowBinding::driveLabel() const { return drive_ ? core::driveName(*drive_) : std::string(); }

} // namespace icd::ui
