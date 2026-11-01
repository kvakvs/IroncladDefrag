#pragma once

#include "core/map/ColorModes.h"

#include <wx/panel.h>

#include <memory>

class wxChoice;
class wxSearchCtrl;
class wxStaticText;

namespace icd::core {
    class Executor;
    class MapData;
} // namespace icd::core

namespace icd::ui {

    class Legend;
    class MapCanvas;

    // The drive map area (plan.md section 11): a toolbar with the color mode, overlays, a caption saying which
    // drive is shown and in what state, the selected file and a file search, above the map canvas with its rulers
    // (plan.md 7.1) and the legend of the color mode below it (plan.md 7.4).
    class MapPanel : public wxPanel {
    public:
        // Creates the toolbar and an empty map; tiles render on executor.
        MapPanel(wxWindow* parent, core::Executor& executor);

        // Disconnects the rulers from the canvas.
        ~MapPanel() override;

        MapPanel(const MapPanel&) = delete;
        MapPanel& operator=(const MapPanel&) = delete;

        // Text in the toolbar, e.g. which drive is shown and what state it is in.
        void setCaption(const wxString& caption);

        // Whether a View command exists yet: color modes whose features are still to come are not.
        [[nodiscard]] static bool commandAvailable(int id);

        // Colors the map and the legend by a mode and shows it in the toolbar.
        void setColorMode(core::ColorMode mode);

        // The View > Color mode command of the current mode (for its check mark).
        [[nodiscard]] int colorModeCommand() const;

        // Shows a map, or clears it with null.
        void setMap(std::shared_ptr<const core::MapData> map);

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const;

        // Carries out a View map command (color mode, minimap and legend toggles, zoom in/out, fit, one cluster per
        // pixel, go to file, next/previous fragment); false for other ids.
        bool handleViewCommand(int id);

        // Whether the minimap is switched on (for the View > Minimap check mark).
        [[nodiscard]] bool minimapShown() const;

        // Whether the legend is switched on (for the View > Legend check mark).
        [[nodiscard]] bool legendShown() const;

    private:
        // Creates the color mode, overlay and search controls (overlays disabled until their features exist).
        wxSizer* createToolbar();

        // Shows or hides the legend strip.
        void showLegend(bool shown);

        // Color mode, minimap and legend commands; false for other ids.
        bool handleDisplayCommand(int id);

        // Zoom, go to file and fragment commands; false for other ids.
        bool handleNavigationCommand(int id);

        // Opens "Go to file" with query; the chosen file is selected and shown.
        void goToFile(const wxString& query);

        // Shows the selected file and its current fragment in the toolbar.
        void updateSelectionLabel();

        core::Executor& executor_; // Runs tiles and file searches.
        std::shared_ptr<const core::MapData> map_; // The map shown, for searches; null for none.
        MapCanvas* canvas_ = nullptr; // The map; child window owned by wx.
        Legend* legend_ = nullptr; // Colors and counts of the view; child window owned by wx.
        core::ColorMode mode_ = core::ColorMode::Fragmentation; // Current color mode.
        wxChoice* colorMode_ = nullptr; // Toolbar color mode; child window owned by wx.
        wxStaticText* caption_ = nullptr; // Drive and state; child window owned by wx.
        wxStaticText* selectionLabel_ = nullptr; // Selected file and fragment; child window owned by wx.
        wxSearchCtrl* search_ = nullptr; // "Find file" field; opens Go to file; child window owned by wx.
    };

} // namespace icd::ui
