#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/map/BinSummary.h"
#include "core/map/World.h"
#include "core/snapshot/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::core {

    // One bin of the pyramid: level and grid position (at level k, bin (x, y) covers cells
    // [x * 2^k, (x + 1) * 2^k) x [y * 2^k, (y + 1) * 2^k), which is pixel (x, y) of the level-k tiles).
    struct BinId {
        int level = 0; // Zoom level.
        std::uint64_t x = 0; // Bin column.
        std::uint64_t y = 0; // Bin row.
    };

    // What the pyramid is built from.
    struct PyramidInputs {
        const Snapshot& snapshot; // Extents and files.
        const World& world; // Cell layout of the volume.
        std::span<const std::uint8_t> fileKinds{}; // Class per file; empty until classification exists (all 0).
        std::span<const std::uint8_t> fileHeat{}; // Heat bucket per file; empty means all 0.
        std::uint64_t maxBaseBins = std::uint64_t{1} << 25U; // Base level limit (~32 M bins); tests lower it.
    };

    // Level-of-detail pyramid of the drive map (plan.md 7.2), a quadtree over the world grid: a bin of level k
    // covers 2^k x 2^k cells, exactly one pixel of a map tile at zoom level k, so coarse tiles are copied from
    // bins. The base is the first level with at most maxBaseBins bins (16 x 16 cells, 256 clusters, at 30 TB);
    // each level above merges 2 x 2 bins up to a single one. Finer levels are not stored: their tiles come
    // straight from the extent index. Built in parallel per band of base-bin rows (each band is one LCN range)
    // with results independent of the thread count, and updated per dirty LCN range.
    class Pyramid {
    public:
        // The first level with at most maxBins bins for a world.
        [[nodiscard]] static int baseLevelFor(const World& world, std::uint64_t maxBins);

        // Units of MapBin::used at a level (see binUsedShift()).
        [[nodiscard]] static unsigned usedShift(int level) { return binUsedShift(level); }

        // Builds every level.
        [[nodiscard]] static Pyramid build(const PyramidInputs& inputs, Executor& executor,
                                           const CancelToken& token = {});

        // Recomputes the bins over dirty after the snapshot changed there. The caller marks every range whose
        // summary may have changed, including all extents of a file whose fragmentation changed.
        void update(const PyramidInputs& inputs, ClusterRun dirty, Executor& executor, const CancelToken& token = {});

        // The stored base level.
        [[nodiscard]] int baseLevel() const { return base_; }

        // The level holding one bin.
        [[nodiscard]] int topLevel() const { return base_ + static_cast<int>(levels_.size()) - 1; }

        // Bins across a stored level (0 outside [baseLevel(), topLevel()]).
        [[nodiscard]] std::uint64_t binsAcross(int level) const;

        // Bins down a stored level (0 outside [baseLevel(), topLevel()]).
        [[nodiscard]] std::uint64_t binsDown(int level) const;

        // Bins of a stored level, row by row (empty outside [baseLevel(), topLevel()]).
        [[nodiscard]] std::span<const MapBin> level(int level) const;

        // A bin (an empty bin outside the stored levels or the grid).
        [[nodiscard]] MapBin bin(BinId id) const;

        // Used clusters of a bin as a count.
        [[nodiscard]] ClusterCount usedClusters(BinId id) const;

        // Bytes the bins take.
        [[nodiscard]] std::size_t memoryBytes() const;

    private:
        // Bins of one level and their grid size.
        struct Level {
            std::uint64_t across = 0; // Columns.
            std::uint64_t down = 0; // Rows.
            std::vector<MapBin> bins{}; // Row by row.
        };

        // Bin rows [first, last) of a level.
        struct RowRange {
            std::uint64_t first = 0; // First row.
            std::uint64_t last = 0; // One past the last row.
        };

        // Recomputes the base bins of some base-bin rows.
        void fillBaseRows(const PyramidInputs& inputs, std::span<const std::uint8_t> extentFlags, RowRange rows);

        // Recomputes some bin rows of a stored level (levels_ index) from the level below.
        void mergeRows(std::size_t index, RowRange rows);

        // Recomputes some base-bin rows and everything above them.
        void rebuildRows(const PyramidInputs& inputs, RowRange rows, Executor& executor, const CancelToken& token);

        // The stored level for a level number, or null.
        [[nodiscard]] const Level* stored(int level) const;

        int base_ = 0; // Stored base level.
        std::vector<Level> levels_; // levels_[k - base_]: bins of 2^k x 2^k cells.
    };

} // namespace icd::core
