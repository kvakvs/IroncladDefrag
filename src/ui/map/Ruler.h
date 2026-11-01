#pragma once

#include "core/map/Ruler.h"

#include <wx/font.h>
#include <wx/window.h>

#include <cstdint>
#include <optional>

class wxDC;

namespace icd::ui {

    class MapCanvas;

    // A ruler beside the drive map (plan.md 7.1). The vertical one, left of the map, marks row starts with the
    // byte offset and % of the volume; the horizontal one, above it, marks offsets within a row. Tick math is
    // core::rowTicks / columnTicks; this window only draws them with GDI and repaints when the map's view
    // changes (MapPanel forwards the canvas's view observer).
    class Ruler : public wxWindow {
    public:
        // Which side of the map the ruler is on.
        enum class Orientation : std::uint8_t {
            Vertical, // Left of the map: rows.
            Horizontal, // Above the map: columns.
        };

        // Describes canvas, a sibling that outlives the ruler's paints (both belong to the same panel).
        Ruler(wxWindow* parent, const MapCanvas& canvas, Orientation orientation);

        // Where the volume sits on its disk, for the "% of disk" labels of the row ruler; nullopt hides them.
        void setDisk(std::optional<core::DiskPlacement> disk);

    private:
        // Draws the ticks for the current view.
        void onPaint(wxPaintEvent& event);

        // Picks the font and sizes the ruler for its widest label (again after a DPI change).
        void updateMetrics();

        // What the tick math needs, with label spacing for this ruler's text.
        [[nodiscard]] core::RulerInput input() const;

        // Draws row ticks: lines across the ruler, labels below them.
        void drawVertical(wxDC& dc, const core::RulerTicks& ticks) const;

        // Draws column ticks: lines down the ruler, labels right of them.
        void drawHorizontal(wxDC& dc, const core::RulerTicks& ticks) const;

        const MapCanvas& canvas_; // The map described.
        Orientation orientation_; // Side of the map.
        wxFont font_; // Label font, a bit smaller than the window font.
        int lineHeight_ = 0; // Height of one label line.
        int labelWidth_ = 0; // Width of the widest expected label.
        int padding_ = 0; // Gap between ticks, labels and edges.
        std::optional<core::DiskPlacement> disk_; // Partition placement of the shown volume, when known.
    };

} // namespace icd::ui
