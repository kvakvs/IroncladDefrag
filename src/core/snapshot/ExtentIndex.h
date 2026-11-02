#pragma once

#include "core/snapshot/Types.h"
#include "core/util/LargeArray.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::core {

    // Saves and loads the private parts of snapshot classes (SnapshotIO.cpp).
    struct SnapshotAccess;

    // Builds a snapshot as changes to a saved one (SnapshotPatch.cpp).
    class SnapshotPatcher;

    // Facts about an extent that limit how it may be moved.
    enum ExtentFlag : std::uint8_t {
        ExtentUnmovable = 1U << 0U, // Metadata or locked data that must stay in place.
        ExtentCompressionUnit = 1U << 1U, // Part of a compressed file: moves only in whole units.
        ExtentDirectoryIndex = 1U << 2U, // A directory's $INDEX_ALLOCATION, not file data.
    };

    // Largest LCN an extent can hold (40 bits: 4 PB at 4 KB clusters).
    inline constexpr Lcn maxExtentLcn = (Lcn{1} << 40U) - 1;

    // Longest run one record can hold (24 bits: 64 GB at 4 KB clusters); longer runs are split.
    inline constexpr ClusterCount maxExtentLength = (ClusterCount{1} << 24U) - 1;

    // One run of clusters owned by one stream of one file, in 16 bytes (plan.md 6.2 principle 4): 60 M
    // extents at the design target take under 1 GB, and a sorted array of them is searched by LCN for the
    // map and the planner.
    class ExtentRecord {
    public:
        // What a record holds, named so callers cannot mix up the file, stream and flag numbers.
        struct Fields {
            ClusterRun run; // Clusters; must fit maxExtentLcn and maxExtentLength.
            FileIndex file = noFile; // Owning file.
            std::uint16_t stream = 0; // Stream ordinal.
            std::uint16_t flags = 0; // ExtentFlag bits.
        };

        // An empty record (no clusters at LCN 0, no file); arrays of records are loaded into these.
        ExtentRecord() = default;

        // A record; throws SnapshotError when the run does not fit.
        explicit ExtentRecord(const Fields& fields);

        // First cluster.
        [[nodiscard]] constexpr Lcn lcn() const { return packed_ & maxExtentLcn; }

        // Number of clusters.
        [[nodiscard]] constexpr ClusterCount length() const { return packed_ >> 40U; }

        // One past the last cluster.
        [[nodiscard]] constexpr Lcn end() const { return lcn() + length(); }

        // The run as a value.
        [[nodiscard]] constexpr ClusterRun run() const { return {lcn(), length()}; }

        // Owning file.
        [[nodiscard]] constexpr FileIndex file() const { return file_; }

        // Stream of the file: 0 is the main data stream; others are named streams or directory indexes.
        [[nodiscard]] constexpr std::uint16_t stream() const { return stream_; }

        // ExtentFlag bits.
        [[nodiscard]] constexpr std::uint16_t flags() const { return flags_; }

        // Records compare as values.
        friend constexpr bool operator==(const ExtentRecord&, const ExtentRecord&) = default;

    private:
        std::uint64_t packed_ = 0; // LCN in the low 40 bits, length in the high 24 bits.
        FileIndex file_ = noFile; // Owning file.
        std::uint16_t stream_ = 0; // Stream ordinal within the file.
        std::uint16_t flags_ = 0; // ExtentFlag bits.
    };

    static_assert(sizeof(ExtentRecord) == 16, "extent records must stay 16 bytes (plan.md 6.2 principle 4)");

    // All extents of a volume sorted by LCN (non-overlapping), plus each file's extents in VCN order. The
    // LCN order serves the map, hit tests and planning by stripe; the per-file order gives fragment i/n.
    class ExtentIndex {
    public:
        // An empty index on the heap.
        ExtentIndex() = default;

        // An empty index whose arrays live in `storage` (mapped files in the low-memory mode).
        explicit ExtentIndex(const std::shared_ptr<std::pmr::memory_resource>& storage) :
            records_(LargeArrayAllocator<ExtentRecord>(storage)),
            fileStart_(LargeArrayAllocator<std::uint32_t>(storage)),
            byFile_(LargeArrayAllocator<std::uint32_t>(storage)) {}

        // All records in LCN order.
        [[nodiscard]] std::span<const ExtentRecord> records() const { return records_; }

        // Number of records.
        [[nodiscard]] std::size_t size() const { return records_.size(); }

        // The record containing lcn, or null when the cluster is free or unknown.
        [[nodiscard]] const ExtentRecord* find(Lcn lcn) const;

        // Records overlapping [range.start, range.end()), in LCN order.
        [[nodiscard]] std::span<const ExtentRecord> overlapping(ClusterRun range) const;

        // Positions in records() of a file's extents, in VCN order; empty for resident or empty files.
        [[nodiscard]] std::span<const std::uint32_t> extentsOf(FileIndex file) const;

        // Fragments of a file: its extents in VCN order, counting a run that continues exactly where the
        // previous one ended (split long runs) as the same fragment.
        [[nodiscard]] std::size_t fragmentCount(FileIndex file) const;

    private:
        friend class SnapshotBuilder; // The only writer.
        friend struct SnapshotAccess; // Saves and loads the index (SnapshotIO).
        friend class SnapshotPatcher; // Patches a snapshot for a USN catch-up (SnapshotPatch).

        LargeVector<ExtentRecord> records_; // Sorted by LCN.
        LargeVector<std::uint32_t> fileStart_; // Per file: start in byFile_; one extra end marker.
        LargeVector<std::uint32_t> byFile_; // Record positions grouped by file, VCN order inside a file.
    };

} // namespace icd::core
