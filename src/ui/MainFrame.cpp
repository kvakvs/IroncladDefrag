
#include "../precompiled.h"
#include "MainFrame.h"

namespace icd {

enum
{
    ID_Exit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_Analyse = wxID_HIGHEST + 1,
    ID_CancelAnalysis
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_Analyse, MainFrame::OnAnalyse)
    EVT_MENU(ID_CancelAnalysis, MainFrame::OnCancelAnalysis)
    EVT_MENU(ID_Exit, MainFrame::OnExit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600))
{
    CreateMenuBar();
    
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
    controller.SetErrorCallback([this](const std::wstring& message) {
        CallAfter([this, message]() {
            OnAnalysisError(message);
        });
    });
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

    wxMenu* analysisMenu = new wxMenu;
    analysisMenu->Append(ID_Analyse, "&Analyse\tF5", "Run a synthetic analysis job");
    analysisMenu->Append(ID_CancelAnalysis, "&Cancel Analysis\tEsc", "Cancel the active synthetic analysis job");
    menuBar->Append(analysisMenu, "&Analysis");
    
    // Set the menu bar
    SetMenuBar(menuBar);
}

void MainFrame::UpdateAnalysisMenuState(bool running)
{
    wxMenuBar* menuBar = GetMenuBar();
    if (menuBar == nullptr) {
        return;
    }

    menuBar->Enable(ID_Analyse, !running);
    menuBar->Enable(ID_CancelAnalysis, running);
}

void MainFrame::OnAnalyse(wxCommandEvent& event)
{
    UpdateAnalysisMenuState(true);
    SetStatusText("Starting synthetic analysis...");

    if (!controller.StartFakeAnalysis()) {
        UpdateAnalysisMenuState(false);
    }
}

void MainFrame::OnCancelAnalysis(wxCommandEvent& event)
{
    SetStatusText("Cancelling synthetic analysis...");
    controller.CancelActiveJob();
    UpdateAnalysisMenuState(false);
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
        SetStatusText(wxString::Format("Synthetic analysis %.0f%% - %s",
                                       progress.percentComplete,
                                       wxString(progress.statusMessage)));
        UpdateAnalysisMenuState(true);
        break;
    case JobState::Cancelled:
        SetStatusText("Synthetic analysis cancelled.");
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Completed:
        SetStatusText("Synthetic analysis complete.");
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Failed:
        SetStatusText("Synthetic analysis failed.");
        UpdateAnalysisMenuState(false);
        break;
    case JobState::Cancelling:
        SetStatusText("Cancelling synthetic analysis...");
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
    UpdateAnalysisMenuState(false);
}

void MainFrame::OnAnalysisError(const std::wstring& message)
{
    SetStatusText(wxString(message));
    UpdateAnalysisMenuState(false);
}

} // namespace icd
