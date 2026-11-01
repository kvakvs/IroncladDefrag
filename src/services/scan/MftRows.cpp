#include "services/scan/MftRows.h"

#include "core/ntfs/MftLayout.h"

#include <limits>
#include <stdexcept>

namespace icd::services::mft {

    namespace {

        using core::ntfs::Attribute;
        using core::ntfs::AttributeType;

        // Damaged record numbers kept per chunk for the log.
        constexpr std::size_t badNumbersKept = 16;

        // Appends text and returns its offset.
        std::uint32_t appendText(ChunkRows& out, std::string_view text) {
            const auto offset = static_cast<std::uint32_t>(out.text.size());
            out.text.append(text);
            return offset;
        }

        // Whether a segment row is needed: clusters, a named stream or an attribute list.
        bool keepSegment(const Attribute& a) {
            return a.nonResident || (a.type == AttributeType::Data && !a.name.empty()) ||
                a.type == AttributeType::AttributeList;
        }

        // The unnamed $DATA's size is known from its resident value or its first segment.
        bool givesMainSize(const Attribute& a) {
            return a.type == AttributeType::Data && a.name.empty() && (!a.nonResident || a.startVcn == 0);
        }

        // Adds a segment row and its allocated runs as extents.
        void appendSegment(const Attribute& a, std::uint64_t record, std::uint16_t index, ChunkRows& out) {
            SegmentRow segment;
            segment.type = a.type;
            segment.nameOffset = appendText(out, a.name);
            segment.nameBytes = static_cast<std::uint16_t>(a.name.size());
            segment.flags = a.flags;
            segment.id = a.id;
            segment.nonResident = a.nonResident;
            segment.startVcn = a.startVcn;
            segment.dataSize = a.dataSize;
            if (!a.nonResident && a.type == AttributeType::AttributeList) {
                segment.valueOffset = static_cast<std::uint32_t>(out.values.size());
                segment.valueBytes = static_cast<std::uint32_t>(a.value.size());
                out.values.insert(out.values.end(), a.value.begin(), a.value.end());
            }
            const std::size_t before = out.extents.size();
            for (const core::ntfs::DataRun& run : a.runs) {
                if (!run.hole()) {
                    out.extents.push_back({.run = {static_cast<core::Lcn>(run.lcn), run.length},
                                           .vcn = run.vcn,
                                           .file = static_cast<core::FileIndex>(record),
                                           .stream = index,
                                           .flags = 0});
                }
            }
            segment.extentCount = static_cast<std::uint32_t>(out.extents.size() - before);
            out.segments.push_back(segment);
        }

        // Adds the names of a record.
        void appendNames(const core::ntfs::ParsedRecord& r, RecordRow& row, ChunkRows& out) {
            row.firstName = static_cast<std::uint32_t>(out.names.size());
            for (const core::ntfs::FileName& name : r.names) {
                out.names.push_back({.parentReference = name.parentReference,
                                     .textOffset = appendText(out, name.name),
                                     .textBytes = static_cast<std::uint16_t>(name.name.size()),
                                     .nameSpace = name.nameSpace});
            }
            row.nameCount = static_cast<std::uint16_t>(r.names.size());
        }

        // Adds the segments of a record and notes the main stream's size.
        void appendSegments(const core::ntfs::ParsedRecord& r, RecordRow& row, ChunkRows& out) {
            row.firstSegment = static_cast<std::uint32_t>(out.segments.size());
            row.firstExtent = static_cast<std::uint32_t>(out.extents.size());
            for (const Attribute& a : r.attributes) {
                if (givesMainSize(a)) {
                    row.mainSize = a.dataSize;
                    row.facts |= RowMainData;
                }
                row.facts |= a.type == AttributeType::AttributeList ? RowAttributeList : 0;
                if (keepSegment(a)) {
                    const std::size_t index = out.segments.size() - row.firstSegment;
                    appendSegment(a, r.recordNumber, static_cast<std::uint16_t>(index), out);
                }
            }
            row.segmentCount = static_cast<std::uint16_t>(out.segments.size() - row.firstSegment);
            row.extentCount = static_cast<std::uint32_t>(out.extents.size() - row.firstExtent);
        }

        // Parses one record into rows, or counts it as damaged.
        void parseOne(std::span<std::byte> raw, std::uint64_t number, ChunkRows& out) {
            thread_local core::ntfs::ParsedRecord parsed; // Reused, keeping its capacity across chunks.
            const auto status = core::ntfs::parseRecord(raw, number, parsed);
            if (status == core::ntfs::RecordStatus::Ok) {
                if (parsed.inUse()) {
                    appendRecord(parsed, out);
                }
                return;
            }
            if (status != core::ntfs::RecordStatus::Empty) {
                ++out.badRecords;
                if (out.badRecordNumbers.size() < badNumbersKept) {
                    out.badRecordNumbers.push_back(number);
                }
            }
        }

    } // namespace

    void appendRecord(const core::ntfs::ParsedRecord& record, ChunkRows& out) {
        if (record.recordNumber > std::numeric_limits<core::FileIndex>::max() - 1) {
            throw std::runtime_error("MFT record numbers past 32 bits are not supported");
        }
        RecordRow row;
        row.reference = record.reference();
        row.baseReference = record.baseReference;
        row.recordFlags = record.flags;
        if (record.standard) {
            row.times = {record.standard->created, record.standard->modified, record.standard->accessed};
            row.dosAttributes = record.standard->attributes;
            row.facts |= RowStandard;
        }
        appendNames(record, row, out);
        appendSegments(record, row, out);
        out.records.push_back(row);
    }

    void parseChunk(std::span<std::byte> records, RecordRange range, std::span<const std::byte> inUseBitmap,
                    ChunkRows& out) {
        out.firstRecord = range.first;
        for (std::uint32_t i = 0; i < range.count; ++i) {
            const std::uint64_t number = range.first + i;
            if (core::ntfs::isRecordUsed(inUseBitmap, number)) {
                parseOne(records.subspan(std::size_t{i} * range.bytesPerRecord, range.bytesPerRecord), number, out);
            }
        }
    }

} // namespace icd::services::mft
