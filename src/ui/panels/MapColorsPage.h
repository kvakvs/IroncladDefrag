#pragma once

#include "core/map/ColorModes.h"
#include "services/settings/Settings.h"

#include <wx/panel.h>

#include <functional>
#include <span>
#include <string_view>

class wxButton;
class wxChoice;
class wxRadioBox;
class wxSizer;

namespace icd::core {
    class Executor;
}

namespace icd::ui {

    class Legend;

    // A map color mode with the View > Color mode command that selects it and its name in the mode switch.
    struct ColorModeItem {
        core::ColorMode mode; // The mode.
        int command; // Its View > Color mode command.
        const char* label; // Its name.
    };

    // The color modes in View > Color mode order; zone fit and plan preview need a plan.
    [[nodiscard]] std::span<const ColorModeItem> colorModeItems();

    // The "Map colors" tab of the bottom panel: the color mode switch, the color preset with a reset, and the legend of
    // the map's visible area, whose entries open a color picker. The file class legend has many entries, so it lives
    // in a tab with room and a scroll bar rather than in a strip under the map. The switch sends the View > Color mode
    // commands, so the menu check marks and the map follow one path; the map panel tells the page which mode is shown
    // and which modes are available. Color changes go to the owner, which saves them and repaints the map.
    class MapColorsPage : public wxPanel {
    public:
        // Creates the switch and an empty legend whose counts run on executor (which must outlive it).
        MapColorsPage(wxWindow* parent, core::Executor& executor);

        // The legend, fed by the map panel.
        [[nodiscard]] Legend& legend() const { return *legend_; }

        // Shows mode as selected without sending a command.
        void showMode(core::ColorMode mode);

        // Enables a mode in the switch (overlay modes need a plan, every mode a map).
        void enableMode(core::ColorMode mode, bool enabled);

        // Shows saved color settings without reporting a change.
        void setColors(const services::MapColorSettings& colors);

        // Sets the receiver of color changes (preset, a changed color, a reset).
        void setColorsHandler(std::function<void(const services::MapColorSettings&)> handler) {
            onColors_ = std::move(handler);
        }

    private:
        // Creates the preset picker and the reset button.
        wxSizer* createPresetRow();

        // Asks for a new color of a legend key and applies it.
        void pickColor(std::string_view key, core::Rgba current);

        // Applies changed settings: shows them and reports them.
        void changeColors(services::MapColorSettings colors);

        // Shows colors_ in the picker and the reset button.
        void showColors();

        wxRadioBox* modes_ = nullptr; // The mode switch; child owned by wx.
        wxChoice* preset_ = nullptr; // Color preset; child owned by wx.
        wxButton* reset_ = nullptr; // Drops the preset's custom colors; child owned by wx.
        Legend* legend_ = nullptr; // Colors and counts; child owned by wx.
        services::MapColorSettings colors_; // The settings shown.
        std::function<void(const services::MapColorSettings&)> onColors_; // Receiver of changes.
    };

} // namespace icd::ui
