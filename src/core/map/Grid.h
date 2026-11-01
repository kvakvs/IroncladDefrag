#pragma once

#include "core/map/Viewport.h"
#include "core/map/World.h"

#include <cstdint>
#include <vector>

namespace icd::core {

    // A grid line in window pixels: a rectangle one pixel thick, on whole pixels so it stays crisp.
    struct GridLine {
        std::int64_t x = 0; // Left.
        std::int64_t y = 0; // Top.
        std::int64_t width = 0; // Columns.
        std::int64_t height = 0; // Rows.

        // Lines compare as values (tests).
        friend constexpr bool operator==(const GridLine&, const GridLine&) = default;
    };

    // The lines between cells for one view (plan.md 7.1), drawn over the tiles at screen resolution. Tiles exist
    // only at power-of-two zooms and are scaled between them, so 1 px lines baked into tiles would be dropped or
    // kept depending on position and flicker while zooming; drawn per frame, every line stays one pixel wide.
    struct GridOverlay {
        double opacity = 0.0; // 0 (no grid) to 1, fading in with the cell size on screen.
        std::vector<GridLine> lines; // Right and bottom edge of every visible cell of the volume.
    };

    // How strongly the grid shows for cells of cellPixels window pixels: none up to 5 px, full from 10 px, eased
    // in between, so lines fade in while zooming instead of appearing at a zoom step.
    [[nodiscard]] double gridOpacity(double cellPixels);

    // The window pixel where a world coordinate's cell starts under nearest-neighbour drawing: the first pixel
    // whose center lies at or past it.
    [[nodiscard]] std::int64_t edgePixel(double windowPosition);

    // The grid of a view: each cell's right and bottom edge on the last pixel of the cell as drawn, so lines sit
    // exactly on cell borders at any scale. Lines stop at the last cluster and at the window; empty when the
    // cells are too small to show a grid.
    [[nodiscard]] GridOverlay gridLines(const World& world, const Viewport& view);

} // namespace icd::core
