#include "ui/panels/MapColorsPage.h"

#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/map/Legend.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/colordlg.h>
#include <wx/radiobox.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/toplevel.h>

#include <algorithm>
#include <array>
#include <vector>

namespace icd::ui {

    namespace {

        // See colorModeItems().
        constexpr std::array modeItems{
            ColorModeItem{core::ColorMode::FileClass, ID_COLOR_CLASS, "File class"},
            ColorModeItem{core::ColorMode::Fragmentation, ID_COLOR_FRAGMENTATION, "Fragmentation"},
            ColorModeItem{core::ColorMode::Age, ID_COLOR_AGE, "Age"},
            ColorModeItem{core::ColorMode::Size, ID_COLOR_SIZE, "Size"},
            ColorModeItem{core::ColorMode::ZoneFit, ID_COLOR_ZONE_FIT, "Zone fit"},
            ColorModeItem{core::ColorMode::PlanPreview, ID_COLOR_PLAN, "Plan preview"},
        };

        // The switch entry of a mode.
        int modeIndex(core::ColorMode mode) {
            const auto found = std::ranges::find(modeItems, mode, &ColorModeItem::mode);
            return found == modeItems.end() ? 0 : static_cast<int>(found - modeItems.begin());
        }

        // A switch label with the mode's shortcut, from the shortcut table: "File class (Alt+1)".
        wxString itemLabel(const ColorModeItem& item) {
            wxString label = item.label;
            if (const auto shortcut = shortcutFor(item.command); shortcut) {
                label += " (" + shortcutText(*shortcut) + ")";
            }
            return label;
        }

    } // namespace

    std::span<const ColorModeItem> colorModeItems() { return modeItems; }

    MapColorsPage::MapColorsPage(wxWindow* parent, core::Executor& executor) : wxPanel(parent) {
        std::vector<wxString> labels;
        labels.reserve(modeItems.size());
        for (const ColorModeItem& item : modeItems) {
            labels.push_back(itemLabel(item));
        }
        modes_ = new wxRadioBox(this, wxID_ANY, "Color the map by", wxDefaultPosition, wxDefaultSize,
                                static_cast<int>(labels.size()), labels.data(), 1, wxRA_SPECIFY_COLS);
        modes_->SetSelection(modeIndex(core::ColorMode::Fragmentation));
        // The same command as the menu, so the menu's check mark, the map and this switch always agree.
        modes_->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& event) {
            const int index = event.GetSelection();
            wxWindow* frame = wxGetTopLevelParent(this);
            if (frame != nullptr && index >= 0 && static_cast<std::size_t>(index) < modeItems.size()) {
                wxCommandEvent command(wxEVT_MENU, modeItems.at(static_cast<std::size_t>(index)).command);
                wxPostEvent(frame, command);
            }
        });
        legend_ = new Legend(this, executor);
        legend_->setColorHandler([this](std::string_view key, core::Rgba color) { pickColor(key, color); });

        auto* right = new wxBoxSizer(wxVERTICAL);
        right->Add(createPresetRow(), wxSizerFlags().Border(wxBOTTOM, FromDIP(8)));
        right->Add(legend_, wxSizerFlags(1).Expand());
        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(modes_, wxSizerFlags().Top().Border(wxALL, FromDIP(8)));
        sizer->Add(right, wxSizerFlags(1).Expand().Border(wxTOP | wxRIGHT | wxBOTTOM, FromDIP(8)));
        SetSizer(sizer);
        showColors();
    }

    wxSizer* MapColorsPage::createPresetRow() {
        preset_ = new wxChoice(this, wxID_ANY);
        for (const core::ColorPreset preset : core::colorPresets()) {
            preset_->Append(wxString::FromUTF8(std::string(core::presetName(preset))));
        }
        preset_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
            const auto presets = core::colorPresets();
            const auto index = static_cast<std::size_t>(std::max(event.GetSelection(), 0));
            services::MapColorSettings next = colors_;
            next.preset = presets[std::min(index, presets.size() - 1)];
            changeColors(std::move(next));
        });
        reset_ = new wxButton(this, wxID_ANY, "Reset colors");
        reset_->SetToolTip("Go back to the preset's own colors");
        reset_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            services::MapColorSettings next = colors_;
            next.custom.erase(next.preset);
            changeColors(std::move(next));
        });
        auto* hint = new wxStaticText(this, wxID_ANY, "Click a color below to change it.");
        hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Colors:"), wxSizerFlags().CenterVertical());
        row->Add(preset_, wxSizerFlags().CenterVertical().Border(wxLEFT | wxRIGHT, FromDIP(6)));
        row->Add(reset_, wxSizerFlags().CenterVertical().Border(wxRIGHT, FromDIP(12)));
        row->Add(hint, wxSizerFlags().CenterVertical());
        return row;
    }

    void MapColorsPage::setColors(const services::MapColorSettings& colors) {
        colors_ = colors;
        showColors();
    }

    void MapColorsPage::showColors() {
        const auto presets = core::colorPresets();
        const auto found = std::ranges::find(presets, colors_.preset);
        preset_->SetSelection(found == presets.end() ? 0 : static_cast<int>(found - presets.begin()));
        const auto custom = colors_.custom.find(colors_.preset);
        reset_->Enable(custom != colors_.custom.end() && !custom->second.empty());
    }

    void MapColorsPage::pickColor(std::string_view key, core::Rgba current) {
        wxColourData data;
        data.SetChooseFull(true);
        data.SetColour(wxColour(static_cast<unsigned char>(current >> 16U), static_cast<unsigned char>(current >> 8U),
                                static_cast<unsigned char>(current)));
        wxColourDialog dialog(this, &data);
        dialog.SetTitle("Map color");
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }
        const wxColour chosen = dialog.GetColourData().GetColour();
        services::MapColorSettings next = colors_;
        next.custom[next.preset][std::string(key)] = core::rgb(chosen.Red(), chosen.Green(), chosen.Blue());
        changeColors(std::move(next));
    }

    void MapColorsPage::changeColors(services::MapColorSettings colors) {
        if (colors == colors_) {
            return;
        }
        colors_ = std::move(colors);
        showColors();
        if (onColors_) {
            onColors_(colors_);
        }
    }

    void MapColorsPage::showMode(core::ColorMode mode) { modes_->SetSelection(modeIndex(mode)); }

    void MapColorsPage::enableMode(core::ColorMode mode, bool enabled) {
        modes_->Enable(static_cast<unsigned>(modeIndex(mode)), enabled);
    }

} // namespace icd::ui
