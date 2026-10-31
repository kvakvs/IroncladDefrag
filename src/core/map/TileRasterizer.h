#pragma once

#include "core/map/BinSummary.h"
#include "core/map/Palette.h"
#include "core/map/Pyramid.h"
#include "core/map/World.h"

#include <cstdint>
#include <vector>

namespace icd::core {

    // Pixels in one tile.
    inline constexpr std::size_t tilePixels = std::size_t{World::tileSize} * World::tileSize;

    // Everything a tile is drawn from. Read-only while tiles render, so tiles render on many workers at once.
    struct MapSource {
        const World& world; // Cell layout.
        const Pyramid& pyramid; // Coarse summaries.
        MapFileFacts facts; // Snapshot and per-file facts for fine tiles.
    };

    // Where a tile's pixels come from.
    enum class TileSource : std::uint8_t {
        Auto, // The pyramid when the level is stored, the extent index below it.
        Pyramid, // Force the pyramid (levels it stores only).
        Extents, // Force the extent index (any level; for tests that compare both).
    };

    // Clusters of the volume inside a cell rectangle: clipped to the world and to the last cluster.
    [[nodiscard]] ClusterCount clustersInRect(const World& world, CellRect rect);

    // Renders one 256 x 256 tile, row by row (plan.md 7.3). Coarse levels copy one pyramid bin per pixel; finer
    // levels summarise the extents of each pixel (or each cell, when a cell is bigger than a pixel) the same way
    // the pyramid does. Cells of 8 px or more get 1 px grid lines (plan.md 7.1). Pixels outside the volume get
    // the palette's outside color.
    [[nodiscard]] std::vector<Rgba> renderTile(const MapSource& source, TileId tile, const Palette& palette,
                                               TileSource from = TileSource::Auto);

} // namespace icd::core
