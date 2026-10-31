#pragma once

#include <wx/app.h>

#include <memory>

namespace icd::services {
    class AppServices;
}

namespace icd {

    // wxWidgets application object: starts logging and the service layer, then creates the main window.
    class App : public wxApp {
    public:
        // Nothing to do before OnInit(); declared so the destructor can live in the .cpp.
        App();

        // Defined in the .cpp, where AppServices is complete.
        ~App() override;

        // Builds and shows the main window; returning false aborts start-up.
        bool OnInit() override;

        // Stops the services (waiting for jobs) and writes the shutdown log line after the last window closed.
        int OnExit() override;

    private:
        std::unique_ptr<services::AppServices> services_; // Pool and job runner; outlives the main window.
    };

} // namespace icd

wxDECLARE_APP(icd::App);
