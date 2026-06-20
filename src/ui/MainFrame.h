
#pragma once

#include "../app/ApplicationController.h"
#include "DriveAnalysisPage.h"

#include <optional>
#include <unordered_map>
#include <wx/wx.h>
#include <wx/notebook.h>

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
    void OnRefreshDrives(wxCommandEvent& event);
    void OnAnalyseDrive(wxCommandEvent& event);
    void OnCancelAnalysis(wxCommandEvent& event);
    void OnProfiles(wxCommandEvent& event);
    void OnBuildPlacementIntent(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnAnalysisProgress(const JobProgress& progress);
    void OnAnalysisComplete(const AnalysisResult& result);
    void OnAnalysisError(const std::wstring& message);
    void OpenOrUpdateAnalysisPage(const AnalysisResult& result);
    std::optional<std::wstring> GetSelectedDriveRoot() const;

    ApplicationController controller;
    wxMenu* analysisMenu = nullptr;
    wxMenu* optimizationMenu = nullptr;
    wxNotebook* documents = nullptr;
    std::unordered_map<int, DriveInfo> driveMenuItems;
    std::unordered_map<std::wstring, DriveAnalysisPage*> analysisPages;
    std::wstring activeDriveRoot;

    wxDECLARE_EVENT_TABLE();
};

} // namespace icd
