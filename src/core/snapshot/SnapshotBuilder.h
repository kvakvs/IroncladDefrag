#pragma once

#include "core/snapshot/FileTable.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace icd::core {

    // One file as a scanner reports it.
    struct FileEntry {
        std::string_view name; // Own name; copied into the name pool.
        FileIndex parent = noFile; // Containing directory (may be added later), or noFile for a root.
        std::uint64_t sizeBytes = 0; // Logical size of the main stream.
        std::uint16_t flags = 0; // FileFlag bits.
        FileTimes times; // Timestamps.
        std::uint64_t fileReference = 0; // NTFS file reference number.
    };

    // One run of one stream as a scanner reports it, before sorting and splitting. Fields are ordered for a
    // 32-byte layout: tens of millions of these are pending at once during a full scan.
    struct ExtentEntry {
        ClusterRun run; // Clusters on disk.
        std::uint64_t vcn = 0; // Position of the run inside its stream, for the per-file order.
        FileIndex file = noFile; // Owning file.
        std::uint16_t stream = 0; // Stream ordinal; 0 is the main data stream.
        std::uint16_t flags = 0; // ExtentFlag bits.
    };

    static_assert(sizeof(ExtentEntry) == 32, "pending extents should stay 32 bytes");

    // Collects files, extents and free runs in any order and turns them into a Snapshot: sorts extents by
    // LCN, splits runs longer than a record holds, groups each file's extents in VCN order, builds the
    // directory tree and merges free runs. Input order never changes the result (plan.md 6.2 principle 8).
    // Scanner workers can fill their own ExtentEntry vectors and hand them over with addExtents().
    class SnapshotBuilder {
    public:
        // Starts an empty snapshot of a volume.
        explicit SnapshotBuilder(VolumeGeometry geometry);

        // Adds a file and returns its index (files are numbered in the order they are added).
        FileIndex addFile(const FileEntry& entry);

        // Adds one extent.
        void addExtent(const ExtentEntry& entry);

        // Adds a batch of extents, e.g. one worker's thread-local buffer.
        void addExtents(std::vector<ExtentEntry> batch);

        // Adds a free run.
        void addFreeRun(ClusterRun run);

        // Checks and builds the snapshot; the builder is spent afterwards. Throws SnapshotError for extents
        // outside the volume, overlapping extents, unknown files or bad parent indexes.
        [[nodiscard]] Snapshot build() &&;

    private:
        // Checks extents, splits long runs and sorts them into the index.
        void buildExtents(Snapshot& snapshot);

        // Groups extent positions by file in VCN order and sums each file's clusters.
        void groupByFile(Snapshot& snapshot);

        Snapshot snapshot_; // Being filled; names and file columns are written directly.
        std::vector<ExtentEntry> extents_; // Pending extents.
        std::vector<ClusterRun> freeRuns_; // Pending free runs.
        std::vector<std::uint64_t> recordVcns_; // VCN of each sorted record, used while grouping by file.
    };

} // namespace icd::core
