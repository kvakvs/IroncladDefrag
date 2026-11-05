#pragma once

#include "services/execute/ExecutionLog.h"
#include "services/workflow/FakeResults.h"

#include <wx/dialog.h>

#include <cstdint>
#include <string>
#include <utility>
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

    // What the user is asked to confirm before an unfinished run continues (T112).
    struct ResumeConfirmation {
        std::string drive; // "D: Data".
        std::string strategy; // Name of the strategy the run was planned with.
        services::UnfinishedRun run; // The run and what is left of it.
        bool analyzeFirst = false; // No map is shown: the drive is analyzed before the moves continue.
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

        // Builds the dialog for resuming an unfinished run (T112).
        ConfirmExecuteDialog(wxWindow* parent, const ResumeConfirmation& what);

        // The options chosen (read after ShowModal() returned wxID_OK).
        [[nodiscard]] ExecuteChoices choices() const;

    private:
        // A figure: its label and value.
        using Figure = std::pair<std::string, std::string>;

        // What one kind of confirmation shows; both constructors fill it.
        struct Content {
            std::string question; // The heading.
            std::vector<Figure> figures; // Label and value rows.
            std::vector<std::string> warnings; // Shown with a warning sign.
            wxString note; // What keeps the volume safe.
            wxString okLabel; // The button that starts the moves.
        };

        // Lays out the dialog from its content.
        void build(const Content& content);

        // The figures as a label and value grid.
        wxSizer* createFigures(const std::vector<Figure>& figures);

        // The priority and throughput options.
        wxSizer* createOptions();

        // The button that starts the moves, and Cancel.
        wxSizer* createButtons(const wxString& okLabel);

        wxCheckBox* lowPriority_ = nullptr; // Low I/O priority; child owned by wx.
        wxSpinCtrl* limit_ = nullptr; // Throughput limit in MB/s; child owned by wx.
    };

} // namespace icd::ui
