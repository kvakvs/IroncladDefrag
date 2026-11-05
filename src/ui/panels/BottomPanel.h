#pragma once

#include <wx/panel.h>

#include <cstdint>
#include <optional>

class wxGauge;
class wxNotebook;
class wxStaticText;

namespace icd::core {
    class Executor;
}

namespace icd::ui {

    class FilePage;
    class LogTab;
    class MapColorsPage;
    class PlanPage;
    class ProgressTab;
    class StrategyPage;
    class SummaryPage;

    // Tabs of the bottom notebook in display order (plan.md section 11); the same tabs stay for the whole
    // workflow, only their content changes.
    enum class BottomTab : std::uint8_t { Summary, Strategy, Plan, Progress, Log, MapColors, File };

    // Bottom area of the main window: the tab notebook, the workflow buttons and the one status line of the
    // application. Status text lives only here (plan.md section 11), so there is no status bar.
    class BottomPanel : public wxPanel {
    public:
        // resourcesText is the detected resources and active caps line shown on the Progress tab; the Map colors
        // tab's legend counts on executor.
        BottomPanel(wxWindow* parent, const wxString& resourcesText, core::Executor& executor);

        // Brings a tab to the front, e.g. Strategy after an analysis.
        void showTab(BottomTab tab);

        // Sets the application's single status line.
        void setStatus(const wxString& text);

        // Shows job progress in [0, 1] next to the status line; nullopt clears it.
        void setProgress(std::optional<double> fraction);


        // The Summary tab.
        [[nodiscard]] SummaryPage& summary() const { return *summary_; }

        // The Strategy tab.
        [[nodiscard]] StrategyPage& strategy() const { return *strategy_; }

        // The Plan tab.
        [[nodiscard]] PlanPage& plan() const { return *plan_; }

        // The Progress tab.
        [[nodiscard]] ProgressTab& progress() const { return *progress_; }

        // The Log tab.
        [[nodiscard]] LogTab& log() const { return *log_; }

        // The Map colors tab (color mode switch and legend).
        [[nodiscard]] MapColorsPage& mapColors() const { return *mapColors_; }

        // The File tab (selected file, search, file actions).
        [[nodiscard]] FilePage& file() const { return *file_; }

        // Whether a tab is in front.
        [[nodiscard]] bool tabShown(BottomTab tab) const;

        // Shows Pause or Resume on the pause button.
        void setPauseLabel(bool paused);

        // Shows or hides the button of a command that appears only while it can run (WorkflowCommand::onlyWhenEnabled).
        void showCommand(int id, bool shown);

    private:
        // Creates the row of workflow buttons, status line and gauge under the notebook.
        wxSizer* createStatusStrip();

        wxNotebook* notebook_ = nullptr; // The tabs; child window owned by wx.
        SummaryPage* summary_ = nullptr; // Summary tab; owned by the notebook.
        StrategyPage* strategy_ = nullptr; // Strategy tab; owned by the notebook.
        PlanPage* plan_ = nullptr; // Plan tab; owned by the notebook.
        ProgressTab* progress_ = nullptr; // Progress tab; owned by the notebook.
        LogTab* log_ = nullptr; // Log tab; owned by the notebook.
        MapColorsPage* mapColors_ = nullptr; // Map colors tab; child owned by wx.
        FilePage* file_ = nullptr; // File tab; child owned by wx.
        wxStaticText* status_ = nullptr; // The single status line.
        wxGauge* gauge_ = nullptr; // Job progress next to the status line.
        wxStaticText* percent_ = nullptr; // Progress as a number, beside the gauge.
    };

} // namespace icd::ui
