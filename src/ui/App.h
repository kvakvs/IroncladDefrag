#pragma once

#include <wx/app.h>

namespace icd {

    // wxWidgets application object: creates the main window and nothing else.
    class App : public wxApp {
    public:
        // Builds and shows the main window; returning false aborts start-up.
        bool OnInit() override;
    };

} // namespace icd

wxDECLARE_APP(icd::App);
