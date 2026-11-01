#pragma once

#include "core/snapshot/Types.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace icd::core {

    // Rounds value to a "nice" row width: at most four significant binary digits (a multiple of a large power
    // of two). Pyramid bins up to that power of two then never wrap around the end of a row.
    [[nodiscard]] std::uint64_t roundToNice(double value);

    // Width and height of the screen a world is laid out for (any unit; only the ratio matters).
    struct ScreenAspect {
        double width = 16.0; // Horizontal extent.
        double height = 9.0; // Vertical extent.
    };

    // A cell of the world grid: column and row, one cluster per cell.
    struct CellPoint {
        std::uint64_t x = 0; // Column, 0 at the left.
        std::uint64_t y = 0; // Row, 0 at the top.

        // Points compare as values.
        friend constexpr bool operator==(const CellPoint&, const CellPoint&) = default;
    };

    // A rectangle of cells [x, x + width) x [y, y + height).
    struct CellRect {
        std::uint64_t x = 0; // Left column.
        std::uint64_t y = 0; // Top row.
        std::uint64_t width = 0; // Columns.
        std::uint64_t height = 0; // Rows.
    };

    // One map tile: tileSize x tileSize pixels at a zoom level.
    struct TileId {
        int level = 0; // Zoom level: one pixel is 2^level cells across (negative: a cell spans several pixels).
        std::uint64_t x = 0; // Tile column.
        std::uint64_t y = 0; // Tile row.

        // Ids compare as values (cache keys).
        friend constexpr bool operator==(const TileId&, const TileId&) = default;
    };

    // How the map composes tile levels for a scale. Tiles exist per power-of-two level and are drawn scaled in
    // between. Where a cell is smaller than a pixel, shrinking a tile with nearest-neighbour sampling drops whole
    // texel rows and columns, so one-cluster details (and tile edges) blink in and out while zooming. There the map
    // draws the next coarser level (whose pixels already average their clusters) and fades the finer level over it
    // by how far the scale has moved between the two, both filtered linearly: mipmapping. Where cells are bigger
    // than a pixel no detail is lost, so one level is drawn crisp with nearest-neighbour sampling.
    struct LevelBlend {
        int fine = 0; // The finer level (World::levelFor()).
        double fineOpacity = 1.0; // 1: the fine level alone; below 1: fine + 1 drawn under it, fine at this opacity.
        bool smooth = false; // Linear filtering (cells under a pixel) rather than nearest-neighbour.
    };

    // The fixed 2-D layout of a volume for the drive map (plan.md 7.1): clusters row by row, row width chosen
    // once from the screen aspect so the whole volume fills a screen. It never re-flows on resize, zoom or a
    // move to another screen, so places stay where the user saw them. Also the zoom-level and tile math the
    // renderer and the navigator share.
    class World {
    public:
        // Tile edge in pixels.
        static constexpr int tileSize = 256;

        // Finest level: a cluster is 2^5 = 32 px wide (plan.md 7.1).
        static constexpr int minLevel = -5;

        // Receives one row piece of a run: row, first column, column count.
        using RowSegment = std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>;

        // Lays out clusters for a screen; an aspect that is not positive counts as square.
        World(ClusterCount clusters, ScreenAspect aspect);

        // Clusters on the volume.
        [[nodiscard]] ClusterCount clusters() const { return clusters_; }

        // Cells per row.
        [[nodiscard]] std::uint64_t rowWidth() const { return rowWidth_; }

        // Rows (the last one may be partly empty).
        [[nodiscard]] std::uint64_t rows() const { return rows_; }

        // The cell of a cluster.
        [[nodiscard]] CellPoint cellOf(Lcn lcn) const { return {lcn % rowWidth_, lcn / rowWidth_}; }

        // The cluster at a cell, or nullopt outside the volume (past the row end or the last cluster).
        [[nodiscard]] std::optional<Lcn> lcnAt(CellPoint cell) const;

        // Coarsest level: the whole world fits in one tile.
        [[nodiscard]] int maxLevel() const { return maxLevel_; }

        // Cells per pixel edge at a level (2^level).
        [[nodiscard]] static double cellsPerPixel(int level);

        // The level to render for a continuous scale (cells per pixel): the nearest finer one, clamped to
        // [minLevel, maxLevel()], so tiles are drawn shrunk rather than blown up.
        [[nodiscard]] int levelFor(double cellsPerPixelScale) const;

        // The levels to compose for a scale (see LevelBlend): from 1 cell per pixel on, the finer level fades out
        // linearly in log scale towards the next one; at the coarsest level there is nothing to fade to.
        [[nodiscard]] LevelBlend blendFor(double cellsPerPixelScale) const;

        // Tiles across and down at a level.
        [[nodiscard]] std::uint64_t tilesAcross(int level) const;

        // Tiles down at a level.
        [[nodiscard]] std::uint64_t tilesDown(int level) const;

        // The cells a tile covers (clipped to the world; empty for tiles outside it).
        [[nodiscard]] CellRect tileCells(TileId tile) const;

        // Calls segment(row, firstColumn, columnCount) for each row piece of a cluster run, top to bottom. A
        // run is contiguous in LCN but wraps at row ends; drawing and hit tests work row by row.
        void forEachRowSegment(ClusterRun run, const RowSegment& segment) const;

    private:
        // Cells a tile edge covers at a level (at least 1).
        [[nodiscard]] static std::uint64_t tileCellSpan(int level);

        ClusterCount clusters_ = 0; // Volume size.
        std::uint64_t rowWidth_ = 1; // Cells per row.
        std::uint64_t rows_ = 0; // Rows.
        int maxLevel_ = 0; // Coarsest level.
    };

} // namespace icd::core
