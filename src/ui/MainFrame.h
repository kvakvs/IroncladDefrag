#pragma once

#include <wx/frame.h>

#include <memory>
#include <string>

class wxSplitterWindow;

namespace icd::services {
    class AppServices;
}

namespace icd::ui {
    class BottomPanel;
    class DrivesPanel;
    class MapPanel;
    class WorkflowBinding;
} // namespace icd::ui

namespace icd {

    // Top-level window (plan.md section 11): the menu, then from top to bottom the all-disks panel, the drive
    // map and the bottom panel with tabs, workflow buttons and the single status line. It only composes the
    // widgets; ui::WorkflowBinding drives them.
    class MainFrame : public wxFrame {
    public:
        // Creates the window and selects the drive with root initialDrive (none when empty); services must outlive
        // it.
        MainFrame(services::AppServices& services, const std::string& initialDrive);

        // Defined in the .cpp, where WorkflowBinding is complete.
        ~MainFrame() override;

        MainFrame(const MainFrame&) = delete;
        MainFrame& operator=(const MainFrame&) = delete;

    private:
        // Creates the panels, the map/bottom splitter and the sizers.
        void buildLayout();

        // Places the splitter sash once the window has its real size (the bottom panel keeps its height).
        void placeSash();

        // Stops the workflow's running job before the window goes away.
        void onClose(wxCloseEvent& event);

        services::AppServices& services_; // Caps for the resources line; passed to the binding.
        wxSplitterWindow* splitter_ = nullptr; // Map above, bottom panel below; child owned by wx.
        ui::DrivesPanel* drives_ = nullptr; // All-disks panel; child owned by wx.
        ui::MapPanel* map_ = nullptr; // Map area; child owned by wx.
        ui::BottomPanel* bottom_ = nullptr; // Tabs, buttons and status line; child owned by wx.
        std::unique_ptr<ui::WorkflowBinding> binding_; // Commands, jobs and widget state.
    };

} // namespace icd
