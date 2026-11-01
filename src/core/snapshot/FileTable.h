#pragma once

#include "core/snapshot/NamePool.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace icd::core {

    // Saves and loads the private parts of snapshot classes (SnapshotIO.cpp).
    struct SnapshotAccess;

    // Facts about a file that change how it may be treated (plan.md 10.2 special handling).
    enum FileFlag : std::uint16_t {
        FileDirectory = 1U << 0U, // A directory; its index allocation is placed first (plan.md 8.3).
        FileResident = 1U << 1U, // Data lives in the MFT record: no clusters, never moved.
        FileCompressed = 1U << 2U, // NTFS-compressed: moved in whole 16-cluster compression units.
        FileSparse = 1U << 3U, // Sparse: only allocated runs exist.
        FileEncrypted = 1U << 4U, // EFS-encrypted: moved only where supported.
        FileReparsePoint = 1U << 5U, // Reparse point (symlink, junction, dedup, cloud placeholder).
        FileMetadata = 1U << 6U, // NTFS metadata file ($MFT, $LogFile, ...).
        FileHardLinked = 1U << 7U, // More than one name; stored once, counted once.
        FileAlternateStreams = 1U << 8U, // Has named data streams besides the main one.
        FileUnmovable = 1U << 9U, // Must not be moved (in use by the system, pinned by policy).
    };

    // The three NTFS timestamps, as FILETIME values (100 ns ticks since 1601, UTC).
    struct FileTimes {
        std::int64_t created = 0; // Creation time.
        std::int64_t modified = 0; // Last data change.
        std::int64_t accessed = 0; // Last access; only meaningful when the volume tracks it (plan.md 8.2).
    };

    // Per-file data of a snapshot as structure of arrays: each column is one vector indexed by FileIndex, so
    // passes that read one property (classification by size, age maps) stream through memory instead of
    // jumping over whole records (plan.md 6.2 principle 4). Filled by SnapshotBuilder.
    class FileTable {
    public:
        // Number of files.
        [[nodiscard]] std::size_t size() const { return names_.size(); }

        // The file's own name (not the path).
        [[nodiscard]] NameId name(FileIndex file) const { return names_.at(file); }

        // The containing directory, or noFile for a root.
        [[nodiscard]] FileIndex parent(FileIndex file) const { return parents_.at(file); }

        // Logical size in bytes (main data stream).
        [[nodiscard]] std::uint64_t sizeBytes(FileIndex file) const { return sizes_.at(file); }

        // Clusters allocated to the file over all its streams.
        [[nodiscard]] ClusterCount allocatedClusters(FileIndex file) const { return clusters_.at(file); }

        // FileFlag bits.
        [[nodiscard]] std::uint16_t flags(FileIndex file) const { return flags_.at(file); }

        // Whether a flag is set.
        [[nodiscard]] bool has(FileIndex file, FileFlag flag) const { return (flags_.at(file) & flag) != 0; }

        // Timestamps.
        [[nodiscard]] const FileTimes& times(FileIndex file) const { return times_.at(file); }

        // NTFS file reference number (MFT record and sequence), for USN catch-up and opening by id.
        [[nodiscard]] std::uint64_t fileReference(FileIndex file) const { return references_.at(file); }

        // Bytes stored per file over all columns, for memory estimates (plan.md 6.6).
        static constexpr std::size_t bytesPerFile = sizeof(NameId) + sizeof(FileIndex) + sizeof(std::uint64_t) +
            sizeof(ClusterCount) + sizeof(std::uint16_t) + sizeof(FileTimes) + sizeof(std::uint64_t);

    private:
        friend class SnapshotBuilder; // The only writer.
        friend struct SnapshotAccess; // Saves and loads the columns (SnapshotIO).

        std::vector<NameId> names_; // Own name.
        std::vector<FileIndex> parents_; // Containing directory.
        std::vector<std::uint64_t> sizes_; // Logical size.
        std::vector<ClusterCount> clusters_; // Allocated clusters (sum of extents).
        std::vector<std::uint16_t> flags_; // FileFlag bits.
        std::vector<FileTimes> times_; // Timestamps.
        std::vector<std::uint64_t> references_; // NTFS file reference numbers.
    };

    // At the design target (20 M files) the table must stay well under the 8 GB snapshot budget.
    static_assert(FileTable::bytesPerFile <= 64, "per-file columns grew; check plan.md 6.5 memory targets");

} // namespace icd::core
