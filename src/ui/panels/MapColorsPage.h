#pragma once

#include "core/map/ColorModes.h"

#include <wx/panel.h>

#include <span>

class wxRadioBox;

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

    // The "Map colors" tab of the bottom panel: the color mode switch and the legend of the map's visible area. The
    // file class legend has many entries, so it lives in a tab with room and a scroll bar rather than in a strip under
    // the map. The switch sends the View > Color mode commands, so the menu check marks and the map follow one path;
    // the map panel tells the page which mode is shown and which modes are available.
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

    private:
        wxRadioBox* modes_ = nullptr; // The mode switch; child owned by wx.
        Legend* legend_ = nullptr; // Colors and counts; child owned by wx.
    };

} // namespace icd::ui
