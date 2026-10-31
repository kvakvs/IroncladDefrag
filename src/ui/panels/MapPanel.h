#pragma once

#include <wx/panel.h>

#include <memory>

class wxChoice;
class wxStaticText;

namespace icd::core {
    class Executor;
    class MapData;
} // namespace icd::core

namespace icd::ui {

    class MapCanvas;

    // The drive map area (plan.md section 11): a toolbar with the color mode, overlays, search and a caption
    // saying which drive is shown and in what state, above the map canvas with its rulers (plan.md 7.1).
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

        // Shows a color mode (index of View > Color mode) in the toolbar.
        void setColorMode(int mode);

        // Shows a map, or clears it with null.
        void setMap(std::shared_ptr<const core::MapData> map);

        // Whether a map is shown.
        [[nodiscard]] bool hasMap() const;

        // Carries out a View map command (minimap toggle, zoom in/out, fit, one cluster per pixel); false for
        // other ids.
        bool handleViewCommand(int id);

        // Whether the minimap is switched on (for the View > Minimap check mark).
        [[nodiscard]] bool minimapShown() const;

    private:
        // Creates the color mode, overlay and search controls (disabled until their features exist).
        wxSizer* createToolbar();

        MapCanvas* canvas_ = nullptr; // The map; child window owned by wx.
        wxChoice* colorMode_ = nullptr; // Toolbar color mode; child window owned by wx.
        wxStaticText* caption_ = nullptr; // Drive and state; child window owned by wx.
    };

} // namespace icd::ui
