#pragma once

#include "core/ntfs/NtfsFormat.h"
#include "core/ntfs/RunList.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace icd::core::ntfs {

    // Why a record could not be used; anything but Ok means the scanner skips the record.
    enum class RecordStatus : std::uint8_t {
        Ok, // Parsed; the result is complete for this record.
        Empty, // Never written (zero signature): a free slot.
        BadSignature, // Not "FILE"; "BAAD" when NTFS itself marked it damaged.
        BadFixup, // Update sequence mismatch: a torn write or damage.
        BadHeader, // Header sizes or offsets point outside the record, or it claims another record number.
        BadAttribute, // An attribute's lengths, offsets, name or run list are inconsistent.
    };

    // Timestamps and DOS attributes from $STANDARD_INFORMATION, which is what FileBasicInfo reports.
    struct StandardInformation {
        std::int64_t created = 0; // FILETIME ticks (100 ns since 1601, UTC).
        std::int64_t modified = 0; // Last data change.
        std::int64_t changed = 0; // Last change of the MFT record.
        std::int64_t accessed = 0; // Last access.
        std::uint32_t attributes = 0; // FILE_ATTRIBUTE_* bits; the directory bit is not stored here.
    };

    // $FILE_NAME namespaces: a long name may come with a separate 8.3 DOS alias, which is not a hard link.
    enum class NameSpace : std::uint8_t {
        Posix = 0, // Case-sensitive name, no DOS alias.
        Win32 = 1, // Long name that has a separate DOS alias.
        Dos = 2, // The 8.3 alias of a Win32 name.
        Win32AndDos = 3, // A name valid in both, no alias needed.
    };

    // One name of a file: hard links give a file several, each with its own parent directory.
    struct FileName {
        std::uint64_t parentReference = 0; // File reference of the directory holding the name.
        NameSpace nameSpace = NameSpace::Posix; // Which naming rules the name follows.
        std::string name; // UTF-8.

        // Whether this is only the 8.3 alias of another name of the same file.
        [[nodiscard]] bool dosAlias() const { return nameSpace == NameSpace::Dos; }
    };

    // One attribute segment the scanner needs: every non-resident one (it owns clusters), and the resident $DATA
    // (size, streams), $ATTRIBUTE_LIST and $BITMAP (their values). A non-resident attribute spread over several
    // records has one segment per record, each mapping its own VCN range.
    struct Attribute {
        AttributeType type = AttributeType::End; // Attribute type.
        std::uint16_t id = 0; // Instance id inside the record; attribute lists refer to it.
        std::uint16_t flags = 0; // attributeCompressionMask, attributeEncrypted, attributeSparse bits.
        std::string name; // UTF-8; empty for the unnamed $DATA stream, "$I30" for directory indexes.
        bool nonResident = false; // Whether the value lives in clusters instead of the record.
        std::uint8_t compressionUnit = 0; // log2 of clusters per compression unit; 0 when not compressed.
        std::uint64_t startVcn = 0; // First VCN this segment maps (non-resident).
        std::uint64_t lastVcn = 0; // Last VCN this segment maps; startVcn - 1 when it maps nothing.
        std::uint64_t allocatedSize = 0; // Bytes allocated; valid in the segment at VCN 0.
        std::uint64_t dataSize = 0; // Logical size (resident: value length); valid in the segment at VCN 0.
        std::uint64_t initializedSize = 0; // Bytes written so far; valid in the segment at VCN 0.
        std::vector<DataRun> runs; // Decoded run list of this segment (non-resident).
        std::vector<std::byte> value; // Resident value of $ATTRIBUTE_LIST and $BITMAP; empty otherwise.
    };

    // What one MFT record holds, ready for the scanner: header facts, timestamps, names and attribute segments.
    // Workers reuse one instance per record (clear() keeps capacity), so millions of records parse with few
    // allocations.
    struct ParsedRecord {
        std::uint64_t recordNumber = 0; // Position in $MFT.
        std::uint16_t sequence = 0; // Reuse count; with the record number it forms the file reference.
        std::uint16_t flags = 0; // recordInUse and recordDirectory bits.
        std::uint16_t linkCount = 0; // Directory entries naming the file, DOS aliases included.
        std::uint64_t baseReference = 0; // 0 for a base record, else the file this extension record extends.
        std::optional<StandardInformation> standard; // Timestamps and DOS attributes, when present.
        std::vector<FileName> names; // Every $FILE_NAME in this record.
        std::vector<Attribute> attributes; // Attribute segments in record order.

        // Whether the record describes a live file (or a live extension of one).
        [[nodiscard]] bool inUse() const { return (flags & recordInUse) != 0; }

        // Whether the file is a directory.
        [[nodiscard]] bool directory() const { return (flags & recordDirectory) != 0; }

        // Whether this is a file's base record rather than an extension record.
        [[nodiscard]] bool isBase() const { return baseReference == 0; }

        // The file reference that names this record now.
        [[nodiscard]] std::uint64_t reference() const { return makeReference(recordNumber, sequence); }

        // Empties the record for reuse.
        void clear();
    };

    // Applies the fixups to `raw` in place, then parses it. recordNumber is the record's position in $MFT; the
    // header's own copy of it must agree when present. Corrupt input never crashes or reads out of bounds: any
    // inconsistency gives a status other than Ok, and `out` must then not be used.
    [[nodiscard]] RecordStatus parseRecord(std::span<std::byte> raw, std::uint64_t recordNumber, ParsedRecord& out);

    // Text for a status, for logs and test messages.
    [[nodiscard]] const char* statusName(RecordStatus status);

} // namespace icd::core::ntfs
