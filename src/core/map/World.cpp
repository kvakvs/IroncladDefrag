#include "core/map/World.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace icd::core {

    std::uint64_t roundToNice(double value) {
        if (!(value >= 1.0)) {
            return 1; // Also catches NaN.
        }
        const auto whole = static_cast<std::uint64_t>(std::llround(value));
        const int bits = std::bit_width(whole);
        const std::uint64_t unit = std::uint64_t{1} << static_cast<unsigned>(std::max(0, bits - 4));
        const auto units = static_cast<std::uint64_t>(std::llround(value / static_cast<double>(unit)));
        return std::max<std::uint64_t>(unit, units * unit);
    }

    World::World(ClusterCount clusters, ScreenAspect aspect) : clusters_(clusters) {
        const bool usable = aspect.width > 0.0 && aspect.height > 0.0;
        const double ratio = usable ? aspect.width / aspect.height : 1.0;
        if (clusters_ > 0) {
            rowWidth_ = roundToNice(std::sqrt(static_cast<double>(clusters_) * ratio));
            rows_ = (clusters_ + rowWidth_ - 1) / rowWidth_;
        }
        const std::uint64_t extent = std::max(rowWidth_, rows_);
        maxLevel_ = minLevel;
        while (tileCellSpan(maxLevel_) < extent) {
            ++maxLevel_;
        }
    }

    std::optional<Lcn> World::lcnAt(CellPoint cell) const {
        if (cell.x >= rowWidth_ || cell.y >= rows_) {
            return std::nullopt;
        }
        const Lcn lcn = cell.y * rowWidth_ + cell.x;
        return lcn < clusters_ ? std::optional(lcn) : std::nullopt;
    }

    double World::cellsPerPixel(int level) { return std::ldexp(1.0, level); }

    int World::levelFor(double cellsPerPixelScale) const {
        if (!(cellsPerPixelScale > 0.0)) {
            return minLevel;
        }
        const auto level = static_cast<int>(std::floor(std::log2(cellsPerPixelScale)));
        return std::clamp(level, minLevel, maxLevel_);
    }

    std::uint64_t World::tileCellSpan(int level) {
        const auto size = static_cast<std::uint64_t>(tileSize);
        return level >= 0 ? size << static_cast<unsigned>(level)
                          : std::max<std::uint64_t>(1, size >> static_cast<unsigned>(-level));
    }

    std::uint64_t World::tilesAcross(int level) const {
        const std::uint64_t span = tileCellSpan(level);
        return (rowWidth_ + span - 1) / span;
    }

    std::uint64_t World::tilesDown(int level) const {
        const std::uint64_t span = tileCellSpan(level);
        return (rows_ + span - 1) / span;
    }

    CellRect World::tileCells(TileId tile) const {
        const std::uint64_t span = tileCellSpan(tile.level);
        const std::uint64_t x = tile.x * span;
        const std::uint64_t y = tile.y * span;
        if (x >= rowWidth_ || y >= rows_) {
            return {x, y, 0, 0};
        }
        return {x, y, std::min(span, rowWidth_ - x), std::min(span, rows_ - y)};
    }

    void World::forEachRowSegment(ClusterRun run, const RowSegment& segment) const {
        const Lcn end = std::min(run.end(), clusters_);
        for (Lcn lcn = run.start; lcn < end;) {
            const std::uint64_t column = lcn % rowWidth_;
            const std::uint64_t count = std::min(rowWidth_ - column, end - lcn);
            segment(lcn / rowWidth_, column, count);
            lcn += count;
        }
    }

} // namespace icd::core
