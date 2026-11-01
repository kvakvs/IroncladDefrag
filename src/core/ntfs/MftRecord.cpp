#include "core/ntfs/MftRecord.h"

#include "core/ntfs/Fixups.h"
#include "core/ntfs/NtfsText.h"

#include <algorithm>

namespace icd::core::ntfs {

    namespace {

        // Header of a record up to and including the NTFS 3.1 record number field.
        constexpr std::size_t headerWithNumber = 0x30;

        // Smallest record header (NTFS 3.0, without the record number).
        constexpr std::size_t headerMinimum = 0x2A;

        // Common attribute header plus the resident or non-resident part.
        constexpr std::size_t residentHeader = 24;
        constexpr std::size_t nonResidentHeader = 64;

        // Fixed parts of the attribute values the parser reads.
        constexpr std::size_t standardMinimum = 36; // Up to and including the DOS attributes.
        constexpr std::size_t fileNameHeader = 66; // Up to the name itself.

        // Where the attributes are and how much of the record is used.
        struct Layout {
            std::size_t firstAttribute = 0; // Offset of the first attribute.
            std::size_t bytesInUse = 0; // End of the used part.
        };

        // Checks the header and fills the record's header fields.
        RecordStatus readHeader(std::span<const std::byte> raw, std::uint64_t recordNumber, ParsedRecord& out,
                                Layout& layout) {
            layout.firstAttribute = load<std::uint16_t>(raw, 0x14);
            layout.bytesInUse = load<std::uint32_t>(raw, 0x18);
            if (layout.bytesInUse > raw.size() || layout.firstAttribute < headerMinimum ||
                layout.firstAttribute % 8 != 0 || layout.firstAttribute >= layout.bytesInUse) {
                return RecordStatus::BadHeader;
            }
            const bool numbered = layout.firstAttribute >= headerWithNumber;
            if (numbered && load<std::uint32_t>(raw, 0x2C) != static_cast<std::uint32_t>(recordNumber)) {
                return RecordStatus::BadHeader;
            }
            out.recordNumber = recordNumber;
            out.sequence = load<std::uint16_t>(raw, 0x10);
            out.linkCount = load<std::uint16_t>(raw, 0x12);
            out.flags = load<std::uint16_t>(raw, 0x16);
            out.baseReference = load<std::uint64_t>(raw, 0x20);
            return RecordStatus::Ok;
        }

        // The attribute's name, or nullopt when it lies outside the attribute.
        std::optional<std::string> attributeName(std::span<const std::byte> attr) {
            const auto chars = static_cast<std::size_t>(load<std::uint8_t>(attr, 9));
            const auto offset = static_cast<std::size_t>(load<std::uint16_t>(attr, 10));
            if (chars == 0) {
                return std::string();
            }
            if (offset < 16 || !fits(attr, offset, 2 * chars)) {
                return std::nullopt;
            }
            return utf16ToUtf8(attr.subspan(offset, 2 * chars));
        }

        // The value of a resident attribute, or nullopt when it lies outside the attribute.
        std::optional<std::span<const std::byte>> residentValue(std::span<const std::byte> attr) {
            if (attr.size() < residentHeader) {
                return std::nullopt;
            }
            const std::size_t length = load<std::uint32_t>(attr, 16);
            const std::size_t offset = load<std::uint16_t>(attr, 20);
            if (offset < residentHeader || !fits(attr, offset, length)) {
                return std::nullopt;
            }
            return attr.subspan(offset, length);
        }

        // $STANDARD_INFORMATION; false when it is too short.
        bool readStandard(std::span<const std::byte> value, ParsedRecord& out) {
            if (value.size() < standardMinimum) {
                return false;
            }
            if (!out.standard) { // Only the first one counts.
                out.standard = StandardInformation{.created = load<std::int64_t>(value, 0),
                                                   .modified = load<std::int64_t>(value, 8),
                                                   .changed = load<std::int64_t>(value, 16),
                                                   .accessed = load<std::int64_t>(value, 24),
                                                   .attributes = load<std::uint32_t>(value, 32)};
            }
            return true;
        }

        // $FILE_NAME; false when the name runs past the value.
        bool readFileName(std::span<const std::byte> value, ParsedRecord& out) {
            if (value.size() < fileNameHeader) {
                return false;
            }
            const auto chars = static_cast<std::size_t>(load<std::uint8_t>(value, 64));
            if (!fits(value, fileNameHeader, 2 * chars)) {
                return false;
            }
            out.names.push_back({.parentReference = load<std::uint64_t>(value, 0),
                                 .nameSpace = static_cast<NameSpace>(load<std::uint8_t>(value, 65) & 3U),
                                 .name = utf16ToUtf8(value.subspan(fileNameHeader, 2 * chars))});
            return true;
        }

        // Sizes and run list of a non-resident attribute; false when they are inconsistent.
        bool readNonResident(std::span<const std::byte> attr, Attribute& a) {
            if (attr.size() < nonResidentHeader) {
                return false;
            }
            a.startVcn = load<std::uint64_t>(attr, 16);
            a.lastVcn = load<std::uint64_t>(attr, 24);
            const std::size_t runsOffset = load<std::uint16_t>(attr, 32);
            a.compressionUnit = load<std::uint8_t>(attr, 34);
            a.allocatedSize = load<std::uint64_t>(attr, 40);
            a.dataSize = load<std::uint64_t>(attr, 48);
            a.initializedSize = load<std::uint64_t>(attr, 56);
            if (runsOffset < nonResidentHeader || runsOffset >= attr.size()) {
                return false;
            }
            return decodeRunList(attr.subspan(runsOffset), a.startVcn, a.lastVcn, a.runs);
        }

        // Whether a resident attribute of this type is kept (others own no clusters and say nothing needed).
        bool keepResident(AttributeType type) {
            return type == AttributeType::Data || type == AttributeType::AttributeList || type == AttributeType::Bitmap;
        }

        // A resident $DATA, $ATTRIBUTE_LIST or $BITMAP as a segment; false when its value is out of bounds.
        bool readResident(std::span<const std::byte> attr, Attribute& a) {
            const auto value = residentValue(attr);
            if (!value) {
                return false;
            }
            a.dataSize = a.allocatedSize = a.initializedSize = value->size();
            if (a.type != AttributeType::Data) {
                a.value.assign(value->begin(), value->end());
            }
            return true;
        }

        // Adds one attribute segment (non-resident, or a kept resident type); false when it is malformed.
        bool readSegment(std::span<const std::byte> attr, AttributeType type, bool nonResident, ParsedRecord& out) {
            if (!nonResident && !keepResident(type)) {
                return true;
            }
            auto name = attributeName(attr);
            if (!name) {
                return false;
            }
            Attribute& a = out.attributes.emplace_back();
            a.type = type;
            a.nonResident = nonResident;
            a.flags = load<std::uint16_t>(attr, 12);
            a.id = load<std::uint16_t>(attr, 14);
            a.name = std::move(*name);
            return nonResident ? readNonResident(attr, a) : readResident(attr, a);
        }

        // One attribute; false when it is malformed.
        bool readAttribute(std::span<const std::byte> attr, ParsedRecord& out) {
            const auto type = static_cast<AttributeType>(load<std::uint32_t>(attr, 0));
            const bool nonResident = load<std::uint8_t>(attr, 8) != 0;
            const bool named = type == AttributeType::StandardInformation || type == AttributeType::FileName;
            if (!named) {
                return readSegment(attr, type, nonResident, out);
            }
            const auto value = nonResident ? std::nullopt : residentValue(attr); // Both are always resident.
            if (!value) {
                return false;
            }
            return type == AttributeType::FileName ? readFileName(*value, out) : readStandard(*value, out);
        }

        // Walks the attributes up to the end marker.
        RecordStatus readAttributes(std::span<const std::byte> raw, const Layout& layout, ParsedRecord& out) {
            const std::span<const std::byte> used = raw.first(layout.bytesInUse);
            std::size_t offset = layout.firstAttribute;
            while (fits(used, offset, 4) && load<std::uint32_t>(used, offset) != std::uint32_t(AttributeType::End)) {
                if (!fits(used, offset, residentHeader)) {
                    return RecordStatus::BadAttribute;
                }
                const std::size_t length = load<std::uint32_t>(used, offset + 4);
                if (length < residentHeader || length % 8 != 0 || !fits(used, offset, length) ||
                    !readAttribute(used.subspan(offset, length), out)) {
                    return RecordStatus::BadAttribute;
                }
                offset += length;
            }
            return fits(used, offset, 4) ? RecordStatus::Ok : RecordStatus::BadAttribute; // No end marker.
        }

        // The status from the signature alone, before fixups.
        RecordStatus signatureStatus(std::span<const std::byte> raw) {
            const auto signature = load<std::uint32_t>(raw, 0);
            if (signature == fileSignature) {
                return RecordStatus::Ok;
            }
            return signature == 0 ? RecordStatus::Empty : RecordStatus::BadSignature;
        }

    } // namespace

    void ParsedRecord::clear() {
        recordNumber = 0;
        sequence = flags = linkCount = 0;
        baseReference = 0;
        standard.reset();
        names.clear();
        attributes.clear();
    }

    RecordStatus parseRecord(std::span<std::byte> raw, std::uint64_t recordNumber, ParsedRecord& out) {
        out.clear();
        if (raw.size() < headerWithNumber + 8) {
            return RecordStatus::BadHeader;
        }
        if (const RecordStatus status = signatureStatus(raw); status != RecordStatus::Ok) {
            return status;
        }
        if (!applyFixups(raw)) {
            return RecordStatus::BadFixup;
        }
        Layout layout;
        if (const RecordStatus status = readHeader(raw, recordNumber, out, layout); status != RecordStatus::Ok) {
            return status;
        }
        return readAttributes(raw, layout, out);
    }

    const char* statusName(RecordStatus status) {
        switch (status) {
        case RecordStatus::Ok:
            return "ok";
        case RecordStatus::Empty:
            return "empty";
        case RecordStatus::BadSignature:
            return "bad signature";
        case RecordStatus::BadFixup:
            return "bad fixup";
        case RecordStatus::BadHeader:
            return "bad header";
        case RecordStatus::BadAttribute:
            return "bad attribute";
        }
        return "unknown";
    }

} // namespace icd::core::ntfs
