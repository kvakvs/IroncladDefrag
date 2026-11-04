#pragma once

#include <wx/app.h>

#include <cstdint>
#include <memory>
#include <string>

namespace icd::services {
    class AppServices;
    enum class Appearance : std::uint8_t;
} // namespace icd::services

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
        // Light or dark windows from the saved setting (wx's Windows dark mode); must run before any window exists.
        void applyAppearance(services::Appearance appearance);

        std::unique_ptr<services::AppServices> services_; // Pool and job runner; outlives the main window.
        std::string initialDrive_; // Root given with --drive; empty for none.
    };

} // namespace icd

wxDECLARE_APP(icd::App);
