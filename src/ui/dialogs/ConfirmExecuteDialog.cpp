#include "ui/dialogs/ConfirmExecuteDialog.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/Duration.h"
#include "ui/AppIcon.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string>
#include <utility>

namespace icd::ui {

    namespace {

        // Highest throughput limit offered, in MB/s (faster than any hard drive).
        constexpr int maxLimit = 10000;

        // Widest a text line of the dialog wraps at, in DIP.
        constexpr int wrapWidth = 520;

        // What keeps the volume safe, in the words of plan.md 10.3-10.4.
        constexpr const char* safetyNote =
            "Before anything moves, the plan is checked against the volume again: files changed since the plan are "
            "left alone. Each move is a Windows file-system call that leaves the file intact if it stops, and every "
            "move is logged, so Pause and Cancel are safe at any time.";

        // A grey explanatory line, wrapped.
        wxStaticText* note(wxWindow* parent, const wxString& text) {
            auto* label = new wxStaticText(parent, wxID_ANY, text);
            label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            label->Wrap(parent->FromDIP(wrapWidth));
            return label;
        }

        // The warning lines, one per warning.
        wxString warningText(const std::vector<std::string>& warnings) {
            wxString text;
            for (const std::string& warning : warnings) {
                text += (text.empty() ? "" : "\n") + wxString::FromUTF8("\xE2\x9A\xA0 " + warning);
            }
            return text;
        }

        // What keeps the volume safe when an unfinished run continues (plan.md 10.4).
        constexpr const char* resumeNote =
            "Before anything moves, the volume is read again: moves whose files changed since are dropped, and a move "
            "that was cut off continues from where it stopped. The run goes on in the same execution log, so Pause and "
            "Cancel are safe at any time and a cancelled run can be resumed again.";

        // The figures of a plan level.
        std::vector<std::pair<std::string, std::string>> levelFigures(const services::PlanLevel& level) {
            return {
                {"Files to move:", core::formatCount(level.filesToMove)},
                {"Data to move:",
                 std::format("{} (least possible {})", core::formatBytes(level.bytesToMove),
                             core::formatBytes(level.lowerBoundBytes))},
                {"Estimated time:", core::formatDuration(level.estimate)},
                {"Fragmentation after:", std::format("{:.1f}%", level.fragmentationAfter)},
                {"In target zone after:", std::format("{:.0f}%", level.zoneFitAfter)},
            };
        }

        // How the run stopped, and when.
        std::string stoppedText(const services::UnfinishedRun& run) {
            const char* how = run.cancelled ? "cancelled" : "interrupted (the program ended during the run)";
            if (run.stoppedAt == std::chrono::system_clock::time_point{}) {
                return how;
            }
            const auto age =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - run.stoppedAt);
            return std::format("{}, {}", how, core::formatAge(age));
        }

        // The figures of an unfinished run.
        std::vector<std::pair<std::string, std::string>> resumeFigures(const ResumeConfirmation& what) {
            const services::UnfinishedRun& run = what.run;
            return {
                {"Planned with:",
                 std::format("{} at {}", what.strategy,
                             services::effortName(static_cast<services::EffortLevel>(run.effort)))},
                {"Stopped:", stoppedText(run)},
                {"Moves left:",
                 std::format("{} of {}", core::formatCount(run.movesLeft), core::formatCount(run.moves))},
                {"Data left:", std::format("up to {}", core::formatBytes(run.bytesLeft))},
                {"Execution log:", run.log.filename().string()},
            };
        }

    } // namespace

    ConfirmExecuteDialog::ConfirmExecuteDialog(wxWindow* parent, const ExecuteConfirmation& what) :
        wxDialog(parent, wxID_ANY, "Execute plan") {
        Content content;
        content.question = std::format("Execute the {} plan ({}) on {}?", services::effortName(what.level.level),
                                       what.strategy, what.drive);
        content.figures = levelFigures(what.level);
        content.warnings = what.warnings;
        content.note = what.simulated
            ? wxString("This is the fake drive list: the run is simulated and nothing on a disk changes.")
            : wxString(safetyNote);
        content.okLabel = "&Execute";
        content.offerHeadOrder = true;
        build(content);
    }

    ConfirmExecuteDialog::ConfirmExecuteDialog(wxWindow* parent, const ResumeConfirmation& what) :
        wxDialog(parent, wxID_ANY, "Resume interrupted run") {
        Content content;
        content.question = std::format("Resume the interrupted run on {}?", what.drive);
        content.figures = resumeFigures(what);
        if (what.analyzeFirst) {
            content.warnings.emplace_back("The drive is analyzed first, so the map can follow the moves.");
        }
        content.note = resumeNote;
        content.okLabel = "&Resume";
        build(content);
    }

    void ConfirmExecuteDialog::build(const Content& content) {
        SetIcons(appIcons());
        auto* heading = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(content.question));
        heading->SetFont(heading->GetFont().Scaled(1.2F).Bold());

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(16);
        sizer->Add(heading, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, border));
        sizer->Add(createFigures(content.figures), wxSizerFlags().Border(wxALL, border));
        if (!content.warnings.empty()) {
            auto* warnings = new wxStaticText(this, wxID_ANY, warningText(content.warnings));
            warnings->Wrap(FromDIP(wrapWidth));
            sizer->Add(warnings, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        }
        sizer->Add(note(this, content.note), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(createOptions(content.offerHeadOrder), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(createButtons(content.okLabel), wxSizerFlags().Right().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

    wxSizer* ConfirmExecuteDialog::createFigures(const std::vector<Figure>& figures) {
        auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(16), FromDIP(4)));
        for (const auto& [label, value] : figures) {
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label)));
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(value)));
        }
        return grid;
    }

    wxSizer* ConfirmExecuteDialog::createOptions(bool offerHeadOrder) {
        auto* options = new wxBoxSizer(wxVERTICAL);
        if (offerHeadOrder) {
            headOrder_ =
                new wxCheckBox(this, wxID_ANY, "&Order moves by disk head position (less seeking on hard drives)");
            headOrder_->SetValue(true);
            headOrder_->SetToolTip("Each next move starts reading near where the last one stopped writing. Moves that "
                                   "touch the same clusters keep their planned order, so the result is the same.");
            options->Add(headOrder_, wxSizerFlags().Border(wxBOTTOM, FromDIP(6)));
        }
        lowPriority_ = new wxCheckBox(this, wxID_ANY, "&Low I/O priority (slower; other programs stay responsive)");
        limit_ = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 0,
                                maxLimit, 0);
        auto* limitRow = new wxBoxSizer(wxHORIZONTAL);
        limitRow->Add(new wxStaticText(this, wxID_ANY, "&Throughput limit:"), wxSizerFlags().CenterVertical());
        limitRow->Add(limit_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        limitRow->Add(new wxStaticText(this, wxID_ANY, "MB/s (0: no limit)"), wxSizerFlags().CenterVertical());
        options->Add(lowPriority_, wxSizerFlags().Border(wxBOTTOM, FromDIP(6)));
        options->Add(limitRow);
        return options;
    }

    wxSizer* ConfirmExecuteDialog::createButtons(const wxString& okLabel) {
        auto* execute = new wxButton(this, wxID_OK, okLabel);
        auto* cancel = new wxButton(this, wxID_CANCEL, "&Cancel");
        cancel->SetDefault(); // Enter alone never starts moving files.
        cancel->SetFocus();
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->Add(execute, wxSizerFlags().Border(wxRIGHT, FromDIP(6)));
        buttons->Add(cancel);
        return buttons;
    }

    ExecuteChoices ConfirmExecuteDialog::choices() const {
        return {.lowPriority = lowPriority_->GetValue(),
                .limitMbPerSecond = static_cast<std::uint64_t>(std::max(limit_->GetValue(), 0)),
                .headOrder = headOrder_ == nullptr || headOrder_->GetValue()};
    }

} // namespace icd::ui
