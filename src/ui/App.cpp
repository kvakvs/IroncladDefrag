#include "ui/App.h"

#include "services/AppLogging.h"
#include "services/AppServices.h"
#include "ui/MainFrame.h"

namespace icd {

    App::App() = default;

    App::~App() = default;

    bool App::OnInit() {
        if (!wxApp::OnInit()) {
            return false;
        }
        services::initAppLogging("IroncladDefrag");
        services_ = std::make_unique<services::AppServices>(services::driveSourceFromEnvironment());
        auto* frame = new MainFrame(*services_); // wx owns top-level windows and deletes them on close.
        frame->Show();
        return true;
    }

    int App::OnExit() {
        services_.reset();
        services::shutdownAppLogging();
        return wxApp::OnExit();
    }

} // namespace icd
