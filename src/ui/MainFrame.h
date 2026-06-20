
#pragma once

#include "../app/ApplicationController.h"
#include "DriveListPanel.h"
#include "DriveAnalysisPage.h"
#include "WorkflowPanel.h"

#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>
#include <wx/notebook.h>
#include <wx/timer.h>
#include <wx/wx.h>

namespace icd {

// Owns the main wxWidgets frame, menus, status text, and analysis document tabs.
class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title);
    virtual ~MainFrame();

private:
    void CreateMenuBar();
    void CreateDocumentArea();
    void RefreshDriveMenu();
    void UpdateAnalysisMenuState(bool running);
    void SelectDrive(const DriveInfo& drive);
    void StartAnalysisForDrive(const DriveInfo& drive);
    bool BuildPlacementIntentForSelected();
    bool BuildMovePlanForSelected(bool showDialog);
    bool ReviewSelectedMovePlan();
    bool ExecuteSelectedMovePlan();
    bool RunFastLane(const OptimizationProfile& profile);
    void ResetSelectedAnalysisWorkflow();
    void OnRefreshDrives(wxCommandEvent& event);
    void OnAnalyseDrive(wxCommandEvent& event);
    void OnCancelAnalysis(wxCommandEvent& event);
    void OnProfiles(wxCommandEvent& event);
    void OnSafetySettings(wxCommandEvent& event);
    void OnBuildPlacementIntent(wxCommandEvent& event);
    void OnBuildMovePlan(wxCommandEvent& event);
    void OnExecuteMovePlan(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnAnalysisProgress(const JobProgress& progress);
    void OnAnalysisComplete(const AnalysisResult& result);
    void OnMoveExecutionComplete(const MoveExecutionResult& result);
    void OnAnalysisError(const std::wstring& message);
    void OpenOrUpdateAnalysisPage(const AnalysisResult& result);
    std::optional<std::wstring> GetSelectedDriveRoot() const;
    void SetStatusTextImmediate(const wxString& text);
    void SetStatusTextThrottled(const wxString& text);
    void FlushPendingStatusText();
    void OnStatusFlushTimer(wxTimerEvent& event);

    ApplicationController controller;
    wxTimer statusFlushTimer;
    wxMenu* analysisMenu = nullptr;
    wxMenu* optimizationMenu = nullptr;
    DriveListPanel* driveListPanel = nullptr;
    wxNotebook* documents = nullptr;
    WorkflowPanel* workflowPanel = nullptr;
    std::vector<DriveInfo> visibleDrives;
    std::unordered_map<int, DriveInfo> driveMenuItems;
    std::unordered_map<std::wstring, DriveAnalysisPage*> analysisPages;
    std::wstring activeDriveRoot;
    wxString pendingStatusText;
    bool hasPendingStatusText = false;
    std::chrono::steady_clock::time_point lastStatusTextTimestamp{};

    wxDECLARE_EVENT_TABLE();
};

} // namespace icd
