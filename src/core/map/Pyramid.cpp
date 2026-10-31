#include "core/map/Pyramid.h"

#include "core/snapshot/Stripes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace icd::core {

    namespace {

        // Base-bin rows per parallel chunk (one row is one LCN range of 2^base world rows).
        constexpr std::size_t rowsPerChunk = 4;

        // Running totals of one base bin while its extents are swept.
        struct BinTotals {
            ClusterCount used = 0; // Used clusters.
            ClusterCount fragmented = 0; // Of those, owned by fragmented files.
            std::uint64_t heatSum = 0; // Sum of heat times clusters.
            ClusterCount largestPiece = 0; // Clusters of the largest contributing piece.
            std::uint8_t kind = 0; // Its owner's class.
            std::uint8_t flags = 0; // MapBinFlag bits.
        };

        // What one extent piece contributes, looked up once per record.
        struct PieceOwner {
            bool fragmented = false; // Owner has more than one fragment.
            std::uint8_t kind = 0; // Owner's class.
            std::uint8_t heat = 0; // Owner's heat bucket.
            std::uint8_t flags = 0; // MapBinFlag bits.
        };

        // Size of a bin grid.
        struct Grid {
            std::uint64_t across = 1; // Columns.
            std::uint64_t down = 1; // Rows.
        };

        // Adds clusters of a piece to a bin's totals.
        void add(BinTotals& totals, ClusterCount clusters, const PieceOwner& owner) {
            totals.used += clusters;
            totals.fragmented += owner.fragmented ? clusters : 0;
            totals.heatSum += clusters * owner.heat;
            totals.flags |= owner.flags;
            if (clusters > totals.largestPiece) {
                totals.largestPiece = clusters;
                totals.kind = owner.kind;
            }
        }

        // A 0-255 value: part / whole scaled by scale, rounded.
        std::uint8_t ratio(double part, double whole, double scale) {
            return whole <= 0.0 ? 0 : static_cast<std::uint8_t>(std::lround(scale * part / whole));
        }

        // The finished base bin of a level.
        MapBin finish(const BinTotals& totals, int level) {
            const auto used = static_cast<double>(totals.used);
            MapBin bin;
            bin.used = static_cast<std::uint32_t>(totals.used >> Pyramid::usedShift(level));
            bin.kind = totals.kind;
            bin.fragmented = ratio(static_cast<double>(totals.fragmented), used, 255.0);
            bin.heat = ratio(static_cast<double>(totals.heatSum), used, 1.0);
            bin.flags = totals.flags;
            return bin;
        }

        // The owner facts of a record.
        PieceOwner ownerOf(const ExtentRecord& record, const PyramidInputs& inputs,
                           std::span<const std::uint8_t> fragmentedFiles) {
            const FileIndex file = record.file();
            PieceOwner owner;
            owner.fragmented = fragmentedFiles[file] != 0;
            owner.kind = file < inputs.fileKinds.size() ? inputs.fileKinds[file] : 0;
            owner.heat = file < inputs.fileHeat.size() ? inputs.fileHeat[file] : 0;
            const bool unmovable =
                (record.flags() & ExtentUnmovable) != 0 || inputs.snapshot.files.has(file, FileUnmovable);
            const bool directoryIndex = (record.flags() & ExtentDirectoryIndex) != 0;
            owner.flags =
                static_cast<std::uint8_t>((unmovable ? BinUnmovable : 0) | (directoryIndex ? BinDirectoryIndex : 0));
            return owner;
        }

        // Per file: 1 when it has more than one fragment, computed in parallel.
        std::vector<std::uint8_t> fragmentedFiles(const Snapshot& snapshot, Executor& executor,
                                                  const CancelToken& token) {
            std::vector<std::uint8_t> out(snapshot.files.size(), 0);
            executor.parallelFor(
                {.count = out.size(), .grain = 65536},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t file = begin; file < end; ++file) {
                        out[file] = snapshot.extents.fragmentCount(static_cast<FileIndex>(file)) > 1 ? 1 : 0;
                    }
                },
                token);
            return out;
        }

        // The bin grid of a world at a level: ceil(size / 2^level) bins each way, at least one.
        Grid gridOf(const World& world, int level) {
            const std::uint64_t span = std::uint64_t{1} << static_cast<unsigned>(level);
            return {.across = std::max<std::uint64_t>(1, (world.rowWidth() + span - 1) / span),
                    .down = std::max<std::uint64_t>(1, (world.rows() + span - 1) / span)};
        }

        // The grid one level up.
        Grid halved(Grid grid) {
            return {.across = std::max<std::uint64_t>(1, (grid.across + 1) / 2),
                    .down = std::max<std::uint64_t>(1, (grid.down + 1) / 2)};
        }

        // Merges up to four children of level - 1 into their parent bin of level.
        MapBin merge(const std::array<MapBin, 4>& children, int level) {
            const unsigned childShift = Pyramid::usedShift(level - 1);
            ClusterCount total = 0;
            double fragmentedSum = 0.0;
            double heatSum = 0.0;
            ClusterCount largest = 0;
            MapBin parent;
            for (const MapBin& child : children) {
                const ClusterCount used = ClusterCount{child.used} << childShift;
                total += used;
                fragmentedSum += static_cast<double>(used) * child.fragmented;
                heatSum += static_cast<double>(used) * child.heat;
                parent.flags |= child.flags;
                if (used > largest) {
                    largest = used;
                    parent.kind = child.kind;
                }
            }
            parent.used = static_cast<std::uint32_t>(total >> Pyramid::usedShift(level));
            parent.fragmented = ratio(fragmentedSum, static_cast<double>(total), 1.0);
            parent.heat = ratio(heatSum, static_cast<double>(total), 1.0);
            return parent;
        }

    } // namespace

    int Pyramid::baseLevelFor(const World& world, std::uint64_t maxBins) {
        int level = 0;
        while (level < 40) {
            const Grid grid = gridOf(world, level);
            if (grid.across * grid.down <= std::max<std::uint64_t>(maxBins, 1)) {
                break;
            }
            ++level;
        }
        return level;
    }

    unsigned Pyramid::usedShift(int level) { return 2 * level > 31 ? static_cast<unsigned>(2 * level - 31) : 0U; }

    Pyramid Pyramid::build(const PyramidInputs& inputs, Executor& executor, const CancelToken& token) {
        Pyramid pyramid;
        pyramid.base_ = baseLevelFor(inputs.world, inputs.maxBaseBins);
        Grid grid = gridOf(inputs.world, pyramid.base_);
        while (true) {
            Level level{.across = grid.across, .down = grid.down};
            level.bins.assign(grid.across * grid.down, MapBin{});
            pyramid.levels_.push_back(std::move(level));
            if (grid.across == 1 && grid.down == 1) {
                break;
            }
            grid = halved(grid);
        }
        pyramid.rebuildRows(inputs, {.first = 0, .last = pyramid.levels_.front().down}, executor, token);
        return pyramid;
    }

    void Pyramid::update(const PyramidInputs& inputs, ClusterRun dirty, Executor& executor, const CancelToken& token) {
        if (dirty.length == 0 || levels_.empty()) {
            return;
        }
        const std::uint64_t width = inputs.world.rowWidth();
        const auto shift = static_cast<unsigned>(base_);
        const std::uint64_t first = (dirty.start / width) >> shift;
        const std::uint64_t last = std::min(levels_.front().down, (((dirty.end() - 1) / width) >> shift) + 1);
        rebuildRows(inputs, {.first = first, .last = last}, executor, token);
    }

    void Pyramid::rebuildRows(const PyramidInputs& inputs, RowRange rows, Executor& executor,
                              const CancelToken& token) {
        // Runs body on chunks of rows in parallel.
        const auto forChunks = [&](RowRange range, const auto& body) {
            if (range.last <= range.first) {
                return;
            }
            executor.parallelFor(
                {.count = static_cast<std::size_t>(range.last - range.first), .grain = rowsPerChunk},
                [&](std::size_t begin, std::size_t end) {
                    body(RowRange{.first = range.first + begin, .last = range.first + end});
                },
                token);
        };
        const std::vector<std::uint8_t> fragmented = fragmentedFiles(inputs.snapshot, executor, token);
        forChunks(rows, [&](RowRange chunk) { fillBaseRows(inputs, fragmented, chunk); });
        for (std::size_t index = 1; index < levels_.size(); ++index) {
            rows = {.first = rows.first / 2, .last = std::min(levels_[index].down, (rows.last + 1) / 2)};
            forChunks(rows, [&](RowRange chunk) { mergeRows(index, chunk); });
        }
    }

    void Pyramid::fillBaseRows(const PyramidInputs& inputs, std::span<const std::uint8_t> fragmentedFiles,
                               RowRange rows) {
        const World& world = inputs.world;
        const auto shift = static_cast<unsigned>(base_);
        Level& base = levels_.front();
        std::vector<BinTotals> totals(base.across);
        for (std::uint64_t row = rows.first; row < rows.last; ++row) {
            std::ranges::fill(totals, BinTotals{});
            // A row of base bins is the LCN range of its world rows.
            const Lcn start = std::min(world.clusters(), (row << shift) * world.rowWidth());
            const Lcn end = std::min(world.clusters(), ((row + 1) << shift) * world.rowWidth());
            const ClusterRun range{start, end - start};
            for (const ExtentRecord& record : inputs.snapshot.extents.overlapping(range)) {
                const PieceOwner owner = ownerOf(record, inputs, fragmentedFiles);
                world.forEachRowSegment(
                    clip(record.run(), range), [&](std::uint64_t /*row*/, std::uint64_t column, std::uint64_t count) {
                        // Spread the segment over the bin columns it crosses.
                        for (std::uint64_t cell = column; cell < column + count;) {
                            const std::uint64_t binEnd = std::min(((cell >> shift) + 1) << shift, column + count);
                            add(totals[cell >> shift], binEnd - cell, owner);
                            cell = binEnd;
                        }
                    });
            }
            for (std::uint64_t column = 0; column < base.across; ++column) {
                base.bins[row * base.across + column] = finish(totals[column], base_);
            }
        }
    }

    void Pyramid::mergeRows(std::size_t index, RowRange rows) {
        const Level& below = levels_[index - 1];
        Level& level = levels_[index];
        const int number = base_ + static_cast<int>(index);
        const auto child = [&](std::uint64_t x, std::uint64_t y) {
            return x < below.across && y < below.down ? below.bins[y * below.across + x] : MapBin{};
        };
        for (std::uint64_t y = rows.first; y < rows.last; ++y) {
            for (std::uint64_t x = 0; x < level.across; ++x) {
                const std::array children{child(2 * x, 2 * y), child(2 * x + 1, 2 * y), child(2 * x, 2 * y + 1),
                                          child(2 * x + 1, 2 * y + 1)};
                level.bins[y * level.across + x] = merge(children, number);
            }
        }
    }

    const Pyramid::Level* Pyramid::stored(int level) const {
        if (level < base_ || level > topLevel()) {
            return nullptr;
        }
        return &levels_[static_cast<std::size_t>(level - base_)];
    }

    std::uint64_t Pyramid::binsAcross(int level) const {
        const Level* found = stored(level);
        return found != nullptr ? found->across : 0;
    }

    std::uint64_t Pyramid::binsDown(int level) const {
        const Level* found = stored(level);
        return found != nullptr ? found->down : 0;
    }

    std::span<const MapBin> Pyramid::level(int level) const {
        const Level* found = stored(level);
        return found != nullptr ? std::span<const MapBin>(found->bins) : std::span<const MapBin>();
    }

    MapBin Pyramid::bin(BinId id) const {
        const Level* found = stored(id.level);
        if (found == nullptr || id.x >= found->across || id.y >= found->down) {
            return {};
        }
        return found->bins[id.y * found->across + id.x];
    }

    ClusterCount Pyramid::usedClusters(BinId id) const { return ClusterCount{bin(id).used} << usedShift(id.level); }

    std::size_t Pyramid::memoryBytes() const {
        std::size_t bytes = 0;
        for (const Level& level : levels_) {
            bytes += level.bins.size() * sizeof(MapBin);
        }
        return bytes;
    }

} // namespace icd::core
