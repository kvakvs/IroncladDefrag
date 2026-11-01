#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace icd::core {

    // What a scan of a volume will need in memory, predicted before it starts from the MFT record count (plan.md 6.6:
    // pick the memory mode up front instead of finding out halfway).
    struct SnapshotEstimate {
        std::uint64_t files = 0; // Files, at most one per MFT record.
        std::uint64_t extents = 0; // Extent records, from the design ratio of three per file.
        std::uint64_t snapshotBytes = 0; // The finished snapshot.
        std::uint64_t largeArrayBytes = 0; // Its part that mapped files can hold (extent index, name pool).
        std::uint64_t scanPeakBytes = 0; // Highest use while scanning: rows, drafts, pending extents and the snapshot.
    };

    // The estimate for a volume with this many MFT records (MftValidDataLength / bytes per record).
    [[nodiscard]] SnapshotEstimate estimateSnapshot(std::uint64_t mftRecords);

    // The give-way ladder of plan.md 6.6, in the order things are given up when memory is short.
    enum class MemoryRung : std::uint8_t {
        Full, // Everything kept in memory, with the speed-only extras.
        NoSpeedExtras, // Cached paths, per-stripe lookup indexes and extra planning tables rebuilt on demand.
        SmallMapCache, // Also a smaller tile cache (and a coarser first stored pyramid level).
        SerialLevels, // Also the three effort levels planned one after another.
        MappedStorage, // Also the extent index and name pool in memory-mapped files; planning by stripe.
    };

    // The memory mode a job runs in.
    struct MemoryPlan {
        MemoryRung rung = MemoryRung::Full; // How far down the ladder.
        std::uint64_t budgetBytes = 0; // The budget planned against (0: none).
        std::uint64_t neededBytes = 0; // Predicted use at this rung.

        // Whether speed-only extras may be kept.
        [[nodiscard]] bool speedExtras() const { return rung < MemoryRung::NoSpeedExtras; }

        // Bytes for the map's tile cache.
        [[nodiscard]] std::size_t tileCacheBytes() const;

        // Whether the effort levels may be planned in parallel.
        [[nodiscard]] bool parallelLevels() const { return rung < MemoryRung::SerialLevels; }

        // Whether the snapshot's large arrays go to memory-mapped files.
        [[nodiscard]] bool mappedStorage() const { return rung >= MemoryRung::MappedStorage; }
    };

    // The first rung whose predicted use fits the budget; the last rung when none does (it finishes anyway, slower).
    // A budget of 0 means no limit. Pure, so the same inputs always pick the same mode.
    [[nodiscard]] MemoryPlan planMemory(const SnapshotEstimate& estimate, std::uint64_t budgetBytes);

    // The next rung down, for when Windows signals low memory during a job (the last rung stays).
    [[nodiscard]] MemoryPlan giveWay(MemoryPlan plan, const SnapshotEstimate& estimate);

    // One line for the log and the Progress tab, e.g. "memory 2.0 GB budget, 3.4 GB needed in full: extent index and
    // names in mapped files".
    [[nodiscard]] std::string describeMemoryPlan(const MemoryPlan& plan);

} // namespace icd::core
