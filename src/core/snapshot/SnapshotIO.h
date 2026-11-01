#pragma once

#include "core/exec/Executor.h"
#include "core/map/World.h"
#include "core/snapshot/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Saved snapshots (plan.md 6.2 principle 6, 7.1): the whole snapshot with the USN cursor and the map aspect, so the
// next run catches up from the journal instead of rescanning and the map keeps its layout. Pure: the file format,
// framing, checksums and validation live here; compression and files are plugged in by services.
namespace icd::core {

    // Version of the snapshot file format; files of any other version are rejected (and rescanned), never migrated.
    inline constexpr std::uint32_t snapshotFormatVersion = 1;

    // Thrown when a snapshot file is damaged, truncated, of another version or inconsistent. The caller rescans.
    class SnapshotFormatError : public std::runtime_error {
    public:
        // Keeps the message.
        explicit SnapshotFormatError(const std::string& message) : std::runtime_error(message) {}
    };

    // What is stored with a snapshot besides its data, readable without loading the data (the "saved snapshot"
    // badge, the catch-up decision).
    struct SnapshotMeta {
        std::string volumeName; // "\\?\Volume{GUID}\" the snapshot describes.
        std::uint64_t volumeSerial = 0; // NTFS serial: a reformatted volume has another, so the snapshot is void.
        std::int64_t scanTime = 0; // When the scan started (FILETIME ticks, UTC).
        ScreenAspect aspect; // World layout of the drive's map, kept across runs (plan.md 7.1).
        std::uint8_t scanner = 0; // services::ScannerKind that produced the files.
        std::uint64_t files = 0; // Files in the snapshot, for display.
        std::uint64_t extents = 0; // Extent records, for display.

        // Metadata compares as values (tests).
        friend bool operator==(const SnapshotMeta& a, const SnapshotMeta& b) {
            return a.volumeName == b.volumeName && a.volumeSerial == b.volumeSerial && a.scanTime == b.scanTime &&
                a.aspect.width == b.aspect.width && a.aspect.height == b.aspect.height && a.scanner == b.scanner &&
                a.files == b.files && a.extents == b.extents;
        }
    };

    // Where serialized bytes go.
    class ByteSink {
    public:
        virtual ~ByteSink() = default;

        // Appends bytes.
        virtual void write(std::span<const std::byte> bytes) = 0;
    };

    // Where serialized bytes come from.
    class ByteSource {
    public:
        virtual ~ByteSource() = default;

        // Fills `out` completely; throws SnapshotFormatError when the data ends first.
        virtual void read(std::span<std::byte> out) = 0;
    };

    // A sink into memory.
    class MemorySink : public ByteSink {
    public:
        // Appends to bytes.
        void write(std::span<const std::byte> data) override { bytes.insert(bytes.end(), data.begin(), data.end()); }

        std::vector<std::byte> bytes; // Everything written.
    };

    // A source over memory.
    class MemorySource : public ByteSource {
    public:
        // Reads from data, which must outlive the source.
        explicit MemorySource(std::span<const std::byte> data) : data_(data) {}

        // Copies the next bytes.
        void read(std::span<std::byte> out) override;

    private:
        std::span<const std::byte> data_; // What is read.
        std::size_t position_ = 0; // Next byte.
    };

    // Turns blocks into stored bytes and back; services plugs in a real compressor, tests the identity.
    class BlockCodec {
    public:
        virtual ~BlockCodec() = default;

        // Number written into each block so a reader knows how to decode it (0 is reserved for "stored as is").
        [[nodiscard]] virtual std::uint32_t id() const = 0;

        // Compressed bytes of a block.
        [[nodiscard]] virtual std::vector<std::byte> compress(std::span<const std::byte> raw) const = 0;

        // The raw block back; throws SnapshotFormatError when the bytes do not decode to rawSize bytes.
        [[nodiscard]] virtual std::vector<std::byte> decompress(std::span<const std::byte> stored,
                                                                std::size_t rawSize) const = 0;
    };

    // Settings of a snapshot file.
    struct SnapshotFileOptions {
        std::size_t blockBytes = 4U << 20U; // Raw bytes per block (each checksummed and compressed alone).
        std::size_t batchBlocks = 8; // Blocks compressed or decompressed in parallel at a time.
    };

    // Writes a snapshot file: header (magic, version, meta, payload size), then the snapshot in checksummed blocks,
    // each compressed by `codec` on the executor (or stored when compression does not help). The output is the same
    // for any executor.
    void writeSnapshotFile(const Snapshot& snapshot, const SnapshotMeta& meta, ByteSink& out, const BlockCodec& codec,
                           Executor& executor, const SnapshotFileOptions& options = {});

    // Reads only the header and meta of a snapshot file. Throws SnapshotFormatError for other files or versions.
    [[nodiscard]] SnapshotMeta readSnapshotMeta(ByteSource& in);

    // A loaded snapshot with its meta.
    struct LoadedSnapshot {
        SnapshotMeta meta; // As saved.
        Snapshot snapshot; // As saved, identical to the one written.
    };

    // Reads a whole snapshot file, checking every block's checksum and the snapshot's consistency (sizes, sorted and
    // disjoint extents inside the volume, valid file, name and parent indexes, sorted free runs). Throws
    // SnapshotFormatError for anything wrong; a file that loads is identical to what was written.
    [[nodiscard]] LoadedSnapshot readSnapshotFile(ByteSource& in, const BlockCodec& codec, Executor& executor,
                                                  const SnapshotFileOptions& options = {});

} // namespace icd::core
