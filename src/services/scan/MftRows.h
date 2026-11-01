#pragma once

#include "core/ntfs/AttributeList.h"
#include "core/ntfs/MftRecord.h"
#include "core/snapshot/FileTable.h"
#include "core/snapshot/SnapshotBuilder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What MFT parser workers produce (plan.md 6.3, thread-local output): compact rows per chunk of records, written by one
// worker and read-only afterwards, so merging needs no locks. Win32-free, so the merge is tested on handcrafted
// records.
namespace icd::services::mft {

    // Facts a record row carries besides counts.
    enum RowFact : std::uint8_t {
        RowStandard = 1U << 0U, // Has $STANDARD_INFORMATION (times, DOS attributes).
        RowMainData = 1U << 1U, // Holds the unnamed $DATA's first segment or resident value: mainSize is valid.
        RowAttributeList = 1U << 2U, // Has an $ATTRIBUTE_LIST: other records hold some of its attributes.
    };

    // One in-use record, base or extension. Its names, segments and extents are contiguous ranges of the chunk's
    // tables.
    struct RecordRow {
        std::uint64_t reference = 0; // Record number and sequence.
        std::uint64_t baseReference = 0; // 0 for a base record, else the file it extends.
        core::FileTimes times; // From $STANDARD_INFORMATION.
        std::uint64_t mainSize = 0; // Logical size of the unnamed $DATA (with RowMainData).
        std::uint32_t dosAttributes = 0; // FILE_ATTRIBUTE_* bits from $STANDARD_INFORMATION.
        std::uint32_t firstName = 0; // Into ChunkRows::names.
        std::uint32_t firstSegment = 0; // Into ChunkRows::segments.
        std::uint32_t firstExtent = 0; // Into ChunkRows::extents.
        std::uint32_t extentCount = 0; // Extents of this record.
        std::uint16_t nameCount = 0; // $FILE_NAMEs in this record.
        std::uint16_t segmentCount = 0; // Segments of this record.
        std::uint16_t recordFlags = 0; // recordInUse, recordDirectory.
        std::uint8_t facts = 0; // RowFact bits.

        // MFT record number.
        [[nodiscard]] std::uint64_t record() const { return core::ntfs::recordOf(reference); }
    };

    // One $FILE_NAME.
    struct NameRow {
        std::uint64_t parentReference = 0; // Directory holding the name.
        std::uint32_t textOffset = 0; // UTF-8 bytes in ChunkRows::text.
        std::uint16_t textBytes = 0; // Length of the name.
        core::ntfs::NameSpace nameSpace = core::ntfs::NameSpace::Posix; // DOS aliases are not hard links.
    };

    // An attribute segment the merge needs: every non-resident one, named resident $DATA (streams) and resident
    // $ATTRIBUTE_LIST (its value).
    struct SegmentRow {
        core::ntfs::AttributeType type = core::ntfs::AttributeType::End; // Attribute type.
        std::uint32_t nameOffset = 0; // UTF-8 name in ChunkRows::text.
        std::uint32_t valueOffset = 0; // Resident attribute list value in ChunkRows::values.
        std::uint32_t valueBytes = 0; // Its length.
        std::uint64_t startVcn = 0; // First VCN this segment maps.
        std::uint64_t dataSize = 0; // Logical size (valid in the segment at VCN 0).
        std::uint32_t extentCount = 0; // Allocated runs of this segment among the record's extents.
        std::uint16_t nameBytes = 0; // Name length.
        std::uint16_t flags = 0; // Attribute header flags.
        std::uint16_t id = 0; // Attribute id within the record (attribute lists refer to it).
        bool nonResident = false; // Value in clusters.
    };

    // Rows of one chunk of records, in record order.
    struct ChunkRows {
        std::uint64_t firstRecord = 0; // Record number of the chunk's first record, for ordering chunks.
        std::vector<RecordRow> records; // In-use records.
        std::vector<NameRow> names; // Their names.
        std::vector<SegmentRow> segments; // Their segments.
        // Allocated runs (holes left out). `file` holds the record number and `stream` the segment index within the
        // record until the merge rewrites them to FileIndex and stream ordinal.
        std::vector<core::ExtentEntry> extents;
        std::string text; // Names, back to back.
        std::vector<std::byte> values; // Resident attribute list values.
        std::uint64_t badRecords = 0; // Damaged records.
        std::vector<std::uint64_t> badRecordNumbers; // The first few, for the log.

        // The text of a name or segment name.
        [[nodiscard]] std::string_view textAt(std::uint32_t offset, std::uint32_t bytes) const {
            return std::string_view(text).substr(offset, bytes);
        }
    };

    // Appends one parsed in-use record. Throws std::runtime_error for record numbers past 32 bits (they could not be
    // carried in an extent's file field).
    void appendRecord(const core::ntfs::ParsedRecord& record, ChunkRows& out);

    // Which records a buffer holds.
    struct RecordRange {
        std::uint64_t first = 0; // Number of the first record.
        std::uint32_t count = 0; // Records in the buffer.
        std::uint32_t bytesPerRecord = 1024; // Record size.
    };

    // Parses the whole records of one read into rows: records not in use, empty and (counted) damaged ones are
    // skipped. `records` holds the records of `range`; fixups are applied in place.
    void parseChunk(std::span<std::byte> records, RecordRange range, std::span<const std::byte> inUseBitmap,
                    ChunkRows& out);

} // namespace icd::services::mft
