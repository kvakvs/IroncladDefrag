#pragma once

#include <wx/app.h>

#include <memory>
#include <string>

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

        // Adds --drive, which selects a drive at start (the restart as administrator passes it on).
        void OnInitCmdLine(wxCmdLineParser& parser) override;

        // Takes --drive.
        bool OnCmdLineParsed(wxCmdLineParser& parser) override;

    private:
        std::unique_ptr<services::AppServices> services_; // Pool and job runner; outlives the main window.
        std::string initialDrive_; // Root given with --drive; empty for none.
    };

} // namespace icd

wxDECLARE_APP(icd::App);
