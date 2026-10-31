#pragma once

#include <wx/dialog.h>

namespace icd::services {
    class AppServices;
}

namespace icd::ui {

    // Help > About: version and build, plus the detected resources and the caps in force (plan.md 6.6), so a
    // user can see and report what the app is working with.
    class AboutDialog : public wxDialog {
    public:
        // Reads resources and caps from services; detection runs again for current free memory.
        AboutDialog(wxWindow* parent, const services::AppServices& services);
    };

} // namespace icd::ui
