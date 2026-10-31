#include "ui/panels/StrategyPage.h"

#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <array>

namespace icd::ui {

    StrategyPage::StrategyPage(wxWindow* parent) : wxPanel(parent) {
        preset_ = new wxChoice(this, wxID_ANY);
        for (const services::StrategyPreset& preset : services::strategyPresets()) {
            preset_->Append(wxString::FromUTF8(preset.name));
        }
        const std::array<wxString, 3> levels{"Fast", "Moderate", "Maximum"};
        effort_ = new wxRadioBox(this, wxID_ANY, "Effort", wxDefaultPosition, wxDefaultSize, levels.size(),
                                 levels.data(), 1, wxRA_SPECIFY_ROWS);
        zones_ = new wxListView(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
        zones_->AppendColumn("Zone", wxLIST_FORMAT_LEFT, FromDIP(180));
        zones_->AppendColumn("File classes", wxLIST_FORMAT_LEFT, FromDIP(280));
        zones_->AppendColumn("From", wxLIST_FORMAT_RIGHT, FromDIP(60));
        zones_->AppendColumn("To", wxLIST_FORMAT_RIGHT, FromDIP(60));

        auto* top = new wxBoxSizer(wxHORIZONTAL);
        top->Add(new wxStaticText(this, wxID_ANY, "Preset:"), wxSizerFlags().CenterVertical());
        top->Add(preset_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(8)));
        top->Add(effort_, wxSizerFlags().CenterVertical());
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(top, wxSizerFlags().Border(wxALL, FromDIP(8)));
        sizer->Add(zones_, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8)));
        SetSizer(sizer);

        preset_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { onUserChange(); });
        effort_->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent&) { onUserChange(); });
        reset();
        setEditable(false);
    }

    std::size_t StrategyPage::preset() const {
        const int selection = preset_->GetSelection();
        return selection == wxNOT_FOUND ? 0 : static_cast<std::size_t>(selection);
    }

    services::EffortLevel StrategyPage::effort() const {
        return static_cast<services::EffortLevel>(effort_->GetSelection());
    }

    void StrategyPage::reset() {
        preset_->SetSelection(0);
        effort_->SetSelection(0);
        showZones();
    }

    void StrategyPage::setEditable(bool editable) {
        preset_->Enable(editable);
        effort_->Enable(editable);
    }

    void StrategyPage::showZones() {
        zones_->DeleteAllItems();
        const auto& zones = services::strategyPresets().at(preset()).zones;
        for (const services::StrategyZone& zone : zones) {
            const long row = zones_->InsertItem(zones_->GetItemCount(), wxString::FromUTF8(zone.name));
            zones_->SetItem(row, 1, wxString::FromUTF8(zone.classes));
            zones_->SetItem(row, 2, wxString::Format("%d%%", zone.startPercent));
            zones_->SetItem(row, 3, wxString::Format("%d%%", zone.endPercent));
        }
    }

    void StrategyPage::onUserChange() {
        showZones();
        if (onChange_) {
            onChange_();
        }
    }

} // namespace icd::ui
