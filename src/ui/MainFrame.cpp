
#include "../precompiled.h"
#include "MainFrame.h"

#include "MovePlanDialog.h"
#include "ProfileSettingsDialog.h"

#include <sstream>

namespace icd {

enum
{
    ID_Exit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_RefreshDrives = wxID_HIGHEST + 1,
    ID_CancelAnalysis,
    ID_Profiles,
    ID_BuildPlacementIntent,
    ID_BuildMovePlan,
    ID_ExecuteMovePlan,
    ID_FirstDrive = wxID_HIGHEST + 100,
    ID_LastDrive = ID_FirstDrive + 25
};

namespace {
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
    EVT_MENU(ID_BuildPlacementIntent, MainFrame::OnBuildPlacementIntent)
    EVT_MENU(ID_BuildMovePlan, MainFrame::OnBuildMovePlan)
    EVT_MENU(ID_ExecuteMovePlan, MainFrame::OnExecuteMovePlan)
    EVT_MENU(ID_Exit, MainFrame::OnExit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600))
{
    CreateMenuBar();
    CreateDocumentArea();
    
    // Create a status bar
    CreateStatusBar();
    SetStatusText("Welcome to Ironclad Defrag");

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
    documents = new wxNotebook(this, wxID_ANY);
    documents->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
        UpdateAnalysisMenuState(controller.IsJobRunning());
        event.Skip();
    });

    auto* welcome = new wxPanel(documents, wxID_ANY);
    auto* welcomeSizer = new wxBoxSizer(wxVERTICAL);
    welcomeSizer->Add(new wxStaticText(welcome, wxID_ANY, "Select Analysis > Refresh Drives, then choose an enabled drive."),
                      0,
                      wxALL,
                      12);
    welcomeSizer->Add(new wxStaticText(welcome, wxID_ANY, "Drive map TODO: analysed drives will open as document tabs."),
                      0,
                      wxLEFT | wxRIGHT | wxBOTTOM,
                      12);
    welcome->SetSizer(welcomeSizer);
    documents->AddPage(welcome, "Start", true);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(documents, 1, wxEXPAND);
    SetSizer(sizer);
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
    const std::vector<DriveInfo> drives = controller.EnumerateDrives();
    int driveId = ID_FirstDrive;
    for (const DriveInfo& drive : drives) {
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
    EnableMenuItemIfPresent(menuBar, ID_BuildPlacementIntent, !running && selectedDrive.has_value());
    EnableMenuItemIfPresent(menuBar, ID_BuildMovePlan, !running && selectedDrive.has_value());
    EnableMenuItemIfPresent(menuBar,
                            ID_ExecuteMovePlan,
                            !running && selectedDrive.has_value() && controller.HasMovePlan(*selectedDrive));
}

void MainFrame::OnRefreshDrives(wxCommandEvent& event)
{
    RefreshDriveMenu();
    SetStatusText("Drive list refreshed.");
}

void MainFrame::OnAnalyseDrive(wxCommandEvent& event)
{
    const auto item = driveMenuItems.find(event.GetId());
    if (item == driveMenuItems.end()) {
        SetStatusText("Drive menu item is no longer available.");
        return;
    }

    const DriveInfo drive = item->second;
    activeDriveRoot = drive.rootPath;
    UpdateAnalysisMenuState(true);
    SetStatusText(wxString::Format("Starting read-only analysis for %s", wxString(drive.rootPath)));

    if (!controller.StartDriveAnalysis(drive)) {
        UpdateAnalysisMenuState(false);
    }
}

void MainFrame::OnCancelAnalysis(wxCommandEvent& event)
{
    SetStatusText("Cancelling drive analysis...");
    controller.RequestCancelActiveJob();
    UpdateAnalysisMenuState(true);
}

void MainFrame::OnProfiles(wxCommandEvent&)
{
    ProfileSettingsDialog dialog(this, controller.GetProfiles(), controller.GetActiveProfile());
    if (dialog.ShowModal() == wxID_OK) {
        controller.SetActiveProfile(dialog.GetSelectedProfile());
        SetStatusText(wxString::Format("Active profile: %s", wxString(dialog.GetSelectedProfile().name)));
        UpdateAnalysisMenuState(false);
    }
}

void MainFrame::OnBuildPlacementIntent(wxCommandEvent&)
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusText("Select an analysed drive tab before building placement intent.");
        return;
    }

    const std::optional<PlacementPlan> plan = controller.BuildPlacementPlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusText("No completed analysis snapshot is available for the selected drive.");
        return;
    }

    const auto page = analysisPages.find(*driveRoot);
    if (page != analysisPages.end()) {
        page->second->UpdatePlacementPlan(*plan);
    }
    SetStatusText(wxString(plan->summary));
}

void MainFrame::OnBuildMovePlan(wxCommandEvent&)
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusText("Select an analysed drive tab before building a move plan.");
        return;
    }

    const std::optional<MovePlan> plan = controller.BuildMovePlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusText("No completed analysis snapshot is available for the selected drive.");
        return;
    }

    const auto page = analysisPages.find(*driveRoot);
    if (page != analysisPages.end()) {
        page->second->UpdateMovePlan(*plan);
    }

    MovePlanDialog dialog(this, *plan);
    dialog.ShowModal();
    SetStatusText(wxString(plan->summary));
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnExecuteMovePlan(wxCommandEvent&)
{
    const std::optional<std::wstring> driveRoot = GetSelectedDriveRoot();
    if (!driveRoot.has_value()) {
        SetStatusText("Select an analysed drive tab before executing a move plan.");
        return;
    }

    const std::optional<MovePlan> plan = controller.GetMovePlan(*driveRoot);
    if (!plan.has_value()) {
        SetStatusText("Build a move plan before executing it.");
        return;
    }

    if (plan->profile.settings.dryRunOnly) {
        wxMessageBox("The selected move plan was built with Dry-run only enabled. Disable Dry-run only in the profile, rebuild the "
                     "move plan, then execute it.",
                     "Execution Blocked",
                     wxOK | wxICON_WARNING,
                     this);
        SetStatusText("Execution blocked: dry-run-only profile.");
        return;
    }

    if (plan->impossible || plan->operations.empty()) {
        SetStatusText(plan->impossible ? "Execution blocked: move plan is impossible."
                                       : "Execution blocked: move plan has no operations.");
        return;
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
                SetStatusText("UAC elevation requested. Rebuild or reopen the plan in the elevated instance before executing.");
            } else {
                SetStatusText("Unable to request UAC elevation.");
            }
        } else {
            SetStatusText("Execution cancelled before elevation.");
        }
        return;
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
        SetStatusText("Move execution cancelled before start.");
        return;
    }

    if (controller.StartMovePlanExecution(*driveRoot)) {
        SetStatusText("Starting move execution...");
        UpdateAnalysisMenuState(true);
    } else {
        UpdateAnalysisMenuState(false);
    }
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
        SetStatusText(wxString::Format("%.0f%% - %s %s",
                                       progress.percentComplete,
                                       wxString(progress.statusMessage),
                                       wxString(progress.currentItem)));
        UpdateAnalysisMenuState(true);
        break;
    case JobState::Cancelled:
        SetStatusText(progress.statusMessage.empty() ? wxString("Job cancelled.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Completed:
        SetStatusText(progress.statusMessage.empty() ? wxString("Job complete.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Failed:
        SetStatusText(progress.statusMessage.empty() ? wxString("Job failed.") : wxString(progress.statusMessage));
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Cancelling:
        SetStatusText("Cancelling drive analysis...");
        UpdateAnalysisMenuState(true);
        break;
    case JobState::Idle:
        UpdateAnalysisMenuState(false);
        break;
    }
}

void MainFrame::OnAnalysisComplete(const AnalysisResult& result)
{
    SetStatusText(wxString(result.summary));
    OpenOrUpdateAnalysisPage(result);
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnMoveExecutionComplete(const MoveExecutionResult& result)
{
    SetStatusText(wxString(result.summary));
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnAnalysisError(const std::wstring& message)
{
    SetStatusText(wxString(message));
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
        return std::nullopt;
    }

    const int selection = documents->GetSelection();
    if (selection == wxNOT_FOUND) {
        return std::nullopt;
    }

    auto* page = dynamic_cast<DriveAnalysisPage*>(documents->GetPage(selection));
    if (page == nullptr) {
        return std::nullopt;
    }

    return page->GetDriveRoot();
}

} // namespace icd
