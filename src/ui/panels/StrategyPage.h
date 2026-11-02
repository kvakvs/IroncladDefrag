#pragma once

#include "core/strategy/ZoneScript.h"
#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

class wxChoice;
class wxGrid;
class wxGridEvent;
class wxRadioBox;
class wxStaticText;

namespace icd::ui {

    // Strategy tab (plan.md section 11): preset picker, effort level and the preset's zones as an editable table
    // (classes, action, anchor, order, order at Maximum, fragment_ok and the gap after each zone, which is where the
    // churn gap is sized, plan.md 8.3). Edits change the strategy the next plan uses; problems show under the table.
    // Editable only when the workflow allows choosing a strategy; every change is reported so a built plan is dropped.
    class StrategyPage : public wxPanel {
    public:
        // Creates the page with the default preset and the Fast level.
        explicit StrategyPage(wxWindow* parent);

        // Index into core::strategy::builtinPresets().
        [[nodiscard]] std::size_t preset() const;

        // Selected effort level.
        [[nodiscard]] services::EffortLevel effort() const;

        // The strategy as shown, with the user's edits.
        [[nodiscard]] const core::strategy::ZoneScript& script() const { return script_; }

        // What is wrong with the edited strategy, one message each; empty when it can be planned.
        [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

        // Back to the default preset and the Fast level (Quick and a drive without a remembered choice use these).
        void reset();

        // Shows a preset (index into builtinPresets(), out of range = default) and an effort level without reporting a
        // change, to restore a drive's remembered choice; edits are dropped.
        void select(std::size_t preset, services::EffortLevel effort);

        // Enables or disables the picker, the effort level and the table.
        void setEditable(bool editable);

        // Sets the receiver of user changes (not of reset() or select()).
        void setChangeHandler(std::function<void()> handler) { onChange_ = std::move(handler); }

    private:
        // Loads the selected preset into the script and the table.
        void loadPreset();

        // Fills the table from the script.
        void showZones();

        // Applies an edited cell to the script; false (and a problem) when the text cannot be read.
        bool applyCell(int row, int column);

        // Validates the script and shows the problems.
        void validate();

        // Reacts to a user change of preset or effort.
        void onUserChange();

        // Reacts to an edited cell.
        void onCellChanged(wxGridEvent& event);

        wxChoice* preset_ = nullptr; // Preset picker.
        wxRadioBox* effort_ = nullptr; // Fast / Moderate / Maximum.
        wxStaticText* description_ = nullptr; // The preset's description.
        wxGrid* zones_ = nullptr; // Zone table.
        wxStaticText* problemsText_ = nullptr; // Validation problems, one per line.
        core::strategy::ZoneScript script_; // The strategy with edits.
        std::vector<std::string> cellProblems_; // Cells whose text could not be read.
        std::vector<std::string> problems_; // Everything wrong, for the workflow.
        std::function<void()> onChange_; // Receiver of user changes.
    };

} // namespace icd::ui
