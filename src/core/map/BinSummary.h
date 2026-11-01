#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <span>
#include <vector>

namespace icd::core {

    // Facts a bin carries besides its counts.
    enum MapBinFlag : std::uint8_t {
        BinUnmovable = 1U << 0U, // Holds clusters that must not move (metadata, locked files).
        BinDirectoryIndex = 1U << 1U, // Holds directory index allocations.
        BinPlanSource = 1U << 2U, // A planned move reads from here (plan overlay, set by the planner).
        BinPlanDestination = 1U << 3U, // A planned move writes here (plan overlay, set by the planner).
    };

    // Summary of a square block of map cells for drawing without touching every extent (plan.md 7.2), in 8
    // bytes so the base level of a 30 TB volume (~28 M bins) takes about 230 MB. The used count shares its word
    // with the size value: 24 bits of count are far more than any palette shade needs.
    struct MapBin {
        std::uint32_t used : 24 = 0; // Used clusters, in units of 2^binUsedShift(level).
        std::uint32_t size : 8 = 0; // Used-weighted average size value of the owners, 0-255 (size color mode).
        std::uint8_t kind = 0; // Dominant file class (the largest contributor at the finest summary).
        std::uint8_t fragmented = 0; // Share of used clusters owned by fragmented files, 0-255.
        std::uint8_t heat = 0; // Used-weighted average heat (age or activity) bucket of the owners, 0-255.
        std::uint8_t flags = 0; // MapBinFlag bits.

        // Bins compare as values (tests, change detection).
        friend constexpr bool operator==(const MapBin&, const MapBin&) = default;
    };

    static_assert(sizeof(MapBin) == 8, "map bins must stay 8 bytes (plan.md 7.2)");

    // Units of MapBin::used for a bin of 2^level x 2^level cells: 2^binUsedShift(level) clusters, so a full
    // bin always fits its 24 bits.
    [[nodiscard]] unsigned binUsedShift(int level);

    // Owner facts of one extent record for the map, one byte per record read in record order, so drawing
    // never jumps into the 20 M-entry file tables (two cache misses per extent otherwise).
    enum ExtentMapFlag : std::uint8_t {
        ExtentOwnerFragmented = 1U << 0U, // The owning file has more than one fragment.
        ExtentOwnerUnmovable = 1U << 1U, // The record or its file must not move.
        ExtentIsDirectoryIndex = 1U << 2U, // The record is a directory index allocation.
    };

    // Per file: 1 when it has more than one fragment, computed in parallel.
    [[nodiscard]] std::vector<std::uint8_t> fragmentedFileFlags(const Snapshot& snapshot, Executor& executor,
                                                                const CancelToken& token = {});

    // ExtentMapFlag bits for every record of a snapshot, in record order, computed in parallel. Recompute after
    // the snapshot changes.
    [[nodiscard]] std::vector<std::uint8_t> extentMapFlags(const Snapshot& snapshot, Executor& executor,
                                                           const CancelToken& token = {});

    // What the map knows about files and extents, shared by the pyramid and the tile rasterizer so both
    // summarise the same way.
    struct MapFileFacts {
        const Snapshot& snapshot; // Files and extents.
        std::span<const std::uint8_t> extentFlags; // Per record: ExtentMapFlag bits (extentMapFlags()).
        std::span<const std::uint8_t> kinds{}; // Class per file; empty means all 0.
        std::span<const std::uint8_t> heat{}; // Heat bucket per file; empty means all 0.
        std::span<const std::uint8_t> sizes{}; // Size value per file; empty means all 0.
    };

    // What one extent contributes to a summary, looked up once per record.
    struct PieceOwner {
        bool fragmented = false; // Owner has more than one fragment.
        std::uint8_t kind = 0; // Owner's class.
        std::uint8_t heat = 0; // Owner's heat bucket.
        std::uint8_t size = 0; // Owner's size value.
        std::uint8_t flags = 0; // MapBinFlag bits.
    };

    // The owner facts of the record at a position in the snapshot's extent index.
    [[nodiscard]] PieceOwner ownerOf(std::size_t record, const MapFileFacts& facts);

    // Collects the extent pieces of one square of cells and turns them into a MapBin.
    class BinAccumulator {
    public:
        // Adds clusters owned by owner.
        void add(ClusterCount clusters, const PieceOwner& owner);

        // The summary for a bin of 2^level x 2^level cells.
        [[nodiscard]] MapBin finish(int level) const;

    private:
        ClusterCount used_ = 0; // Used clusters.
        ClusterCount fragmented_ = 0; // Of those, owned by fragmented files.
        std::uint64_t heatSum_ = 0; // Sum of heat times clusters.
        std::uint64_t sizeSum_ = 0; // Sum of size value times clusters.
        ClusterCount largestPiece_ = 0; // Clusters of the largest contributing piece.
        std::uint8_t kind_ = 0; // Its owner's class.
        std::uint8_t flags_ = 0; // MapBinFlag bits.
    };

} // namespace icd::core
