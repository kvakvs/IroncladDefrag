#pragma once

#include <wx/panel.h>

#include <cstdint>
#include <optional>

class wxGauge;
class wxListView;
class wxNotebook;
class wxStaticText;

namespace icd::ui {

    class PlanPage;
    class ProgressTab;
    class StrategyPage;
    class SummaryPage;

    // Tabs of the bottom notebook in display order (plan.md section 11); the same tabs stay for the whole
    // workflow, only their content changes.
    enum class BottomTab : std::uint8_t { Summary, Strategy, Plan, Progress, Log };

    // Bottom area of the main window: the tab notebook, the workflow buttons and the one status line of the
    // application. Status text lives only here (plan.md section 11), so there is no status bar.
    class BottomPanel : public wxPanel {
    public:
        // resourcesText is the detected resources and active caps line shown on the Progress tab.
        BottomPanel(wxWindow* parent, const wxString& resourcesText);

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

        // Adds a row to the Log tab (a skipped or failed file, or a line about a run), stamped with the current time.
        void addLogEntry(const wxString& file, const wxString& result, const wxString& details);

        // Empties the Log tab (a new run starts).
        void clearLog();

        // Shows Pause or Resume on the pause button.
        void setPauseLabel(bool paused);

    private:
        // Creates the Log tab (skipped and failed files with reasons).
        wxWindow* createLogPage();

        // Creates the row of workflow buttons, status line and gauge under the notebook.
        wxSizer* createStatusStrip();

        wxNotebook* notebook_ = nullptr; // The tabs; child window owned by wx.
        SummaryPage* summary_ = nullptr; // Summary tab; owned by the notebook.
        StrategyPage* strategy_ = nullptr; // Strategy tab; owned by the notebook.
        PlanPage* plan_ = nullptr; // Plan tab; owned by the notebook.
        ProgressTab* progress_ = nullptr; // Progress tab; owned by the notebook.
        wxListView* log_ = nullptr; // Log tab; owned by the notebook.
        wxStaticText* status_ = nullptr; // The single status line.
        wxGauge* gauge_ = nullptr; // Job progress next to the status line.
        wxStaticText* percent_ = nullptr; // Progress as a number, beside the gauge.
    };

} // namespace icd::ui
