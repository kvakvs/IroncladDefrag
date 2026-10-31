#pragma once

#include "services/job/JobRunner.h"

#include <wx/frame.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace icd::services {
    class AppServices;
}

namespace icd::ui {
    class JobEventBridge;
}

namespace icd {

    // Top-level window. For now: menus, a status bar and the Debug menu's fake job; panels, the drive list and
    // the map arrive in Phase 1 and 2.
    class MainFrame : public wxFrame {
    public:
        // Menu command ids; public so scripted checks can post them as WM_COMMAND (wxID_HIGHEST is 6000).
        enum CommandId : std::uint16_t {
            ID_RUN_FAKE_JOB = wxID_HIGHEST + 1, // Debug > Run fake parallel job.
            ID_CANCEL_JOB, // Debug > Cancel job.
        };

        // Creates the window; services must outlive it.
        explicit MainFrame(services::AppServices& services);

        // Detaches the job event bridge so late events from running jobs are dropped.
        ~MainFrame() override;

        MainFrame(const MainFrame&) = delete;
        MainFrame& operator=(const MainFrame&) = delete;

    private:
        // Creates the File and Debug menus.
        void buildMenus();

        // Enables Run/Cancel from activeJob_. Set explicitly on every change: wxMSW refreshes
        // wxEVT_UPDATE_UI menu state only when a menu opens, and ignores accelerators of disabled items.
        void updateJobCommands();

        // Starts the fake parallel job (Debug menu).
        void onRunFakeJob(wxCommandEvent& event);

        // Requests cancellation of the running job.
        void onCancelJob(wxCommandEvent& event);

        // Shows throttled job progress in the status bar.
        void onJobProgress(wxThreadEvent& event);

        // Shows how the job ended and re-enables the menu.
        void onJobFinished(wxThreadEvent& event);

        // Cancels a running job before the window goes away.
        void onClose(wxCloseEvent& event);

        services::AppServices& services_; // Pool and job runner.
        std::shared_ptr<ui::JobEventBridge> bridge_; // Delivers job events to this window.
        std::optional<services::JobId> activeJob_; // The running job started from this window, if any.
    };

} // namespace icd
