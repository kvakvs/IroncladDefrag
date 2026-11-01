#include "core/map/Grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <set>

namespace icd::core { namespace {

    // The grid fades in between 5 and 10 px per cell, smoothly and monotonically.
    TEST(Grid, OpacityFades) {
        EXPECT_EQ(gridOpacity(1.0), 0.0);
        EXPECT_EQ(gridOpacity(5.0), 0.0);
        EXPECT_DOUBLE_EQ(gridOpacity(7.5), 0.5);
        EXPECT_EQ(gridOpacity(10.0), 1.0);
        EXPECT_EQ(gridOpacity(32.0), 1.0);
        double previous = 0.0;
        for (double cell = 5.0; cell <= 10.0; cell += 0.25) {
            EXPECT_GE(gridOpacity(cell), previous);
            previous = gridOpacity(cell);
        }
    }

    // A cell starts at the first pixel whose center is at or past its edge.
    TEST(Grid, EdgePixel) {
        EXPECT_EQ(edgePixel(0.0), 0);
        EXPECT_EQ(edgePixel(0.5), 0);
        EXPECT_EQ(edgePixel(0.51), 1);
        EXPECT_EQ(edgePixel(2.0), 2);
        EXPECT_EQ(edgePixel(-0.6), -1);
    }

    // Small cells get no grid at all.
    TEST(Grid, NoneWhenCellsAreSmall) {
        const World world(10'000, {.width = 16, .height = 9});
        const Viewport view{.scale = 1.0 / 4.0, .center = {20, 20}, .size = {640, 480}};
        const GridOverlay grid = gridLines(world, view);
        EXPECT_EQ(grid.opacity, 0.0);
        EXPECT_TRUE(grid.lines.empty());
    }

    // The cell a window pixel shows under nearest-neighbour drawing, or nullopt off the volume.
    std::optional<Lcn> cellAt(const World& world, const Viewport& view, std::int64_t x, std::int64_t y) {
        const WorldPoint point = view.worldAt({static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5});
        if (point.x < 0.0 || point.y < 0.0) {
            return std::nullopt;
        }
        return world.lcnAt(
            {static_cast<std::uint64_t>(std::floor(point.x)), static_cast<std::uint64_t>(std::floor(point.y))});
    }

    // Column and row of an LCN.
    CellPoint cellOf(const World& world, Lcn lcn) { return world.cellOf(lcn); }

    // At random fractional scales and positions, including the partial last row: every grid pixel shows a cell of
    // the volume and is that cell's last pixel across (vertical lines) or down (horizontal lines), and every
    // column's and row's last pixel in the window gets a line.
    TEST(Grid, LinesSitOnCellBordersAsDrawn) {
        const World world(5'003, {.width = 16, .height = 9}); // The last row is partial.
        std::mt19937 random(17);
        std::uniform_real_distribution<double> cellPixels(6.0, 40.0);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        constexpr std::int64_t width = 320;
        constexpr std::int64_t height = 200;
        for (int sample = 0; sample < 60; ++sample) {
            // Every few samples look at the end of the volume, where the last row stops early.
            const bool atEnd = sample % 3 == 0;
            const Viewport view{.scale = 1.0 / cellPixels(random),
                                .center = {unit(random) * static_cast<double>(world.rowWidth()),
                                           atEnd ? static_cast<double>(world.rows()) - unit(random) * 3.0
                                                 : unit(random) * static_cast<double>(world.rows())},
                                .size = {static_cast<double>(width), static_cast<double>(height)}};
            const GridOverlay grid = gridLines(world, view);
            ASSERT_GT(grid.opacity, 0.0);
            std::set<std::pair<std::int64_t, std::int64_t>> vertical; // Pixels covered by vertical lines.
            std::set<std::pair<std::int64_t, std::int64_t>> horizontal; // By horizontal ones.
            for (const GridLine& line : grid.lines) {
                ASSERT_TRUE(line.width == 1 || line.height == 1);
                for (std::int64_t y = line.y; y < line.y + line.height; ++y) {
                    for (std::int64_t x = line.x; x < line.x + line.width; ++x) {
                        ASSERT_GE(x, 0);
                        ASSERT_LT(x, width);
                        ASSERT_GE(y, 0);
                        ASSERT_LT(y, height);
                        (line.width == 1 && line.height != 1 ? vertical : horizontal).insert({x, y});
                    }
                }
            }
            for (std::int64_t y = 0; y < height; ++y) {
                for (std::int64_t x = 0; x < width; ++x) {
                    const auto here = cellAt(world, view, x, y);
                    if (!here) {
                        EXPECT_FALSE(vertical.contains({x, y}) || horizontal.contains({x, y}))
                            << "grid off the volume at " << x << "," << y << " sample " << sample;
                        continue;
                    }
                    const auto right = cellAt(world, view, x + 1, y);
                    const auto below = cellAt(world, view, x, y + 1);
                    const bool lastAcross = !right || cellOf(world, *right).x != cellOf(world, *here).x;
                    const bool lastDown = !below || cellOf(world, *below).y != cellOf(world, *here).y;
                    // A one-pixel-high line is both a vertical and a horizontal candidate; count either.
                    const bool onLine = vertical.contains({x, y}) || horizontal.contains({x, y});
                    EXPECT_EQ(onLine, lastAcross || lastDown) << x << "," << y << " sample " << sample;
                }
            }
        }
    }

    // Line count stays proportional to the window, not to the volume.
    TEST(Grid, LinesBoundedByWindow) {
        const World world(1'000'000'000, {.width = 16, .height = 9});
        const Viewport view{.scale = 1.0 / 5.5, .center = {1000, 1000}, .size = {2560, 1440}};
        const GridOverlay grid = gridLines(world, view);
        EXPECT_GT(grid.opacity, 0.0);
        EXPECT_LE(grid.lines.size(), 2560 / 5 + 1440 / 5 + 4);
        EXPECT_GE(grid.lines.size(), 2560 / 6 + 1440 / 6);
    }

}} // namespace icd::core
