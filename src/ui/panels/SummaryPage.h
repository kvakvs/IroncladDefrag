#pragma once

#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

class wxListView;
class wxStaticText;

namespace icd::ui {

    // Summary tab (plan.md section 11): fragmentation and free-space figures next to per-class counts, bytes
    // and how much of each class already sits in its target zone.
    class SummaryPage : public wxPanel {
    public:
        // Creates the page empty, with the hint shown.
        explicit SummaryPage(wxWindow* parent);

        // Shows an analysis result.
        void show(const services::AnalysisSummary& summary);

        // Empties the tables and shows a hint about what to do next.
        void clear(const wxString& hint);

    private:
        wxStaticText* hint_ = nullptr; // What to do to get content; hidden while content is shown.
        wxListView* metrics_ = nullptr; // Volume-wide figures.
        wxListView* classes_ = nullptr; // Per-class rows.
    };

} // namespace icd::ui
