#include "ui/App.h"

#include "services/AppLogging.h"
#include "services/AppServices.h"
#include "ui/MainFrame.h"

#include <wx/cmdline.h>

namespace icd {

    App::App() = default;

    App::~App() = default;

    bool App::OnInit() {
        if (!wxApp::OnInit()) {
            return false;
        }
        services::initAppLogging("IroncladDefrag");
        services_ = std::make_unique<services::AppServices>(services::driveSourceFromEnvironment());
        // wx owns top-level windows and deletes them on close.
        auto* frame = new MainFrame(*services_, initialDrive_);
        frame->Show();
        return true;
    }

    void App::OnInitCmdLine(wxCmdLineParser& parser) {
        wxApp::OnInitCmdLine(parser);
        parser.AddLongOption("drive", "select this drive at start (for example T:)", wxCMD_LINE_VAL_STRING);
    }

    bool App::OnCmdLineParsed(wxCmdLineParser& parser) {
        if (wxString drive; parser.Found("drive", &drive)) {
            initialDrive_ = drive.utf8_string();
        }
        return wxApp::OnCmdLineParsed(parser);
    }

    int App::OnExit() {
        services_.reset();
        services::shutdownAppLogging();
        return wxApp::OnExit();
    }

} // namespace icd
