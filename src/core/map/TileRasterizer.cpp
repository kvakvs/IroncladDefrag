#include "core/map/TileRasterizer.h"

#include "core/snapshot/Stripes.h"

#include <algorithm>
#include <cstddef>
#include <span>

namespace icd::core {

    namespace {

        // Tile edge in pixels.
        constexpr std::uint64_t edge = World::tileSize;

        // Cells at least this many pixels wide get grid lines (plan.md 7.1 says 6 px; cell sizes are powers
        // of two, so that is 8 px and up).
        constexpr std::uint64_t gridCellPixels = 8;

        // A tile's summaries at the resolution they are computed: one unit per pixel, or per cell when a cell
        // spans several pixels.
        struct Units {
            int level = 0; // Summary level (0 for a single cell).
            std::uint64_t cellPixels = 1; // Pixels per cell edge (1 unless zoomed in past one cell per pixel).
            std::uint64_t across = 0; // Units across.
            std::uint64_t down = 0; // Units down.
            std::vector<Rgba> colors; // Color per unit, row by row.
            std::vector<ClusterCount> capacity; // Clusters on the volume per unit, row by row.
        };

        // Remembers the color of the previous summary. Neighbouring pixels usually summarise the same way (runs of
        // one extent, runs of free space), and the palette call is the costliest step per pixel.
        class ColorCache {
        public:
            // Colors summaries of a level with palette.
            ColorCache(const Palette& palette, int level) : palette_(palette), level_(level) {}

            // The color of a summary holding capacity clusters (0: outside the volume).
            Rgba get(const MapBin& bin, ClusterCount capacity) {
                const PixelSummary summary{.bin = bin, .level = level_, .capacity = capacity};
                if (!valid_ || summary != last_) {
                    last_ = summary;
                    color_ = capacity == 0 ? palette_.outside() : palette_.color(summary);
                    valid_ = true;
                }
                return color_;
            }

        private:
            const Palette& palette_; // Color source.
            int level_ = 0; // Level of the summaries.
            PixelSummary last_; // Last summary.
            Rgba color_ = 0; // Its color.
            bool valid_ = false; // Whether the fields above hold a value.
        };

        // ceil(value / 2^shift).
        std::uint64_t divideUp(std::uint64_t value, int shift) {
            return (value + (std::uint64_t{1} << static_cast<unsigned>(shift)) - 1) >> static_cast<unsigned>(shift);
        }

        // The first record at or after from whose end lies past lcn (records are sorted and disjoint). Tile rows
        // are walked top to bottom, so the answer is just ahead of the previous row's: galloping forward from
        // there touches a few nearby records instead of binary-searching the whole index per row.
        std::size_t firstEndingAfter(std::span<const ExtentRecord> records, std::size_t from, Lcn lcn) {
            if (from >= records.size() || records[from].end() > lcn) {
                return from;
            }
            std::size_t bound = 1;
            while (from + bound < records.size() && records[from + bound].end() <= lcn) {
                bound *= 2;
            }
            const auto first = records.begin() + static_cast<std::ptrdiff_t>(from + bound / 2);
            const auto last = records.begin() + static_cast<std::ptrdiff_t>(std::min(records.size(), from + bound + 1));
            const auto found =
                std::partition_point(first, last, [lcn](const ExtentRecord& r) { return r.end() <= lcn; });
            return static_cast<std::size_t>(found - records.begin());
        }

        // Adds the extents of one world row of the tile to the unit accumulators; cursor carries the search
        // position from row to row.
        void sweepRow(const MapSource& source, const CellRect& rect, std::uint64_t row, Units& units,
                      std::vector<BinAccumulator>& totals, std::size_t& cursor) {
            const World& world = source.world;
            const Lcn rowStart = row * world.rowWidth();
            const ClusterRun cells = clip({rowStart + rect.x, rect.width}, {0, world.clusters()});
            if (cells.length == 0) {
                return;
            }
            const std::uint64_t unitRow = (row - rect.y) >> static_cast<unsigned>(units.level);
            const auto records = source.facts.snapshot.extents.records();
            cursor = firstEndingAfter(records, cursor, cells.start);
            for (std::size_t index = cursor; index < records.size() && records[index].lcn() < cells.end(); ++index) {
                const PieceOwner owner = ownerOf(index, source.facts);
                const ClusterRun piece = clip(records[index].run(), cells);
                const std::uint64_t first = piece.start - rowStart - rect.x;
                const std::uint64_t last = first + piece.length;
                for (std::uint64_t column = first; column < last;) {
                    const std::uint64_t unit = column >> static_cast<unsigned>(units.level);
                    const std::uint64_t unitEnd = std::min((unit + 1) << static_cast<unsigned>(units.level), last);
                    totals[unitRow * units.across + unit].add(unitEnd - column, owner);
                    column = unitEnd;
                }
            }
        }

        // Summarises a tile's cells from the extent index and colors each unit. The buffers are per thread and
        // reused: a tile needs a few MB of scratch, and fresh blocks that large cost a page fault per 4 KB.
        const Units& unitsFromExtents(const MapSource& source, TileId tile, const Palette& palette) {
            const World& world = source.world;
            const CellRect rect = world.tileCells(tile);
            thread_local Units units;
            thread_local std::vector<BinAccumulator> totals;
            units.level = std::max(tile.level, 0);
            units.cellPixels = tile.level < 0 ? std::uint64_t{1} << static_cast<unsigned>(-tile.level) : 1;
            units.across = divideUp(rect.width, units.level);
            units.down = divideUp(rect.height, units.level);
            totals.assign(units.across * units.down, BinAccumulator{});
            std::size_t cursor = 0;
            for (std::uint64_t row = rect.y; row < rect.y + rect.height; ++row) {
                sweepRow(source, rect, row, units, totals, cursor);
            }
            const std::uint64_t span = std::uint64_t{1} << static_cast<unsigned>(units.level);
            units.colors.resize(totals.size());
            units.capacity.resize(totals.size());
            ColorCache colors(palette, units.level);
            for (std::uint64_t y = 0; y < units.down; ++y) {
                for (std::uint64_t x = 0; x < units.across; ++x) {
                    const std::size_t index = y * units.across + x;
                    const ClusterCount capacity =
                        clustersInRect(world, {rect.x + x * span, rect.y + y * span, span, span});
                    units.capacity[index] = capacity;
                    units.colors[index] = colors.get(totals[index].finish(units.level), capacity);
                }
            }
            return units;
        }

        // The color of a pixel of a tile drawn from units, with grid lines at deep zoom.
        Rgba unitPixel(const Units& units, std::uint64_t px, std::uint64_t py, const Palette& palette) {
            const std::uint64_t x = px / units.cellPixels;
            const std::uint64_t y = py / units.cellPixels;
            if (x >= units.across || y >= units.down) {
                return palette.outside();
            }
            const std::size_t index = y * units.across + x;
            const bool onGrid = units.cellPixels >= gridCellPixels &&
                (px % units.cellPixels == units.cellPixels - 1 || py % units.cellPixels == units.cellPixels - 1);
            return onGrid && units.capacity[index] != 0 ? palette.gridLine() : units.colors[index];
        }

        // Paints a tile from the extent index.
        void fromExtents(const MapSource& source, TileId tile, const Palette& palette, std::vector<Rgba>& pixels) {
            const Units& units = unitsFromExtents(source, tile, palette);
            if (units.cellPixels == 1) {
                // One unit per pixel: copy unit rows; the rest of the tile keeps the outside color.
                for (std::uint64_t y = 0; y < units.down; ++y) {
                    std::copy_n(units.colors.begin() + static_cast<std::ptrdiff_t>(y * units.across), units.across,
                                pixels.begin() + static_cast<std::ptrdiff_t>(y * edge));
                }
                return;
            }
            for (std::uint64_t py = 0; py < edge; ++py) {
                for (std::uint64_t px = 0; px < edge; ++px) {
                    pixels[py * edge + px] = unitPixel(units, px, py, palette);
                }
            }
        }

        // Paints a tile from the pyramid: one bin per pixel.
        void fromPyramid(const MapSource& source, TileId tile, const Palette& palette, std::vector<Rgba>& pixels) {
            const std::uint64_t span = std::uint64_t{1} << static_cast<unsigned>(tile.level);
            ColorCache colors(palette, tile.level);
            for (std::uint64_t py = 0; py < edge; ++py) {
                for (std::uint64_t px = 0; px < edge; ++px) {
                    const BinId bin{.level = tile.level, .x = tile.x * edge + px, .y = tile.y * edge + py};
                    const ClusterCount capacity =
                        clustersInRect(source.world, {bin.x * span, bin.y * span, span, span});
                    pixels[py * edge + px] = colors.get(source.pyramid.bin(bin), capacity);
                }
            }
        }

    } // namespace

    ClusterCount clustersInRect(const World& world, CellRect rect) {
        const std::uint64_t x1 = std::min(world.rowWidth(), rect.x + rect.width);
        const std::uint64_t y1 = std::min(world.rows(), rect.y + rect.height);
        if (world.clusters() == 0 || rect.x >= x1 || rect.y >= y1) {
            return 0;
        }
        const CellPoint last = world.cellOf(world.clusters() - 1);
        // Rows above the last one are full; the last row ends at the last cluster.
        const std::uint64_t fullRows = last.y > rect.y ? std::min(y1, last.y) - rect.y : 0;
        ClusterCount count = fullRows * (x1 - rect.x);
        if (last.y >= rect.y && last.y < y1 && last.x >= rect.x) {
            count += std::min(x1, last.x + 1) - rect.x;
        }
        return count;
    }

    std::vector<Rgba> renderTile(const MapSource& source, TileId tile, const Palette& palette, TileSource from) {
        std::vector<Rgba> pixels(tilePixels, palette.outside());
        const bool stored = tile.level >= source.pyramid.baseLevel() && tile.level <= source.pyramid.topLevel();
        const bool coarse =
            from == TileSource::Pyramid || (from == TileSource::Auto && tile.level >= source.pyramid.baseLevel());
        if (!coarse) {
            fromExtents(source, tile, palette, pixels);
        }
        else if (stored) {
            fromPyramid(source, tile, palette, pixels);
        }
        // Levels above the pyramid's top lie beyond World::maxLevel(); their tiles stay outside-colored.
        return pixels;
    }

} // namespace icd::core
