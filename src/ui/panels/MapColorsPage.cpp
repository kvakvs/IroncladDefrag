#include "ui/panels/MapColorsPage.h"

#include "ui/Accelerators.h"
#include "ui/Commands.h"
#include "ui/map/Legend.h"

#include <wx/radiobox.h>
#include <wx/sizer.h>
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

        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(modes_, wxSizerFlags().Top().Border(wxALL, FromDIP(8)));
        sizer->Add(legend_, wxSizerFlags(1).Expand().Border(wxTOP | wxRIGHT | wxBOTTOM, FromDIP(8)));
        SetSizer(sizer);
    }

    void MapColorsPage::showMode(core::ColorMode mode) { modes_->SetSelection(modeIndex(mode)); }

    void MapColorsPage::enableMode(core::ColorMode mode, bool enabled) {
        modes_->Enable(static_cast<unsigned>(modeIndex(mode)), enabled);
    }

} // namespace icd::ui
