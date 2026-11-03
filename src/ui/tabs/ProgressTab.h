#pragma once

#include <wx/panel.h>

#include <array>
#include <cstddef>

class wxStaticText;

namespace icd::ui {

    // Text for the Progress tab's fields (plan.md 11: stage, disk throughput, worker utilisation, ETA, current file),
    // already formatted by the caller; an empty field shows "-".
    struct ProgressDetails {
        wxString stage; // Current stage name.
        wxString done; // "1,234 of 10,000".
        wxString elapsed; // Time since the job started.
        wxString eta; // Estimated time left.
        wxString throughput; // Disk throughput (execution: bytes moved per second).
        wxString workers; // Share of the worker threads busy, recently.
        wxString currentFile; // File being processed, if the job reports one.
        wxString moves; // Execution: moves done, skipped, partly moved, dropped.
    };

    // The Progress tab: what the running job is doing, its speed and time left, and the resources and caps in force
    // (plan.md 6.6). The single status line stays under the notebook; this tab holds the details.
    class ProgressTab : public wxPanel {
    public:
        // resourcesText is the detected resources and active caps line.
        ProgressTab(wxWindow* parent, const wxString& resourcesText);

        // Shows the fields; default-constructed details clear them.
        void show(const ProgressDetails& details);

    private:
        // Number of fields (ProgressDetails members), shown two per row.
        static constexpr std::size_t rows = 8;

        std::array<wxStaticText*, rows> values_{}; // Value labels in field order; children owned by wx.
    };

} // namespace icd::ui
