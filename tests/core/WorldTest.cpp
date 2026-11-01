#include "core/map/World.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <vector>

namespace icd::core { namespace {

    // Nice widths keep four significant binary digits and stay within 1/16 of the input.
    TEST(World, RoundToNice) {
        EXPECT_EQ(roundToNice(0.3), 1u);
        EXPECT_EQ(roundToNice(13), 13u);
        EXPECT_EQ(roundToNice(15.4), 15u);
        EXPECT_EQ(roundToNice(1000), 1024u);
        for (double value = 17; value < 1e7; value *= 1.37) {
            const std::uint64_t nice = roundToNice(value);
            const std::uint64_t unit = std::uint64_t{1} << std::countr_zero(nice);
            EXPECT_LE(std::bit_width(nice / unit), 4) << value;
            EXPECT_NEAR(static_cast<double>(nice), value, value / 16.0) << value;
        }
    }

    // A 30 TB volume on a 16:9 screen gives a 16:9 world with a nice row width.
    TEST(World, LandscapeAndPortrait) {
        const ClusterCount clusters = 7'300'000'000; // 30 TB at 4 KB.
        const World wide(clusters, {.width = 3840, .height = 2160});
        EXPECT_NEAR(static_cast<double>(wide.rowWidth()) / static_cast<double>(wide.rows()), 16.0 / 9.0, 0.15);
        EXPECT_GE(wide.rowWidth() * wide.rows(), clusters);
        EXPECT_LT((wide.rows() - 1) * wide.rowWidth(), clusters); // No empty rows at the end.
        EXPECT_EQ(wide.rowWidth() % 4096, 0u); // Bins up to 4096 clusters never wrap a row.

        const World tall(clusters, {.width = 1080, .height = 1920});
        EXPECT_NEAR(static_cast<double>(tall.rowWidth()) / static_cast<double>(tall.rows()), 9.0 / 16.0, 0.05);

        const World square(1000, {.width = 0, .height = 0}); // Unusable aspect: square.
        EXPECT_EQ(square.rowWidth(), 32u);
        EXPECT_EQ(square.rows(), 32u);
    }

    // LCN to cell and back is exact; cells past the last cluster or the row end are outside.
    TEST(World, RoundTrips) {
        const World world(1'000'003, {.width = 16, .height = 9});
        for (const Lcn lcn : {Lcn{0}, Lcn{1}, world.rowWidth() - 1, world.rowWidth(), Lcn{777'777}, Lcn{1'000'002}}) {
            const CellPoint cell = world.cellOf(lcn);
            EXPECT_LT(cell.x, world.rowWidth());
            EXPECT_EQ(world.lcnAt(cell), lcn);
        }
        const CellPoint last = world.cellOf(1'000'002);
        EXPECT_EQ(last.y, world.rows() - 1);
        EXPECT_EQ(world.lcnAt({last.x + 1, last.y}), std::nullopt); // Partly empty last row.
        EXPECT_EQ(world.lcnAt({world.rowWidth(), 0}), std::nullopt);
        EXPECT_EQ(world.lcnAt({0, world.rows()}), std::nullopt);
        EXPECT_EQ(World(0, {}).lcnAt({0, 0}), std::nullopt);
    }

    // Levels: the finest is 32 px per cluster, the coarsest holds the world in one tile.
    TEST(World, ZoomLevels) {
        const World world(7'300'000'000, {.width = 16, .height = 9});
        const int top = world.maxLevel();
        EXPECT_EQ(world.tilesAcross(top), 1u);
        EXPECT_EQ(world.tilesDown(top), 1u);
        EXPECT_GT(world.tilesAcross(top - 1), 1u);
        EXPECT_DOUBLE_EQ(World::cellsPerPixel(World::minLevel), 1.0 / 32.0);
        EXPECT_EQ(world.levelFor(1.0), 0);
        EXPECT_EQ(world.levelFor(1.9), 0); // Nearest finer level.
        EXPECT_EQ(world.levelFor(2.0), 1);
        EXPECT_EQ(world.levelFor(0.3), -2);
        EXPECT_EQ(world.levelFor(1e-9), World::minLevel);
        EXPECT_EQ(world.levelFor(1e30), top);
        EXPECT_EQ(world.levelFor(0.0), World::minLevel);
    }

    // Tiles tile the world: edge tiles are clipped, tiles outside are empty.
    TEST(World, TileCells) {
        const World world(100'000, {.width = 1, .height = 1}); // 320 x 313 cells.
        ASSERT_EQ(world.rowWidth(), 320u);
        EXPECT_EQ(world.tilesAcross(0), 2u);
        const CellRect first = world.tileCells({0, 0, 0});
        EXPECT_EQ(first.width, 256u);
        const CellRect edge = world.tileCells({0, 1, 1});
        EXPECT_EQ(edge.x, 256u);
        EXPECT_EQ(edge.width, 64u);
        EXPECT_EQ(edge.height, world.rows() - 256);
        EXPECT_EQ(world.tileCells({0, 2, 0}).width, 0u);
        const CellRect deep = world.tileCells({World::minLevel, 3, 0}); // 8 cells per tile edge.
        EXPECT_EQ(deep.x, 24u);
        EXPECT_EQ(deep.width, 8u);
    }

    // Runs are cut at row ends, and nothing past the volume is reported.
    TEST(World, RowSegments) {
        const World world(1000, {.width = 1, .height = 1}); // 32 x 32 cells.
        std::vector<std::vector<std::uint64_t>> segments;
        const auto collect = [&](std::uint64_t row, std::uint64_t column, std::uint64_t count) {
            segments.push_back({row, column, count});
        };
        world.forEachRowSegment({30, 70}, collect);
        EXPECT_EQ(segments, (std::vector<std::vector<std::uint64_t>>{{0, 30, 2}, {1, 0, 32}, {2, 0, 32}, {3, 0, 4}}));
        segments.clear();
        world.forEachRowSegment({990, 50}, collect);
        EXPECT_EQ(segments, (std::vector<std::vector<std::uint64_t>>{{30, 30, 2}, {31, 0, 8}}));
        segments.clear();
        world.forEachRowSegment({5, 0}, collect);
        EXPECT_TRUE(segments.empty());
    }

    // Below one cell per pixel one level is drawn crisp; from there on the finer level fades out towards the next
    // one in log scale, and the coarsest level stands alone.
    TEST(World, BlendLevels) {
        const World world(1ULL << 30U, {.width = 16, .height = 9});
        const LevelBlend deep = world.blendFor(1.0 / 8.0);
        EXPECT_EQ(deep.fine, -3);
        EXPECT_EQ(deep.fineOpacity, 1.0);
        EXPECT_FALSE(deep.smooth);
        EXPECT_FALSE(world.blendFor(0.99).smooth);

        const LevelBlend one = world.blendFor(1.0);
        EXPECT_EQ(one.fine, 0);
        EXPECT_DOUBLE_EQ(one.fineOpacity, 1.0);
        EXPECT_TRUE(one.smooth);

        const LevelBlend between = world.blendFor(std::sqrt(2.0) * 4.0); // Halfway from level 2 to 3.
        EXPECT_EQ(between.fine, 2);
        EXPECT_NEAR(between.fineOpacity, 0.5, 1e-12);

        const LevelBlend almost = world.blendFor(7.99);
        EXPECT_EQ(almost.fine, 2);
        EXPECT_LT(almost.fineOpacity, 0.01);
        EXPECT_EQ(world.blendFor(8.0).fine, 3);
        EXPECT_DOUBLE_EQ(world.blendFor(8.0).fineOpacity, 1.0); // Continuous: level 3 alone at its own scale.

        const LevelBlend top = world.blendFor(World::cellsPerPixel(world.maxLevel()) * 1.5);
        EXPECT_EQ(top.fine, world.maxLevel());
        EXPECT_EQ(top.fineOpacity, 1.0); // Nothing coarser to fade to.
    }

}} // namespace icd::core
