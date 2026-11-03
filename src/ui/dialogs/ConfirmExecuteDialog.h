#pragma once

#include "services/workflow/FakeResults.h"

#include <wx/dialog.h>

#include <cstdint>
#include <string>
#include <vector>

class wxCheckBox;
class wxSpinCtrl;

namespace icd::ui {

    // What the user is asked to confirm before files move (plan.md 2: execution needs explicit confirmation).
    struct ExecuteConfirmation {
        std::string drive; // "D: Data".
        std::string strategy; // Strategy name.
        services::PlanLevel level; // Figures of the chosen level.
        std::vector<std::string> warnings; // The plan's warnings (VSS, skipped files, validation).
        bool simulated = false; // Fake drive list: the run is simulated and nothing on a disk changes.
    };

    // How the user wants the moves to run (plan.md 10.4: optional low I/O priority and a throughput limit).
    struct ExecuteChoices {
        bool lowPriority = false; // Background I/O priority, so other programs stay responsive.
        std::uint64_t limitMbPerSecond = 0; // Throughput limit in MB/s; 0: none.
    };

    // Asks for confirmation of an execution: the plan's figures and warnings, what keeps the volume safe, and the
    // priority and throughput options. Cancel is the default button, so Enter alone never starts moving files.
    class ConfirmExecuteDialog : public wxDialog {
    public:
        // Builds the dialog for one plan level.
        ConfirmExecuteDialog(wxWindow* parent, const ExecuteConfirmation& what);

        // The options chosen (read after ShowModal() returned wxID_OK).
        [[nodiscard]] ExecuteChoices choices() const;

    private:
        // The figures of the level as a label and value grid.
        wxSizer* createFigures(const services::PlanLevel& level);

        // The priority and throughput options.
        wxSizer* createOptions();

        // The Execute and Cancel buttons.
        wxSizer* createButtons();

        wxCheckBox* lowPriority_ = nullptr; // Low I/O priority; child owned by wx.
        wxSpinCtrl* limit_ = nullptr; // Throughput limit in MB/s; child owned by wx.
    };

} // namespace icd::ui
