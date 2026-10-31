#pragma once

#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

#include <cstddef>
#include <functional>

class wxChoice;
class wxListView;
class wxRadioBox;

namespace icd::ui {

    // Strategy tab (plan.md section 11): preset picker, effort level and the preset's zone list. Editable only
    // when the workflow allows choosing a strategy; a change is reported so a built plan can be dropped.
    class StrategyPage : public wxPanel {
    public:
        // Creates the page with the default preset and the Fast level.
        explicit StrategyPage(wxWindow* parent);

        // Index into services::strategyPresets().
        [[nodiscard]] std::size_t preset() const;

        // Selected effort level.
        [[nodiscard]] services::EffortLevel effort() const;

        // Back to the default preset and the Fast level (Quick and a new drive use these).
        void reset();

        // Enables or disables the picker and the effort level.
        void setEditable(bool editable);

        // Sets the receiver of user changes (not of reset()).
        void setChangeHandler(std::function<void()> handler) { onChange_ = std::move(handler); }

    private:
        // Fills the zone list from the selected preset.
        void showZones();

        // Reacts to a user change of preset or effort.
        void onUserChange();

        wxChoice* preset_ = nullptr; // Preset picker.
        wxRadioBox* effort_ = nullptr; // Fast / Moderate / Maximum.
        wxListView* zones_ = nullptr; // Zones of the selected preset.
        std::function<void()> onChange_; // Receiver of user changes.
    };

} // namespace icd::ui
