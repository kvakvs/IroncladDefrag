#include "core/map/Selection.h"

#include "core/map/TileRasterizer.h"

#include <algorithm>
#include <optional>

namespace icd::core {

    namespace {

        // Share of the window a focused fragment fills.
        constexpr double focusFill = 0.5;

        // Share of the window width whole rows fill when a focused fragment wraps: about what the fitted view
        // uses, since half the width would zoom out past the whole volume.
        constexpr double rowsFill = 0.9;

        // Closest zoom when focusing: 8 px per cluster, where grid lines show (plan.md 7.1).
        constexpr double focusMinScale = 1.0 / 8.0;

        // The overlap of two cell rectangles, or nullopt when they do not overlap.
        std::optional<CellRect> intersect(const CellRect& a, const CellRect& b) {
            const std::uint64_t x0 = std::max(a.x, b.x);
            const std::uint64_t y0 = std::max(a.y, b.y);
            const std::uint64_t x1 = std::min(a.x + a.width, b.x + b.width);
            const std::uint64_t y1 = std::min(a.y + a.height, b.y + b.height);
            if (x0 >= x1 || y0 >= y1) {
                return std::nullopt;
            }
            return CellRect{x0, y0, x1 - x0, y1 - y0};
        }

        // The runs that may reach the rows of rect: those overlapping the LCN span of its rows.
        std::span<const ClusterRun> runsInRows(const World& world, std::span<const ClusterRun> runs,
                                               const CellRect& rect) {
            const Lcn first = rect.y * world.rowWidth();
            const Lcn last = (rect.y + rect.height) * world.rowWidth();
            const auto begin =
                std::ranges::partition_point(runs, [first](const ClusterRun& run) { return run.end() <= first; });
            const auto end = std::ranges::partition_point(begin, runs.end(),
                                                          [last](const ClusterRun& run) { return run.start < last; });
            return {begin, end};
        }

        // A rectangle of tile pixels [x0, x1) x [y0, y1).
        struct PixelBox {
            std::uint64_t x0 = 0; // Left.
            std::uint64_t y0 = 0; // Top.
            std::uint64_t x1 = 0; // One past the right.
            std::uint64_t y1 = 0; // One past the bottom.
        };

        // The tile pixels covering cells (inside the tile's rect) at a level: at coarse levels every pixel the cells
        // touch, at deep zoom the cells' full squares.
        PixelBox pixelsOf(const CellRect& tileRect, int level, const CellRect& cells) {
            const std::uint64_t x0 = cells.x - tileRect.x;
            const std::uint64_t y0 = cells.y - tileRect.y;
            const std::uint64_t x1 = x0 + cells.width;
            const std::uint64_t y1 = y0 + cells.height;
            if (level >= 0) {
                const auto shift = static_cast<unsigned>(level);
                return {x0 >> shift, y0 >> shift, ((x1 - 1) >> shift) + 1, ((y1 - 1) >> shift) + 1};
            }
            const auto shift = static_cast<unsigned>(-level);
            return {x0 << shift, y0 << shift, x1 << shift, y1 << shift};
        }

        // Fills a pixel box, skipping grid lines when cells are cellPixels wide and big enough to have them.
        void fillBox(const PixelBox& box, Rgba color, std::span<Rgba> pixels, std::uint64_t cellPixels) {
            const bool grid = cellPixels >= gridCellPixels;
            for (std::uint64_t y = box.y0; y < box.y1; ++y) {
                if (grid && y % cellPixels == cellPixels - 1) {
                    continue;
                }
                for (std::uint64_t x = box.x0; x < box.x1; ++x) {
                    if (!grid || x % cellPixels != cellPixels - 1) {
                        pixels[y * World::tileSize + x] = color;
                    }
                }
            }
        }

    } // namespace

    std::vector<ClusterRun> fileFragments(const ExtentIndex& extents, FileIndex file) {
        std::vector<ClusterRun> fragments;
        const auto records = extents.records();
        for (const std::uint32_t position : extents.extentsOf(file)) {
            const ClusterRun run = records[position].run();
            if (!fragments.empty() && fragments.back().end() == run.start) {
                fragments.back().length += run.length; // A split long run continuing on disk.
            }
            else {
                fragments.push_back(run);
            }
        }
        return fragments;
    }

    SelectionOverlay selectionOf(const ExtentIndex& extents, FileIndex file) {
        std::vector<ClusterRun> sorted = fileFragments(extents, file);
        std::ranges::sort(sorted, {}, &ClusterRun::start);
        SelectionOverlay overlay;
        overlay.file = file;
        for (const ClusterRun& run : sorted) {
            if (!overlay.runs.empty() && overlay.runs.back().end() >= run.start) {
                overlay.runs.back().length = std::max(overlay.runs.back().end(), run.end()) - overlay.runs.back().start;
            }
            else {
                overlay.runs.push_back(run);
            }
        }
        return overlay;
    }

    RunShape shapeOf(const World& world, ClusterRun run) {
        RunShape shape;
        if (run.length == 0) {
            return shape;
        }
        const CellPoint first = world.cellOf(run.start);
        const CellPoint last = world.cellOf(run.end() - 1);
        if (first.y == last.y) {
            shape.rects[shape.count++] = {first.x, first.y, last.x - first.x + 1, 1};
            return shape;
        }
        shape.rects[shape.count++] = {first.x, first.y, world.rowWidth() - first.x, 1};
        if (last.y > first.y + 1) {
            shape.rects[shape.count++] = {0, first.y + 1, world.rowWidth(), last.y - first.y - 1};
        }
        shape.rects[shape.count++] = {0, last.y, last.x + 1, 1};
        return shape;
    }

    bool runsTouch(const World& world, std::span<const ClusterRun> runs, CellRect rect) {
        for (const ClusterRun& run : runsInRows(world, runs, rect)) {
            const RunShape shape = shapeOf(world, run);
            for (std::size_t index = 0; index < shape.count; ++index) {
                if (intersect(shape.rects.at(index), rect)) {
                    return true;
                }
            }
        }
        return false;
    }

    void highlightRuns(const World& world, TileId tile, std::span<const ClusterRun> runs, Rgba color,
                       std::span<Rgba> pixels) {
        const CellRect rect = world.tileCells(tile);
        if (rect.width == 0 || pixels.size() < tilePixels) {
            return;
        }
        const std::uint64_t cellPixels = tile.level < 0 ? std::uint64_t{1} << static_cast<unsigned>(-tile.level) : 1;
        for (const ClusterRun& run : runsInRows(world, runs, rect)) {
            const RunShape shape = shapeOf(world, run);
            for (std::size_t index = 0; index < shape.count; ++index) {
                if (const auto cells = intersect(shape.rects.at(index), rect); cells) {
                    fillBox(pixelsOf(rect, tile.level, *cells), color, pixels, cellPixels);
                }
            }
        }
    }

    Viewport focusView(const World& world, const Viewport& view, ClusterRun run, const ViewLimits& limits) {
        const RunShape shape = shapeOf(world, run);
        if (shape.count == 0) {
            return view;
        }
        // One rectangle: its own box; more: the run wraps, so it spans whole rows.
        const CellRect& top = shape.rects.front();
        const CellRect& bottom = shape.rects.at(shape.count - 1);
        const double x0 = shape.count == 1 ? static_cast<double>(top.x) : 0.0;
        const double width = shape.count == 1 ? static_cast<double>(top.width) : static_cast<double>(world.rowWidth());
        const double y0 = static_cast<double>(top.y);
        const double height = static_cast<double>(bottom.y + bottom.height) - y0;
        Viewport focused = view;
        const double widthFill = shape.count == 1 ? focusFill : rowsFill;
        focused.scale = std::max({width / std::max(view.size.x * widthFill, 1.0),
                                  height / std::max(view.size.y * focusFill, 1.0), focusMinScale});
        focused.center = {x0 + width / 2.0, y0 + height / 2.0};
        return clampView(focused, limits);
    }

} // namespace icd::core
