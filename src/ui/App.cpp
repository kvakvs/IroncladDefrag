#include "ui/App.h"

#include "services/AppLogging.h"
#include "ui/MainFrame.h"

namespace icd {

    bool App::OnInit() {
        if (!wxApp::OnInit()) {
            return false;
        }
        services::initAppLogging("IroncladDefrag");
        auto* frame = new MainFrame(); // wx owns top-level windows and deletes them on close.
        frame->Show();
        return true;
    }

    int App::OnExit() {
        services::shutdownAppLogging();
        return wxApp::OnExit();
    }

} // namespace icd
