#include "core/resources/MemoryPlanner.h"

#include "core/format/ByteSize.h"
#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/FileTable.h"
#include "core/snapshot/SnapshotBuilder.h"

#include <array>
#include <format>

namespace icd::core {

    namespace {

        // Design ratio of extents to files (60 M / 20 M, plan.md 6.1).
        constexpr std::uint64_t extentsPerFile = 3;

        // Average name with its offset in the name pool.
        constexpr std::uint64_t nameBytes = 24;

        // Per-file rows and drafts while an MFT scan merges (record, name and segment rows, draft, entry).
        constexpr std::uint64_t scanBytesPerFile = 200;

        // Bytes per extent while scanning: the parser's entry, the draft's entry and the builder's sorted copy.
        constexpr std::uint64_t scanBytesPerExtent = 3 * sizeof(ExtentEntry);

        // Tile cache of the map in full and in the reduced mode.
        constexpr std::size_t fullTileCache = std::size_t{256} << 20U;
        constexpr std::size_t smallTileCache = std::size_t{64} << 20U;

        // Bytes a rung adds or removes on top of the scan peak.
        std::uint64_t extrasAt(MemoryRung rung, const SnapshotEstimate& e) {
            std::uint64_t bytes = rung < MemoryRung::NoSpeedExtras ? e.snapshotBytes / 4 : 0; // Paths, indexes.
            bytes += rung < MemoryRung::SmallMapCache ? fullTileCache : smallTileCache;
            bytes += rung < MemoryRung::SerialLevels ? e.snapshotBytes : e.snapshotBytes / 3; // Planning tables.
            return bytes;
        }

        // Predicted use at a rung.
        std::uint64_t neededAt(MemoryRung rung, const SnapshotEstimate& e) {
            const std::uint64_t mappedOut = rung >= MemoryRung::MappedStorage ? e.largeArrayBytes : 0;
            return e.scanPeakBytes - mappedOut + extrasAt(rung, e);
        }

        // Words for what a rung gives up.
        const char* rungText(MemoryRung rung) {
            constexpr std::array<const char*, 5> texts{"everything in memory", "speed extras rebuilt on demand",
                                                       "smaller map cache", "effort levels planned one at a time",
                                                       "extent index and names in mapped files"};
            return texts.at(static_cast<std::size_t>(rung));
        }

    } // namespace

    SnapshotEstimate estimateSnapshot(std::uint64_t mftRecords) {
        SnapshotEstimate e;
        e.files = mftRecords;
        e.extents = mftRecords * extentsPerFile;
        const std::uint64_t perExtent = sizeof(ExtentRecord) + sizeof(std::uint32_t); // Record and per-file position.
        e.largeArrayBytes = e.extents * perExtent + e.files * (nameBytes + sizeof(std::uint32_t));
        e.snapshotBytes = e.largeArrayBytes + e.files * FileTable::bytesPerFile;
        e.scanPeakBytes = e.snapshotBytes + e.files * scanBytesPerFile + e.extents * scanBytesPerExtent;
        return e;
    }

    std::size_t MemoryPlan::tileCacheBytes() const {
        return rung < MemoryRung::SmallMapCache ? fullTileCache : smallTileCache;
    }

    MemoryPlan planMemory(const SnapshotEstimate& estimate, std::uint64_t budgetBytes) {
        MemoryPlan plan{.rung = MemoryRung::Full, .budgetBytes = budgetBytes, .neededBytes = 0};
        while (plan.rung < MemoryRung::MappedStorage && budgetBytes != 0 &&
               neededAt(plan.rung, estimate) > budgetBytes) {
            plan.rung = static_cast<MemoryRung>(static_cast<int>(plan.rung) + 1);
        }
        plan.neededBytes = neededAt(plan.rung, estimate);
        return plan;
    }

    MemoryPlan giveWay(MemoryPlan plan, const SnapshotEstimate& estimate) {
        if (plan.rung < MemoryRung::MappedStorage) {
            plan.rung = static_cast<MemoryRung>(static_cast<int>(plan.rung) + 1);
        }
        plan.neededBytes = neededAt(plan.rung, estimate);
        return plan;
    }

    std::string describeMemoryPlan(const MemoryPlan& plan) {
        const std::string budget =
            plan.budgetBytes == 0 ? "no memory limit" : formatBytes(plan.budgetBytes) + " budget";
        return std::format("{}, about {} needed: {}", budget, formatBytes(plan.neededBytes), rungText(plan.rung));
    }

} // namespace icd::core
