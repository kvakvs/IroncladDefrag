
#include "../precompiled.h"
#include "MainFrame.h"

#include "MovePlanDialog.h"
#include "ProfileSettingsDialog.h"
#include "SafetySettingsDialog.h"

#include <sstream>

namespace icd {

enum
{
    ID_Exit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_RefreshDrives = wxID_HIGHEST + 1,
    ID_CancelAnalysis,
    ID_Profiles,
    ID_SafetySettings,
    ID_BuildPlacementIntent,
    ID_BuildMovePlan,
    ID_ExecuteMovePlan,
    ID_FirstDrive = wxID_HIGHEST + 100,
    ID_LastDrive = ID_FirstDrive + 25
};

namespace {
constexpr auto StatusThrottleInterval = std::chrono::milliseconds(100);

void EnableMenuItemIfPresent(wxMenuBar* menuBar, int id, bool enabled)
{
    if (menuBar != nullptr && menuBar->FindItem(id) != nullptr) {
        menuBar->Enable(id, enabled);
    }
}

std::wstring FormatBytes(byte_count64_t bytes)
{
    double value = static_cast<double>(bytes.getValue());
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < 4) {
        value /= 1024.0;
        ++unitIndex;
    }

    std::wstringstream stream;
    stream.precision(unitIndex == 0 ? 0 : 1);
    stream << std::fixed << value << L" " << units[unitIndex];
    return stream.str();
}
} // namespace

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_RefreshDrives, MainFrame::OnRefreshDrives)
    EVT_MENU_RANGE(ID_FirstDrive, ID_LastDrive, MainFrame::OnAnalyseDrive)
    EVT_MENU(ID_CancelAnalysis, MainFrame::OnCancelAnalysis)
    EVT_MENU(ID_Profiles, MainFrame::OnProfiles)
    EVT_MENU(ID_SafetySettings, MainFrame::OnSafetySettings)
    EVT_MENU(ID_BuildPlacementIntent, MainFrame::OnBuildPlacementIntent)
    EVT_MENU(ID_BuildMovePlan, MainFrame::OnBuildMovePlan)
    EVT_MENU(ID_ExecuteMovePlan, MainFrame::OnExecuteMovePlan)
    EVT_MENU(ID_Exit, MainFrame::OnExit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600)),
      statusFlushTimer(this)
{
    CreateMenuBar();
    CreateDocumentArea();
    
    // Create a status bar
    CreateStatusBar();
    SetStatusTextImmediate("Welcome to Ironclad Defrag");
    Bind(wxEVT_TIMER, &MainFrame::OnStatusFlushTimer, this, statusFlushTimer.GetId());

    controller.SetProgressCallback([this](const JobProgress& progress) {
        CallAfter([this, progress]() {
            OnAnalysisProgress(progress);
        });
    });
    controller.SetCompletionCallback([this](const AnalysisResult& result) {
        CallAfter([this, result]() {
            OnAnalysisComplete(result);
        });
    });
    controller.SetExecutionCompletionCallback([this](const MoveExecutionResult& result) {
        CallAfter([this, result]() {
            OnMoveExecutionComplete(result);
        });
    });
    controller.SetErrorCallback([this](const std::wstring& message) {
        CallAfter([this, message]() {
            OnAnalysisError(message);
        });
    });
    RefreshDriveMenu();
    UpdateAnalysisMenuState(false);
}

MainFrame::~MainFrame()
{
    controller.ClearCallbacks();
    controller.CancelActiveJob();
}

// Applies important status messages immediately and clears any delayed progress text.
void MainFrame::SetStatusTextImmediate(const wxString& text)
{
    statusFlushTimer.Stop();
    pendingStatusText.clear();
    hasPendingStatusText = false;
    lastStatusTextTimestamp = {};
    wxFrame::SetStatusText(text);
}

// Coalesces high-frequency progress text so the status bar updates at most ten times per second.
void MainFrame::SetStatusTextThrottled(const wxString& text)
{
    const auto now = std::chrono::steady_clock::now();
    if (lastStatusTextTimestamp == std::chrono::steady_clock::time_point{} ||
        now - lastStatusTextTimestamp >= StatusThrottleInterval) {
        statusFlushTimer.Stop();
        pendingStatusText.clear();
        hasPendingStatusText = false;
        lastStatusTextTimestamp = now;
        wxFrame::SetStatusText(text);
        return;
    }

    pendingStatusText = text;
    hasPendingStatusText = true;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        StatusThrottleInterval - (now - lastStatusTextTimestamp));
    const int delayMs = (std::max)(1, static_cast<int>(remaining.count()));
    statusFlushTimer.StartOnce(delayMs);
}

// Flushes the latest delayed progress text once the throttle interval has elapsed.
void MainFrame::FlushPendingStatusText()
{
    if (!hasPendingStatusText) {
        lastStatusTextTimestamp = {};
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastStatusTextTimestamp != std::chrono::steady_clock::time_point{} &&
        now - lastStatusTextTimestamp < StatusThrottleInterval) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            StatusThrottleInterval - (now - lastStatusTextTimestamp));
        const int delayMs = (std::max)(1, static_cast<int>(remaining.count()));
        statusFlushTimer.StartOnce(delayMs);
        return;
    }

    wxFrame::SetStatusText(pendingStatusText);
    pendingStatusText.clear();
    hasPendingStatusText = false;
    lastStatusTextTimestamp = {};
}

void MainFrame::OnStatusFlushTimer(wxTimerEvent&)
{
    FlushPendingStatusText();
}

void MainFrame::CreateMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar;
    
    // Create File menu
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_Exit, "E&xit\tAlt-X", "Quit this program");
    
    // Add menu to menu bar
    menuBar->Append(fileMenu, "&File");

    analysisMenu = new wxMenu;
    menuBar->Append(analysisMenu, "&Analysis");

    optimizationMenu = new wxMenu;
    optimizationMenu->Append(ID_Profiles, "&Profiles...", "Choose and edit optimization profile settings");
    optimizationMenu->Append(ID_SafetySettings, "&Safety Settings...", "Edit global safety guardrails and exclusions");
    optimizationMenu->Append(ID_BuildPlacementIntent,
                             "&Build Placement Intent",
                             "Build dry-run placement intent for the selected analysed drive");
    optimizationMenu->Append(ID_BuildMovePlan,
                             "Build &Move Plan",
                             "Build and inspect a dry-run move plan for the selected analysed drive");
    optimizationMenu->Append(ID_ExecuteMovePlan,
                             "&Execute Move Plan...",
                             "Execute the selected analysed drive's current bounded move plan");
    menuBar->Append(optimizationMenu, "&Optimization");
    
    // Set the menu bar
    SetMenuBar(menuBar);
}

void MainFrame::CreateDocumentArea()
{
    driveListPanel = new DriveListPanel(this);
    workflowPanel = new WorkflowPanel(this);
    documents = new wxNotebook(this, wxID_ANY);
    documents->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
        UpdateAnalysisMenuState(controller.IsJobRunning());
        event.Skip();
    });

    auto* welcome = new wxPanel(documents, wxID_ANY);
    auto* welcomeSizer = new wxBoxSizer(wxVERTICAL);
    welcomeSizer->Add(new wxStaticText(welcome, wxID_ANY, "Refresh drives, select an enabled disk, then run analysis."),
                      0,
                      wxALL,
                      12);
    welcomeSizer->Add(new wxStaticText(welcome, wxID_ANY, "Analysed drives open as workflow tabs with map, plan, and execution views."),
                      0,
                      wxLEFT | wxRIGHT | wxBOTTOM,
                      12);
    welcome->SetSizer(welcomeSizer);
    documents->AddPage(welcome, "Start", true);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(driveListPanel, 0, wxEXPAND);
    sizer->Add(documents, 1, wxEXPAND);
    sizer->Add(workflowPanel, 0, wxEXPAND);
    SetSizer(sizer);

    driveListPanel->SetRefreshCallback([this]() {
        wxCommandEvent event;
        OnRefreshDrives(event);
    });
    driveListPanel->SetAnalyzeCallback([this](const DriveInfo& drive) {
        StartAnalysisForDrive(drive);
    });
    driveListPanel->SetSelectionChangedCallback([this](const DriveInfo& drive) {
        SelectDrive(drive);
    });

    workflowPanel->SetRefreshCallback([this]() {
        wxCommandEvent event;
        OnRefreshDrives(event);
    });
    workflowPanel->SetAnalyzeCallback([this]() {
        const auto drive = std::find_if(visibleDrives.begin(), visibleDrives.end(), [this](const DriveInfo& item) {
            return item.rootPath == activeDriveRoot;
        });
        if (drive != visibleDrives.end()) {
            StartAnalysisForDrive(*drive);
        }
    });
    workflowPanel->SetCancelCallback([this]() {
        wxCommandEvent event;
        OnCancelAnalysis(event);
    });
    workflowPanel->SetSettingsCallback([this]() {
        wxCommandEvent event;
        OnProfiles(event);
    });
    workflowPanel->SetSafetySettingsCallback([this]() {
        wxCommandEvent event;
        OnSafetySettings(event);
    });
    workflowPanel->SetBuildPlacementCallback([this]() {
        BuildPlacementIntentForSelected();
    });
    workflowPanel->SetBuildPlanCallback([this]() {
        BuildMovePlanForSelected(false);
    });
    workflowPanel->SetQuickDefragCallback([this]() {
        const std::optional<OptimizationProfile> profile = controller.GetProfile(OptimizationMode::SingleFileDefragmentation);
        if (profile.has_value()) {
            RunFastLane(*profile);
        } else {
            SetStatusTextImmediate("Single-file defragmentation profile is unavailable.");
        }
    });
    workflowPanel->SetFullOptimizeCallback([this]() {
        RunFastLane(controller.GetActiveProfile());
    });
    workflowPanel->SetReviewPlanCallback([this]() {
        ReviewSelectedMovePlan();
    });
    workflowPanel->SetExecuteCallback([this]() {
        ExecuteSelectedMovePlan();
    });
    workflowPanel->SetProfileChangedCallback([this](const OptimizationProfile& profile) {
        controller.SetActiveProfile(profile);
        workflowPanel->SetProfiles(controller.GetProfiles(), controller.GetActiveProfile());
        ResetSelectedAnalysisWorkflow();
        SetStatusTextImmediate(wxString::Format("Active profile: %s", wxString(profile.name)));
        UpdateAnalysisMenuState(controller.IsJobRunning());
    });
    workflowPanel->SetProfiles(controller.GetProfiles(), controller.GetActiveProfile());
    workflowPanel->SetSafetySettings(controller.GetSafetySettings());
}

// Rebuilds the Analysis menu from current drive capabilities.
void MainFrame::RefreshDriveMenu()
{
    if (analysisMenu == nullptr) {
        return;
    }

    while (analysisMenu->GetMenuItemCount() > 0) {
        wxMenuItem* item = analysisMenu->FindItemByPosition(0);
        analysisMenu->Destroy(item);
    }

    driveMenuItems.clear();
    analysisMenu->Append(ID_RefreshDrives, "&Refresh Drives", "Refresh visible local drives");
    analysisMenu->AppendSeparator();

    const bool running = controller.IsJobRunning();
    visibleDrives = controller.EnumerateDrives();
    if (driveListPanel != nullptr) {
        driveListPanel->SetDrives(visibleDrives);
        if (!activeDriveRoot.empty()) {
            driveListPanel->SetSelectedDrive(activeDriveRoot);
        }
        driveListPanel->SetBusy(running);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetBusy(running);
    }
    int driveId = ID_FirstDrive;
    for (const DriveInfo& drive : visibleDrives) {
        if (driveId > ID_LastDrive) {
            break;
        }

        wxString label(drive.displayName);
        if (!drive.capabilities.disabledReason.empty()) {
            label += wxString::Format(" - %s", wxString(drive.capabilities.disabledReason));
        } else if (!drive.capabilities.statusReason.empty()) {
            label += wxString::Format(" - %s", wxString(drive.capabilities.statusReason));
        }

        wxMenuItem* item = analysisMenu->Append(driveId, label, "Run read-only analysis for this drive");
        item->Enable(drive.capabilities.canAnalyze && !running);
        driveMenuItems.emplace(driveId, drive);
        ++driveId;
    }

    if (driveMenuItems.empty()) {
        wxMenuItem* item = analysisMenu->Append(wxID_ANY, "No visible drives found");
        item->Enable(false);
    }

    analysisMenu->AppendSeparator();
    analysisMenu->Append(ID_CancelAnalysis, "&Cancel Analysis\tEsc", "Request cancellation of the active analysis job");
    UpdateAnalysisMenuState(running);
}

void MainFrame::UpdateAnalysisMenuState(bool running)
{
    wxMenuBar* menuBar = GetMenuBar();
    if (menuBar == nullptr) {
        return;
    }

    EnableMenuItemIfPresent(menuBar, ID_RefreshDrives, !running);
    EnableMenuItemIfPresent(menuBar, ID_CancelAnalysis, running);

    for (const auto& [id, drive] : driveMenuItems) {
        EnableMenuItemIfPresent(menuBar, id, drive.capabilities.canAnalyze && !running);
    }

    const std::optional<std::wstring> selectedDrive = GetSelectedDriveRoot();
    EnableMenuItemIfPresent(menuBar, ID_Profiles, !running);
    EnableMenuItemIfPresent(menuBar, ID_SafetySettings, !running);
    EnableMenuItemIfPresent(menuBar, ID_BuildPlacementIntent, !running && selectedDrive.has_value());
    EnableMenuItemIfPresent(menuBar, ID_BuildMovePlan, !running && selectedDrive.has_value());
    EnableMenuItemIfPresent(menuBar,
                            ID_ExecuteMovePlan,
                            !running && selectedDrive.has_value() && controller.HasExecutableMovePlan(*selectedDrive));
    if (driveListPanel != nullptr) {
        driveListPanel->SetBusy(running);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetBusy(running);
        workflowPanel->SetExecuteAllowed(selectedDrive.has_value() && controller.HasExecutableMovePlan(*selectedDrive));
    }
}

void MainFrame::SelectDrive(const DriveInfo& drive)
{
    activeDriveRoot = drive.rootPath;
    if (driveListPanel != nullptr) {
        driveListPanel->SetSelectedDrive(drive.rootPath);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetDrive(drive);
        workflowPanel->SetRecentAnalysisSummary(controller.GetRecentAnalysisSummary(drive.rootPath));
    }

    const auto existingPage = analysisPages.find(drive.rootPath);
    if (existingPage != analysisPages.end() && documents != nullptr) {
        const int index = documents->FindPage(existingPage->second);
        if (index != wxNOT_FOUND) {
            documents->SetSelection(index);
        }
    }

    for (const AnalysisResult& snapshot : controller.GetAnalysisSnapshots()) {
        if (snapshot.drive.rootPath == drive.rootPath && workflowPanel != nullptr) {
            workflowPanel->SetAnalysis(snapshot);
            if (const std::optional<PlacementPlan> placement = controller.GetPlacementPlan(drive.rootPath)) {
                workflowPanel->SetPlacementPlan(*placement);
            }
            if (const std::optional<MovePlan> plan = controller.GetMovePlan(drive.rootPath)) {
                workflowPanel->SetMovePlan(*plan);
            }
            if (const std::optional<MoveExecutionResult> result = controller.GetMoveExecutionResult(drive.rootPath)) {
                workflowPanel->SetExecutionResult(*result);
            }
            break;
        }
    }

    UpdateAnalysisMenuState(controller.IsJobRunning());
}

void MainFrame::StartAnalysisForDrive(const DriveInfo& drive)
{
    activeDriveRoot = drive.rootPath;
    if (driveListPanel != nullptr) {
        driveListPanel->SetSelectedDrive(drive.rootPath);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetDrive(drive);
        workflowPanel->SetRecentAnalysisSummary(controller.GetRecentAnalysisSummary(drive.rootPath));
    }
    UpdateAnalysisMenuState(true);
    SetStatusTextImmediate(wxString::Format("Starting read-only analysis for %s", wxString(drive.rootPath)));

    if (!controller.StartDriveAnalysis(drive)) {
        UpdateAnalysisMenuState(false);
    }
}

bool MainFrame::BuildPlacementIntentForSelected()
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusTextImmediate("Select an analysed drive before building placement intent.");
        return false;
    }

    const std::optional<PlacementPlan> plan = controller.BuildPlacementPlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusTextImmediate("No completed analysis snapshot is available for the selected drive.");
        return false;
    }

    const auto page = analysisPages.find(*driveRoot);
    if (page != analysisPages.end()) {
        page->second->UpdatePlacementPlan(*plan);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetPlacementPlan(*plan);
    }
    SetStatusTextImmediate(wxString(plan->summary));
    UpdateAnalysisMenuState(false);
    return true;
}

bool MainFrame::BuildMovePlanForSelected(bool showDialog)
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusTextImmediate("Select an analysed drive before building a move plan.");
        return false;
    }

    const std::optional<MovePlan> plan = controller.BuildMovePlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusTextImmediate("No completed analysis snapshot is available for the selected drive.");
        return false;
    }

    const auto page = analysisPages.find(*driveRoot);
    if (page != analysisPages.end()) {
        page->second->UpdateMovePlan(*plan);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetMovePlan(*plan);
    }

    if (showDialog) {
        MovePlanDialog dialog(this, *plan);
        dialog.ShowModal();
    }
    SetStatusTextImmediate(wxString(plan->summary));
    UpdateAnalysisMenuState(false);
    return true;
}

bool MainFrame::ReviewSelectedMovePlan()
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusTextImmediate("Select an analysed drive before reviewing a move plan.");
        return false;
    }

    const std::optional<MovePlan> plan = controller.GetMovePlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusTextImmediate("Build a move plan before reviewing it.");
        return false;
    }

    MovePlanDialog dialog(this, *plan);
    dialog.ShowModal();
    return true;
}

bool MainFrame::ExecuteSelectedMovePlan()
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusTextImmediate("Select an analysed drive before executing a move plan.");
        return false;
    }

    const std::optional<MovePlan> plan = controller.GetMovePlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusTextImmediate("Build a move plan before executing it.");
        return false;
    }

    if (plan->profile.settings.dryRunOnly) {
        wxMessageBox("The selected move plan was built with Dry-run only enabled. Disable Dry-run only in the profile, rebuild the "
                     "move plan, then execute it.",
                     "Execution Blocked",
                     wxOK | wxICON_WARNING,
                     this);
        SetStatusTextImmediate("Execution blocked: dry-run-only profile.");
        return false;
    }

    if (plan->impossible || plan->operations.empty()) {
        SetStatusTextImmediate(plan->impossible ? "Execution blocked: move plan is impossible."
                                                : "Execution blocked: move plan has no operations.");
        return false;
    }

    const MoveExecutionPrivilegeStatus privileges = controller.GetMoveExecutionPrivilegeStatus(*driveRoot);
    if (!privileges.canMoveFiles) {
        wxMessageDialog dialog(this,
                               wxString(privileges.message) +
                                   "\n\nClick Run Elevated to restart IroncladDefrag with UAC. No moves will run in this "
                                   "non-elevated instance.",
                               "Elevation Required",
                               wxYES_NO | wxICON_WARNING);
        dialog.SetYesNoLabels("Run Elevated", "Cancel");
        if (dialog.ShowModal() == wxID_YES) {
            if (controller.RelaunchElevatedForExecution()) {
                SetStatusTextImmediate("UAC elevation requested. Rebuild or reopen the plan in the elevated instance before executing.");
            } else {
                SetStatusTextImmediate("Unable to request UAC elevation.");
            }
        } else {
            SetStatusTextImmediate("Execution cancelled before elevation.");
        }
        return false;
    }

    const wxString driveLabel(*driveRoot);
    const wxString bytesLabel(FormatBytes(plan->estimatedBytesToMove));
    const wxString confirmation = wxString::Format("Execute a bounded Phase 6 move plan for %s?\n\n"
                                                   "Operations: %llu\n"
                                                   "Estimated bytes: %s\n\n"
                                                   "Use only on controlled test data or a disposable NTFS volume.",
                                                   driveLabel.c_str(),
                                                   static_cast<unsigned long long>(plan->metrics.affectedFiles.getValue()),
                                                   bytesLabel.c_str());
    if (wxMessageBox(confirmation, "Confirm Move Execution", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        SetStatusTextImmediate("Move execution cancelled before start.");
        return false;
    }

    if (controller.StartMovePlanExecution(*driveRoot)) {
        SetStatusTextImmediate("Starting move execution...");
        UpdateAnalysisMenuState(true);
        return true;
    }

    UpdateAnalysisMenuState(false);
    return false;
}

bool MainFrame::RunFastLane(const OptimizationProfile& profile)
{
    controller.SetActiveProfile(profile);
    if (workflowPanel != nullptr) {
        workflowPanel->SetProfiles(controller.GetProfiles(), controller.GetActiveProfile());
    }

    if (!BuildPlacementIntentForSelected()) {
        return false;
    }
    if (!BuildMovePlanForSelected(false)) {
        return false;
    }
    return ExecuteSelectedMovePlan();
}

void MainFrame::ResetSelectedAnalysisWorkflow()
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        return;
    }

    for (const AnalysisResult& snapshot : controller.GetAnalysisSnapshots()) {
        if (snapshot.drive.rootPath == *driveRoot) {
            const auto page = analysisPages.find(*driveRoot);
            if (page != analysisPages.end()) {
                page->second->UpdateResult(snapshot);
            }
            if (workflowPanel != nullptr) {
                workflowPanel->SetAnalysis(snapshot);
                workflowPanel->SetRecentAnalysisSummary(std::nullopt);
            }
            break;
        }
    }
}

void MainFrame::OnRefreshDrives(wxCommandEvent& event)
{
    RefreshDriveMenu();
    SetStatusTextImmediate("Drive list refreshed.");
}

void MainFrame::OnAnalyseDrive(wxCommandEvent& event)
{
    const auto item = driveMenuItems.find(event.GetId());
    if (item == driveMenuItems.end()) {
        SetStatusTextImmediate("Drive menu item is no longer available.");
        return;
    }

    const DriveInfo drive = item->second;
    StartAnalysisForDrive(drive);
}

void MainFrame::OnCancelAnalysis(wxCommandEvent& event)
{
    SetStatusTextImmediate("Cancelling active job...");
    controller.RequestCancelActiveJob();
    UpdateAnalysisMenuState(true);
}

void MainFrame::OnProfiles(wxCommandEvent&)
{
    ProfileSettingsDialog dialog(this, controller.GetProfiles(), controller.GetActiveProfile());
    if (dialog.ShowModal() == wxID_OK) {
        controller.SetActiveProfile(dialog.GetSelectedProfile());
        if (workflowPanel != nullptr) {
            workflowPanel->SetProfiles(controller.GetProfiles(), controller.GetActiveProfile());
        }
        ResetSelectedAnalysisWorkflow();
        SetStatusTextImmediate(wxString::Format("Active profile: %s", wxString(dialog.GetSelectedProfile().name)));
        UpdateAnalysisMenuState(false);
    }
}

void MainFrame::OnSafetySettings(wxCommandEvent&)
{
    SafetySettingsDialog dialog(this, controller.GetSafetySettings());
    if (dialog.ShowModal() == wxID_OK) {
        controller.SetSafetySettings(dialog.GetSafetySettings());
        if (workflowPanel != nullptr) {
            workflowPanel->SetSafetySettings(controller.GetSafetySettings());
        }
        ResetSelectedAnalysisWorkflow();
        SetStatusTextImmediate("Safety settings saved. Rebuild placement and move plans to apply them.");
        UpdateAnalysisMenuState(false);
    }
}

void MainFrame::OnBuildPlacementIntent(wxCommandEvent&)
{
    BuildPlacementIntentForSelected();
}

void MainFrame::OnBuildMovePlan(wxCommandEvent&)
{
    BuildMovePlanForSelected(true);
}

void MainFrame::OnExecuteMovePlan(wxCommandEvent&)
{
    ExecuteSelectedMovePlan();
}

void MainFrame::OnExit(wxCommandEvent& event)
{
    Close(true);
}

void MainFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox("Ironclad Defrag - Disk Defragmentation Tool",
                 "About Ironclad Defrag",
                 wxOK | wxICON_INFORMATION,
                 this);
}

void MainFrame::OnClose(wxCloseEvent& event)
{
    controller.ClearCallbacks();
    controller.CancelActiveJob();
    event.Skip();
}

void MainFrame::OnAnalysisProgress(const JobProgress& progress)
{
    switch (progress.state) {
    case JobState::Running:
        SetStatusTextThrottled(wxString::Format("%.0f%% - %s %s",
                                                progress.percentComplete,
                                                wxString(progress.statusMessage),
                                                wxString(progress.currentItem)));
        UpdateAnalysisMenuState(true);
        break;
    case JobState::Cancelled:
        SetStatusTextImmediate(progress.statusMessage.empty() ? wxString("Job cancelled.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Completed:
        SetStatusTextImmediate(progress.statusMessage.empty() ? wxString("Job complete.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Failed:
        SetStatusTextImmediate(progress.statusMessage.empty() ? wxString("Job failed.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Cancelling:
        SetStatusTextImmediate("Cancelling active job...");
        UpdateAnalysisMenuState(true);
        break;
    case JobState::Idle:
        UpdateAnalysisMenuState(false);
        break;
    }
}

void MainFrame::OnAnalysisComplete(const AnalysisResult& result)
{
    SetStatusTextImmediate(wxString(result.summary));
    OpenOrUpdateAnalysisPage(result);
    if (driveListPanel != nullptr) {
        driveListPanel->SetSelectedDrive(result.drive.rootPath);
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetAnalysis(result);
        workflowPanel->SetRecentAnalysisSummary(std::nullopt);
    }
    activeDriveRoot = result.drive.rootPath;
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnMoveExecutionComplete(const MoveExecutionResult& result)
{
    SetStatusTextImmediate(wxString(result.summary));
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (driveRoot.has_value()) {
        const auto page = analysisPages.find(*driveRoot);
        if (page != analysisPages.end()) {
            page->second->UpdateExecutionResult(result);
        }
    }
    if (workflowPanel != nullptr) {
        workflowPanel->SetExecutionResult(result);
    }
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnAnalysisError(const std::wstring& message)
{
    SetStatusTextImmediate(wxString(message));
    if (workflowPanel != nullptr) {
        workflowPanel->SetBusy(false);
    }
    UpdateAnalysisMenuState(false);
}

// Opens a new drive document tab or refreshes the existing snapshot for that drive.
void MainFrame::OpenOrUpdateAnalysisPage(const AnalysisResult& result)
{
    if (documents == nullptr) {
        return;
    }

    auto existing = analysisPages.find(result.drive.rootPath);
    if (existing != analysisPages.end()) {
        existing->second->UpdateResult(result);
        const int index = documents->FindPage(existing->second);
        if (index != wxNOT_FOUND) {
            documents->SetSelection(index);
        }
        return;
    }

    auto* page = new DriveAnalysisPage(documents, result);
    documents->AddPage(page, wxString(result.drive.rootPath), true);
    analysisPages.emplace(result.drive.rootPath, page);
}

std::optional<std::wstring> MainFrame::GetSelectedDriveRoot() const
{
    if (documents == nullptr) {
        return activeDriveRoot.empty() ? std::nullopt : std::optional<std::wstring>(activeDriveRoot);
    }

    const int selection = documents->GetSelection();
    if (selection == wxNOT_FOUND) {
        return std::nullopt;
    }

    auto* page = dynamic_cast<DriveAnalysisPage*>(documents->GetPage(selection));
    if (page == nullptr) {
        if (!activeDriveRoot.empty() && controller.HasAnalysisSnapshot(activeDriveRoot)) {
            return activeDriveRoot;
        }
        return std::nullopt;
    }

    return page->GetDriveRoot();
}

} // namespace icd
