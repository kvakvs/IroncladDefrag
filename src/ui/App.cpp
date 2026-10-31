#include "ui/App.h"

#include "ui/MainFrame.h"

namespace icd {

    bool App::OnInit() {
        if (!wxApp::OnInit()) {
            return false;
        }
        auto* frame = new MainFrame(); // wx owns top-level windows and deletes them on close.
        frame->Show();
        return true;
    }

} // namespace icd
