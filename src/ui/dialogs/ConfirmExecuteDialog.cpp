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

    } // namespace

    ConfirmExecuteDialog::ConfirmExecuteDialog(wxWindow* parent, const ExecuteConfirmation& what) :
        wxDialog(parent, wxID_ANY, "Execute plan") {
        SetIcons(appIcons());
        const std::string question = std::format("Execute the {} plan ({}) on {}?",
                                                 services::effortName(what.level.level), what.strategy, what.drive);
        auto* heading = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(question));
        heading->SetFont(heading->GetFont().Scaled(1.2F).Bold());

        auto* sizer = new wxBoxSizer(wxVERTICAL);
        const int border = FromDIP(16);
        sizer->Add(heading, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, border));
        sizer->Add(createFigures(what.level), wxSizerFlags().Border(wxALL, border));
        if (!what.warnings.empty()) {
            auto* warnings = new wxStaticText(this, wxID_ANY, warningText(what.warnings));
            warnings->Wrap(FromDIP(wrapWidth));
            sizer->Add(warnings, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        }
        const wxString safety = what.simulated
            ? wxString("This is the fake drive list: the run is simulated and nothing on a disk changes.")
            : wxString(safetyNote);
        sizer->Add(note(this, safety), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(createOptions(), wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, border));
        sizer->Add(createButtons(), wxSizerFlags().Right().Border(wxALL, FromDIP(12)));
        SetSizerAndFit(sizer);
        CentreOnParent();
    }

    wxSizer* ConfirmExecuteDialog::createFigures(const services::PlanLevel& level) {
        const std::array<std::pair<const char*, std::string>, 5> rows{{
            {"Files to move:", core::formatCount(level.filesToMove)},
            {"Data to move:",
             std::format("{} (least possible {})", core::formatBytes(level.bytesToMove),
                         core::formatBytes(level.lowerBoundBytes))},
            {"Estimated time:", core::formatDuration(level.estimate)},
            {"Fragmentation after:", std::format("{:.1f}%", level.fragmentationAfter)},
            {"In target zone after:", std::format("{:.0f}%", level.zoneFitAfter)},
        }};
        auto* grid = new wxFlexGridSizer(2, wxSize(FromDIP(16), FromDIP(4)));
        for (const auto& [label, value] : rows) {
            grid->Add(new wxStaticText(this, wxID_ANY, label));
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(value)));
        }
        return grid;
    }

    wxSizer* ConfirmExecuteDialog::createOptions() {
        lowPriority_ = new wxCheckBox(this, wxID_ANY, "&Low I/O priority (slower; other programs stay responsive)");
        limit_ = new wxSpinCtrl(this, wxID_ANY, "0", wxDefaultPosition, FromDIP(wxSize(90, -1)), wxSP_ARROW_KEYS, 0,
                                maxLimit, 0);
        auto* limitRow = new wxBoxSizer(wxHORIZONTAL);
        limitRow->Add(new wxStaticText(this, wxID_ANY, "&Throughput limit:"), wxSizerFlags().CenterVertical());
        limitRow->Add(limit_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        limitRow->Add(new wxStaticText(this, wxID_ANY, "MB/s (0: no limit)"), wxSizerFlags().CenterVertical());
        auto* options = new wxBoxSizer(wxVERTICAL);
        options->Add(lowPriority_, wxSizerFlags().Border(wxBOTTOM, FromDIP(6)));
        options->Add(limitRow);
        return options;
    }

    wxSizer* ConfirmExecuteDialog::createButtons() {
        auto* execute = new wxButton(this, wxID_OK, "&Execute");
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
                .limitMbPerSecond = static_cast<std::uint64_t>(std::max(limit_->GetValue(), 0))};
    }

} // namespace icd::ui
