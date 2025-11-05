
#include "../precompiled.h"
#include "MainFrame.h"

namespace icd {

enum
{
    ID_Exit = wxID_EXIT,
    ID_About = wxID_ABOUT
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_Exit, MainFrame::OnExit)
    EVT_MENU(ID_About, MainFrame::OnAbout)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600))
{
    CreateMenuBar();
    
    // Create a status bar
    CreateStatusBar();
    SetStatusText("Welcome to Ironclad Defrag");
}

MainFrame::~MainFrame()
{
}

void MainFrame::CreateMenuBar()
{
    wxMenuBar* menuBar = new wxMenuBar;
    
    // Create File menu
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_Exit, "E&xit\tAlt-X", "Quit this program");
    
    // Add menu to menu bar
    menuBar->Append(fileMenu, "&File");
    
    // Set the menu bar
    SetMenuBar(menuBar);
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

} // namespace icd
