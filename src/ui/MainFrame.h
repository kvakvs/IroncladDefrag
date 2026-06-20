
#pragma once

#include "../app/ApplicationController.h"

#include <wx/wx.h>

namespace icd {

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title);
    virtual ~MainFrame();

private:
    void CreateMenuBar();
    void UpdateAnalysisMenuState(bool running);
    void OnAnalyse(wxCommandEvent& event);
    void OnCancelAnalysis(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnAnalysisProgress(const JobProgress& progress);
    void OnAnalysisComplete(const AnalysisResult& result);
    void OnAnalysisError(const std::wstring& message);

    ApplicationController controller;

    wxDECLARE_EVENT_TABLE();
};

} // namespace icd
