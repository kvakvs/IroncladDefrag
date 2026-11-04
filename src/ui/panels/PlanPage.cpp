#include "ui/panels/PlanPage.h"

#include "core/format/ByteSize.h"
#include "core/format/Count.h"
#include "core/format/Duration.h"

#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <array>
#include <format>
#include <functional>
#include <string>

namespace icd::ui {

    namespace {

        // One table row: a label and how to print the metric for a level.
        struct MetricRow {
            const char* label = ""; // Row title.
            std::function<std::string(const services::PlanLevel&)> value; // Cell text for one level.
        };

        // The rows of the level table, in display order.
        std::array<MetricRow, 6> metricRows() {
            using services::PlanLevel;
            return {{
                {"Files to move", [](const PlanLevel& l) { return core::formatCount(l.filesToMove); }},
                {"Data to move", [](const PlanLevel& l) { return core::formatBytes(l.bytesToMove); }},
                {"Least possible", [](const PlanLevel& l) { return core::formatBytes(l.lowerBoundBytes); }},
                {"Estimated time", [](const PlanLevel& l) { return core::formatDuration(l.estimate); }},
                {"Fragmentation after",
                 [](const PlanLevel& l) { return std::format("{:.1f}%", l.fragmentationAfter); }},
                {"In target zone after", [](const PlanLevel& l) { return std::format("{:.0f}%", l.zoneFitAfter); }},
            }};
        }

        // The VSS row: unaligned moves of all moves and the shadow storage they may add.
        std::string unalignedText(const services::PlanLevel& l) {
            if (l.unalignedMoves == 0) {
                return std::format("0 of {}", core::formatCount(l.moves));
            }
            return std::format("{} of {} (+{})", core::formatCount(l.unalignedMoves), core::formatCount(l.moves),
                               core::formatBytes(l.shadowGrowthBytes));
        }

        // Lines of a warning list, or a note that there are none.
        wxString warningText(const std::vector<std::string>& warnings) {
            if (warnings.empty()) {
                return "No warnings.";
            }
            wxString text;
            for (const std::string& warning : warnings) {
                text += (text.empty() ? "" : "\n") + wxString::FromUTF8("\xE2\x9A\xA0 " + warning);
            }
            return text;
        }

    } // namespace

    PlanPage::PlanPage(wxWindow* parent) : wxPanel(parent) {
        hint_ = new wxStaticText(this, wxID_ANY, "");
        hint_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        levels_ = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        levels_->AppendColumn("", wxLIST_FORMAT_LEFT, FromDIP(170));
        for (int level = 0; level < 3; ++level) {
            levels_->AppendColumn("", wxLIST_FORMAT_RIGHT, FromDIP(130));
        }
        warnings_ = new wxStaticText(this, wxID_ANY, "");
        blockUnaligned_ = new wxCheckBox(this, wxID_ANY,
                                         "&Block unaligned moves (shadow copies exist; unaligned moves "
                                         "grow shadow storage)");
        blockUnaligned_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            if (vssChanged_) {
                vssChanged_();
            }
        });
        blockUnaligned_->Hide();

        auto* side = new wxBoxSizer(wxVERTICAL);
        side->Add(warnings_, wxSizerFlags(1).Expand());
        side->Add(blockUnaligned_, wxSizerFlags().Border(wxTOP, FromDIP(8)));
        auto* body = new wxBoxSizer(wxHORIZONTAL);
        body->Add(levels_, wxSizerFlags(0).Expand().Border(wxRIGHT, FromDIP(12)));
        body->Add(side, wxSizerFlags(1).Expand());
        levels_->SetMinSize(FromDIP(wxSize(570, -1)));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(hint_, wxSizerFlags().Border(wxALL, FromDIP(8)));
        sizer->Add(body, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
        SetSizer(sizer);
        clear("Build a plan to compare the Fast, Moderate and Maximum levels and review what will move.");
    }

    void PlanPage::setTitles(std::optional<services::EffortLevel> chosen) {
        for (int column = 1; column <= 3; ++column) {
            const auto level = static_cast<services::EffortLevel>(column - 1);
            wxString title = services::effortName(level);
            if (chosen == level) {
                title += " (chosen)";
            }
            wxListItem item;
            item.SetMask(wxLIST_MASK_TEXT);
            item.SetText(title);
            levels_->SetColumn(column, item);
        }
    }

    void PlanPage::show(const services::PlanSummary& plan, services::EffortLevel chosen) {
        levels_->DeleteAllItems();
        setTitles(chosen);
        for (const MetricRow& row : metricRows()) {
            const long index = levels_->InsertItem(levels_->GetItemCount(), row.label);
            for (std::size_t level = 0; level < plan.levels.size(); ++level) {
                levels_->SetItem(index, static_cast<long>(level) + 1,
                                 wxString::FromUTF8(row.value(plan.levels.at(level))));
            }
        }
        if (plan.shadowCopies) {
            const long index = levels_->InsertItem(levels_->GetItemCount(), "Unaligned moves (VSS)");
            for (std::size_t level = 0; level < plan.levels.size(); ++level) {
                levels_->SetItem(index, static_cast<long>(level) + 1,
                                 wxString::FromUTF8(unalignedText(plan.levels.at(level))));
            }
        }
        warnings_->SetLabelText(warningText(plan.warnings));
        hint_->Hide();
        Layout();
    }

    void PlanPage::clear(const wxString& hint) {
        levels_->DeleteAllItems();
        setTitles(std::nullopt);
        warnings_->SetLabelText("");
        hint_->SetLabelText(hint);
        hint_->Show();
        Layout();
    }

    void PlanPage::setShadowCopies(bool present) {
        blockUnaligned_->Show(present);
        Layout();
    }

    bool PlanPage::blockUnaligned() const { return blockUnaligned_->IsShown() && blockUnaligned_->GetValue(); }

    void PlanPage::setBlockUnaligned(bool block) { blockUnaligned_->SetValue(block); }

} // namespace icd::ui
