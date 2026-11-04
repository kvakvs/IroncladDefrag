#pragma once

#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

#include <functional>
#include <optional>
#include <utility>

class wxCheckBox;
class wxListView;
class wxStaticText;

namespace icd::ui {

    // Plan tab (plan.md section 11 and 9.2.2): the Fast, Moderate and Maximum levels side by side, with the
    // chosen one marked, and the warnings that apply to the plan. On a drive with shadow copies it also counts the
    // unaligned moves of each level and offers to block them (plan.md 9.4).
    class PlanPage : public wxPanel {
    public:
        // Creates the page empty, with the hint shown.
        explicit PlanPage(wxWindow* parent);

        // Shows a plan; chosen is the level that Execute will run.
        void show(const services::PlanSummary& plan, services::EffortLevel chosen);

        // Empties the table and shows a hint about what to do next.
        void clear(const wxString& hint);

        // Shows the VSS option for a drive with shadow copies and hides it otherwise.
        void setShadowCopies(bool present);

        // Whether "Block unaligned moves" is shown and ticked: the next plan leaves unaligned moves out.
        [[nodiscard]] bool blockUnaligned() const;

        // Ticks or clears "Block unaligned moves" (the VSS warning's choice); the handler is not called.
        void setBlockUnaligned(bool block);

        // Called when the user changes "Block unaligned moves" (a built plan no longer matches it).
        void setVssHandler(std::function<void()> handler) { vssChanged_ = std::move(handler); }

    private:
        // Sets the level column titles, marking the chosen one.
        void setTitles(std::optional<services::EffortLevel> chosen);

        wxStaticText* hint_ = nullptr; // What to do to get content; hidden while content is shown.
        wxListView* levels_ = nullptr; // Metric rows by level columns.
        wxStaticText* warnings_ = nullptr; // One warning per line.
        wxCheckBox* blockUnaligned_ = nullptr; // The VSS option; shown only with shadow copies.
        std::function<void()> vssChanged_; // The option changed.
    };

} // namespace icd::ui
