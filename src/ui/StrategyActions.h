#pragma once

#include "ui/panels/StrategyPage.h"

#include <string_view>

class wxWindow;

namespace icd::services {
    class StrategyLibrary;
}

namespace icd::ui {

    // Carries out the Strategy tab's library buttons (plan.md 13 Phase 10: custom strategies, import and export): asks
    // for names, files and confirmations, changes the library and shows it in the page's picker. Errors are shown in a
    // message box; the library is unchanged by a failed step. Kept out of WorkflowBinding, which only learns that the
    // selection changed.
    class StrategyActions {
    public:
        // Acts on page and library with dialogs over parent; all three outlive it.
        StrategyActions(wxWindow& parent, StrategyPage& page, services::StrategyLibrary& library);

        // Fills the picker from the library and selects id (unknown: the default preset) at the shown effort level.
        void showLibrary(std::string_view id);

        // Runs one button. True when the page now shows another strategy, which the caller remembers for the drive.
        bool run(StrategyCommand command);

    private:
        // Save as: names the shown strategy (with edits) and stores it; replacing another of that name asks first.
        bool saveAs();

        // Delete: removes the selected custom strategy after a confirmation; the default preset is shown after it.
        bool remove();

        // Import: adds a zone script file and shows it.
        bool importFile();

        // Export: writes the shown strategy (with edits) to a file.
        void exportFile();

        // Shows a failed step.
        void showError(const wxString& title, const char* message);

        wxWindow& parent_; // Owner of the dialogs.
        StrategyPage& page_; // The Strategy tab.
        services::StrategyLibrary& library_; // Built-in and custom strategies.
    };

} // namespace icd::ui
