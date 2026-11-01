#include "core/map/Grid.h"

#include <algorithm>
#include <cmath>

namespace icd::core {

    namespace {

        // Cells this small on screen get no grid (plan.md 7.1: lines once cells are about 6 px).
        constexpr double fadeStart = 5.0;

        // Cells this big get the full grid.
        constexpr double fadeFull = 10.0;

        // What the lines of both directions are computed from.
        struct GridFrame {
            const World& world; // Cell layout.
            const Viewport& view; // What is shown.
            CellPoint last; // Cell of the last cluster: the last row ends there.
            std::int64_t windowWidth = 0; // Window pixels across.
            std::int64_t windowHeight = 0; // Window pixels down.
        };

        // Adds a line clipped to the window, unless nothing of it is visible.
        void addLine(std::vector<GridLine>& lines, const GridFrame& frame, const GridLine& line) {
            const std::int64_t x0 = std::max<std::int64_t>(line.x, 0);
            const std::int64_t y0 = std::max<std::int64_t>(line.y, 0);
            const std::int64_t x1 = std::min(line.x + line.width, frame.windowWidth);
            const std::int64_t y1 = std::min(line.y + line.height, frame.windowHeight);
            if (x0 < x1 && y0 < y1) {
                lines.push_back({x0, y0, x1 - x0, y1 - y0});
            }
        }

        // The first pixel of the cell column starting at world x.
        std::int64_t columnPixel(const Viewport& view, double x) { return edgePixel(view.pixelAt({x, 0}).x); }

        // The first pixel of the cell row starting at world y.
        std::int64_t rowPixel(const Viewport& view, double y) { return edgePixel(view.pixelAt({0, y}).y); }

        // The world coordinates one window axis shows.
        struct AxisRange {
            double from = 0.0; // At the window's first pixel.
            double to = 0.0; // At its far edge.
        };

        // Boundaries [first, last] of cells visible in a range, within [1, limit].
        std::pair<std::int64_t, std::int64_t> visibleBoundaries(AxisRange range, std::uint64_t limit) {
            const auto first = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::floor(range.from)));
            const auto last =
                std::min(static_cast<std::int64_t>(limit), static_cast<std::int64_t>(std::ceil(range.to)) + 1);
            return {first, last};
        }

        // Right edges of the visible columns: down to the last full row, and through the last row where it holds
        // that column.
        void verticalLines(const GridFrame& frame, std::vector<GridLine>& lines) {
            const Viewport& view = frame.view;
            const auto [first, last] =
                visibleBoundaries({view.worldAt({0, 0}).x, view.worldAt(view.size).x}, frame.world.rowWidth());
            const std::int64_t top = rowPixel(view, 0.0);
            const std::int64_t fullBottom = rowPixel(view, static_cast<double>(frame.last.y));
            const std::int64_t lastBottom = rowPixel(view, static_cast<double>(frame.last.y) + 1.0);
            for (std::int64_t k = first; k <= last; ++k) {
                const bool inLastRow = static_cast<std::uint64_t>(k) <= frame.last.x + 1;
                const std::int64_t bottom = inLastRow ? lastBottom : fullBottom;
                const std::int64_t x = columnPixel(view, static_cast<double>(k)) - 1;
                addLine(lines, frame, {x, top, 1, bottom - top});
            }
        }

        // Bottom edges of the visible rows, as long as each row is.
        void horizontalLines(const GridFrame& frame, std::vector<GridLine>& lines) {
            const Viewport& view = frame.view;
            const auto [first, last] =
                visibleBoundaries({view.worldAt({0, 0}).y, view.worldAt(view.size).y}, frame.world.rows());
            const std::int64_t left = columnPixel(view, 0.0);
            const std::int64_t fullRight = columnPixel(view, static_cast<double>(frame.world.rowWidth()));
            const std::int64_t lastRight = columnPixel(view, static_cast<double>(frame.last.x) + 1.0);
            for (std::int64_t j = first; j <= last; ++j) {
                const bool fullRow = static_cast<std::uint64_t>(j) - 1 < frame.last.y;
                const std::int64_t right = fullRow ? fullRight : lastRight;
                const std::int64_t y = rowPixel(view, static_cast<double>(j)) - 1;
                addLine(lines, frame, {left, y, right - left, 1});
            }
        }

    } // namespace

    double gridOpacity(double cellPixels) {
        const double t = std::clamp((cellPixels - fadeStart) / (fadeFull - fadeStart), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t); // Smoothstep: no visible kink where the fade starts or ends.
    }

    std::int64_t edgePixel(double windowPosition) { return static_cast<std::int64_t>(std::ceil(windowPosition - 0.5)); }

    GridOverlay gridLines(const World& world, const Viewport& view) {
        GridOverlay grid;
        grid.opacity = view.scale > 0.0 ? gridOpacity(1.0 / view.scale) : 0.0;
        if (grid.opacity <= 0.0 || world.clusters() == 0) {
            return grid;
        }
        const GridFrame frame{.world = world,
                              .view = view,
                              .last = world.cellOf(world.clusters() - 1),
                              .windowWidth = static_cast<std::int64_t>(std::ceil(view.size.x)),
                              .windowHeight = static_cast<std::int64_t>(std::ceil(view.size.y))};
        verticalLines(frame, grid.lines);
        horizontalLines(frame, grid.lines);
        return grid;
    }

} // namespace icd::core
