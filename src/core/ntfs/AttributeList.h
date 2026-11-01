#pragma once

#include "core/ntfs/NtfsFormat.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace icd::core::ntfs {

    // One entry of an $ATTRIBUTE_LIST: which record holds one segment of one attribute. Files whose attributes do
    // not fit one record (many fragments, many hard links) list every segment here, and the scanner joins the
    // segments it found in extension records to their base file only when the list names them.
    struct AttributeListEntry {
        AttributeType type = AttributeType::End; // Attribute type.
        std::uint64_t startVcn = 0; // First VCN the segment maps (0 for resident attributes).
        std::uint64_t recordReference = 0; // File reference of the record holding the segment.
        std::uint16_t id = 0; // Attribute id inside that record.
        std::string name; // Attribute name (UTF-8), empty for the unnamed stream.

        // Entries compare as values (tests).
        friend bool operator==(const AttributeListEntry&, const AttributeListEntry&) = default;
    };

    // Parses the value of an $ATTRIBUTE_LIST (resident value, or the bytes read from its clusters up to its data
    // size) and appends the entries to `out`. Returns false and leaves `out` as it was when an entry is malformed.
    [[nodiscard]] bool parseAttributeList(std::span<const std::byte> value, std::vector<AttributeListEntry>& out);

} // namespace icd::core::ntfs
