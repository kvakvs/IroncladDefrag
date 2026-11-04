#pragma once

#include "core/strategy/ZoneScript.h"
#include "services/workflow/FakeResults.h"

#include <wx/panel.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class wxButton;
class wxChoice;
class wxSizer;
class wxGrid;
class wxGridEvent;
class wxRadioBox;
class wxStaticText;

namespace icd::ui {

    // The strategy library buttons of the Strategy tab; the page only reports them, the owner of the library acts.
    enum class StrategyCommand : std::uint8_t {
        SaveAs, // Save the shown strategy, with its edits, as a custom strategy.
        Delete, // Delete the selected custom strategy.
        Import, // Add a custom strategy from a zone script file.
        Export, // Write the shown strategy, with its edits, to a zone script file.
    };

    // Strategy tab (plan.md section 11): preset picker (built-in and custom strategies), effort level and the preset's
    // zones as an editable table (classes, action, anchor, order, order at Maximum, fragment_ok and the gap after each
    // zone, which is where the churn gap is sized, plan.md 8.3). Edits change the strategy the next plan uses; problems
    // show under the table; Save as keeps them as a custom strategy. Editable only when the workflow allows choosing a
    // strategy; every change is reported so a built plan is dropped.
    class StrategyPage : public wxPanel {
    public:
        // Creates the page with the built-in presets, the default one shown at the Fast level.
        explicit StrategyPage(wxWindow* parent);

        // Replaces the strategies the picker offers (built-in first, then custom ones) and shows the first one; the
        // owner selects the one it wants next.
        void setStrategies(std::vector<core::strategy::ZoneScript> strategies);

        // Index of the selected strategy in the picker.
        [[nodiscard]] std::size_t preset() const;

        // Id of the selected strategy (DriveStrategy::preset).
        [[nodiscard]] const std::string& presetId() const;

        // Whether the selected strategy is a custom one (Delete applies).
        [[nodiscard]] bool customSelected() const;

        // Selected effort level.
        [[nodiscard]] services::EffortLevel effort() const;

        // The strategy as shown, with the user's edits.
        [[nodiscard]] const core::strategy::ZoneScript& script() const { return script_; }

        // What is wrong with the edited strategy, one message each; empty when it can be planned.
        [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

        // Back to the default preset and the Fast level (Quick and a drive without a remembered choice use these).
        void reset();

        // Shows a strategy by id (unknown ids: the default preset) and an effort level without reporting a change, to
        // restore a drive's remembered choice; edits are dropped.
        void select(std::string_view id, services::EffortLevel effort);

        // Enables or disables the picker, the effort level and the table.
        void setEditable(bool editable);

        // Sets the receiver of user changes (not of reset() or select()).
        void setChangeHandler(std::function<void()> handler) { onChange_ = std::move(handler); }

        // Sets the receiver of the library buttons.
        void setCommandHandler(std::function<void(StrategyCommand)> handler) { onCommand_ = std::move(handler); }

    private:
        // Creates the library buttons, in a row.
        wxSizer* createButtons();

        // Enables the library buttons for the editable state, the selection and the problems.
        void refreshButtons();

        // Shows strategies_[index] (out of range: the first) and the effort level, dropping edits.
        void selectIndex(std::size_t index, services::EffortLevel effort);

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
        wxButton* saveAs_ = nullptr; // Save as custom strategy.
        wxButton* delete_ = nullptr; // Delete the selected custom strategy.
        wxButton* import_ = nullptr; // Import a zone script file.
        wxButton* export_ = nullptr; // Export the shown strategy.
        std::vector<core::strategy::ZoneScript> strategies_; // What the picker offers, in picker order.
        bool editable_ = false; // The workflow allows choosing a strategy now.
        core::strategy::ZoneScript script_; // The strategy with edits.
        std::vector<std::string> cellProblems_; // Cells whose text could not be read.
        std::vector<std::string> problems_; // Everything wrong, for the workflow.
        std::function<void()> onChange_; // Receiver of user changes.
        std::function<void(StrategyCommand)> onCommand_; // Receiver of the library buttons.
    };

} // namespace icd::ui
