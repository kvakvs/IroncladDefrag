#pragma once

#include "services/job/JobRunner.h"

#include <wx/frame.h>

#include <memory>
#include <optional>

class wxSplitterWindow;

namespace icd::services {
    class AppServices;
}

namespace icd::ui {
    class BottomPanel;
    class DrivesPanel;
    class JobEventBridge;
    class MapPlaceholder;
} // namespace icd::ui

namespace icd {

    // Top-level window (plan.md section 11): the menu, then from top to bottom the all-disks panel, the drive
    // map and the bottom panel with tabs, workflow buttons and the single status line.
    class MainFrame : public wxFrame {
    public:
        // Creates the window; services must outlive it.
        explicit MainFrame(services::AppServices& services);

        // Detaches the job event bridge so late events from running jobs are dropped.
        ~MainFrame() override;

        MainFrame(const MainFrame&) = delete;
        MainFrame& operator=(const MainFrame&) = delete;

    private:
        // Creates the panels, the map/bottom splitter and the sizers.
        void buildLayout();

        // Places the splitter sash once the window has its real size (the bottom panel keeps its height).
        void placeSash();

        // Binds the commands that work in this phase (menus and buttons share ids).
        void bindCommands();

        // Sets the enabled state of every state-dependent command.
        void updateCommands();

        // Whether a command can run now.
        [[nodiscard]] bool commandEnabled(int id) const;

        // Shows the selected drive on the map and refreshes the commands that depend on it.
        void onDriveSelected();

        // Reloads the drive list from the provider (Drive > Refresh drive list).
        void onRefreshDrives(wxCommandEvent& event);

        // TRIM for the selected SSD (stub until Phase 3).
        void onTrim(wxCommandEvent& event);

        // Starts the fake parallel job (Debug menu).
        void onRunFakeJob(wxCommandEvent& event);

        // Requests cancellation of the running job.
        void onCancel(wxCommandEvent& event);

        // Shows throttled job progress on the status line and the Progress tab.
        void onJobProgress(wxThreadEvent& event);

        // Shows how the job ended and re-enables commands.
        void onJobFinished(wxThreadEvent& event);

        // Cancels a running job before the window goes away.
        void onClose(wxCloseEvent& event);

        services::AppServices& services_; // Pool and job runner.
        std::shared_ptr<ui::JobEventBridge> bridge_; // Delivers job events to this window.
        std::optional<services::JobId> activeJob_; // The running job started from this window, if any.
        wxSplitterWindow* splitter_ = nullptr; // Map above, bottom panel below; child owned by wx.
        ui::DrivesPanel* drives_ = nullptr; // All-disks panel; child owned by wx.
        ui::MapPlaceholder* map_ = nullptr; // Map area; child owned by wx.
        ui::BottomPanel* bottom_ = nullptr; // Tabs, buttons and status line; child owned by wx.
    };

} // namespace icd
