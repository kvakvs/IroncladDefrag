#pragma once

#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

#include <optional>

class wxListView;
class wxStaticText;

namespace icd::ui {

    // Plan tab (plan.md section 11 and 9.2.2): the Fast, Moderate and Maximum levels side by side, with the
    // chosen one marked, and the warnings that apply to the plan.
    class PlanPage : public wxPanel {
    public:
        // Creates the page empty, with the hint shown.
        explicit PlanPage(wxWindow* parent);

        // Shows a plan; chosen is the level that Execute will run.
        void show(const services::PlanSummary& plan, services::EffortLevel chosen);

        // Empties the table and shows a hint about what to do next.
        void clear(const wxString& hint);

    private:
        // Sets the level column titles, marking the chosen one.
        void setTitles(std::optional<services::EffortLevel> chosen);

        wxStaticText* hint_ = nullptr; // What to do to get content; hidden while content is shown.
        wxListView* levels_ = nullptr; // Metric rows by level columns.
        wxStaticText* warnings_ = nullptr; // One warning per line.
    };

} // namespace icd::ui
