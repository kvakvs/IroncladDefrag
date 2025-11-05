#include "../precompiled.h"
#include "App.h"

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

        // TODO: Create main frame window
        // Example:
        // MainFrame* frame = new MainFrame("Ironclad Defrag", wxDefaultPosition, wxSize(800, 600));
        // frame->Show(true);

        return true;
    }

    int App::OnExit()
    {
        return wxApp::OnExit();
    }
} // namespace icd
