#include "../precompiled.h"
#include "App.h"
#include "MainFrame.h"

wxBEGIN_EVENT_TABLE(icd::App, wxApp)
wxEND_EVENT_TABLE()

namespace icd {

    App::App()
    {
    }

    App::~App()
    {
    }

    bool App::OnInit()
    {
        if (!wxApp::OnInit())
            return false;

        MainFrame* frame = new MainFrame("Ironclad Defrag");
        frame->Show(true);

        return true;
    }

    int App::OnExit()
    {
        return wxApp::OnExit();
    }
} // namespace icd
