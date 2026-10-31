#pragma once

#include <wx/panel.h>

#include <array>
#include <cstdint>
#include <optional>

class wxGauge;
class wxNotebook;
class wxStaticText;

namespace icd::ui {

    // Tabs of the bottom notebook in display order (plan.md section 11); the same tabs stay for the whole
    // workflow, only their content changes.
    enum class BottomTab : std::uint8_t { Summary, Strategy, Plan, Progress, Log };

    // Text for the Progress tab's detail fields, already formatted by the caller.
    struct ProgressDetails {
        wxString stage; // Current stage name.
        wxString done; // "1,234 of 10,000".
        wxString elapsed; // Time since the job started.
        wxString throughput; // Disk throughput; "-" until real disk work exists.
        wxString eta; // Estimated time left; "-" when unknown.
        wxString currentFile; // File being processed, if the job reports one.
    };

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

        // Fills the Progress tab; default-constructed details clear it.
        void setProgressDetails(const ProgressDetails& details);

        // The page window of a tab, for later tasks that replace placeholder content.
        [[nodiscard]] wxWindow* page(BottomTab tab) const;

    private:
        // Number of detail rows on the Progress tab (ProgressDetails fields).
        static constexpr std::size_t detailRows = 6;

        // Creates the Progress tab with its detail grid and the resources line.
        wxWindow* createProgressPage(const wxString& resourcesText);

        // Creates the Log tab (skipped and failed files with reasons).
        wxWindow* createLogPage();

        // Creates the row of workflow buttons, status line and gauge under the notebook.
        wxSizer* createStatusStrip();

        wxNotebook* notebook_ = nullptr; // The tabs; child window owned by wx.
        wxStaticText* status_ = nullptr; // The single status line.
        wxGauge* gauge_ = nullptr; // Job progress next to the status line.
        wxStaticText* percent_ = nullptr; // Progress as a number, beside the gauge.
        std::array<wxStaticText*, detailRows> details_{}; // Value labels of the Progress tab, in field order.
    };

} // namespace icd::ui
