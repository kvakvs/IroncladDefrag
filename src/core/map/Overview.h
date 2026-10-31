#pragma once

#include "core/map/Viewport.h"
#include "core/map/World.h"

#include <optional>

namespace icd::core {

    // A rectangle in map window pixels.
    struct PixelBox {
        double x = 0.0; // Left.
        double y = 0.0; // Top.
        double width = 0.0; // Width.
        double height = 0.0; // Height.

        // Whether a pixel lies inside (left and top edges included).
        [[nodiscard]] bool contains(PixelPoint point) const {
            return point.x >= x && point.x < x + width && point.y >= y && point.y < y + height;
        }

        // The middle of the box.
        [[nodiscard]] PixelPoint center() const { return {x + width / 2.0, y + height / 2.0}; }
    };

    // How the minimap is sized and placed in the map window (plan.md 7.4).
    struct OverviewStyle {
        double maxEdge = 200.0; // Longest side of the minimap, in pixels.
        double margin = 12.0; // Gap to the window's bottom-right corner.
        double minWindowShare = 3.0; // The window must be this many minimaps wide and high to show one.
    };

    // The minimap: the whole volume drawn small in a corner of the map window, with the part the view shows
    // marked. Pure math, so dragging is tested without a window.
    struct Overview {
        PixelBox box; // Where the whole world is drawn, in window pixels.
        double scale = 1.0; // World cells per minimap pixel.

        // The world point under a window pixel (clamped to the world).
        [[nodiscard]] WorldPoint worldAt(PixelPoint pixel, const World& world) const;

        // The window pixel showing a world point.
        [[nodiscard]] PixelPoint pixelAt(WorldPoint point) const;

        // The part of the world a view shows, in window pixels, clipped to the minimap and grown around its
        // center to at least minEdge so it stays visible at deep zoom.
        [[nodiscard]] PixelBox viewBox(const Viewport& view, double minEdge) const;

        // The view moved so its center is the world point under pointer - grab; grab is where in the view box
        // the user took hold, relative to its center (zero after a click outside the box: the view jumps there).
        [[nodiscard]] Viewport dragTo(const Viewport& view, PixelPoint pointer, PixelPoint grab, const World& world,
                                      const ViewLimits& limits) const;
    };

    // The minimap for a world in a window of size pixels: the world's shape scaled to fit maxEdge, in the
    // bottom-right corner. Nullopt when the window is too small to spare the room or the world is empty.
    [[nodiscard]] std::optional<Overview> placeOverview(const World& world, PixelPoint size,
                                                        const OverviewStyle& style = {});

} // namespace icd::core
