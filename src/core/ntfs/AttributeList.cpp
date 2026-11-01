#include "core/ntfs/AttributeList.h"

#include "core/ntfs/NtfsText.h"

#include <optional>

namespace icd::core::ntfs {

    namespace {

        // Fixed part of an entry: type, length, name length and offset, start VCN, reference, id.
        constexpr std::size_t entryHeader = 26;

        // Whether an entry's length and name lie inside the list and inside the entry itself.
        bool entryValid(std::span<const std::byte> at, std::size_t length, std::size_t nameChars,
                        std::size_t nameOffset) {
            if (length < entryHeader || length % 8 != 0 || !fits(at, 0, length)) {
                return false;
            }
            return nameChars == 0 || (nameOffset >= entryHeader && nameOffset + 2 * nameChars <= length);
        }

        // The entry at offset and its length, or nullopt when it is malformed.
        std::optional<std::pair<AttributeListEntry, std::size_t>> readEntry(std::span<const std::byte> value,
                                                                            std::size_t offset) {
            if (!fits(value, offset, entryHeader)) {
                return std::nullopt;
            }
            const std::span<const std::byte> at = value.subspan(offset);
            const auto length = load<std::uint16_t>(at, 4);
            const auto nameChars = static_cast<std::size_t>(load<std::uint8_t>(at, 6));
            const auto nameOffset = static_cast<std::size_t>(load<std::uint8_t>(at, 7));
            if (!entryValid(at, length, nameChars, nameOffset)) {
                return std::nullopt;
            }
            AttributeListEntry entry;
            entry.type = static_cast<AttributeType>(load<std::uint32_t>(at, 0));
            entry.startVcn = load<std::uint64_t>(at, 8);
            entry.recordReference = load<std::uint64_t>(at, 16);
            entry.id = load<std::uint16_t>(at, 24);
            entry.name = utf16ToUtf8(at.subspan(nameOffset, 2 * nameChars));
            return std::pair(std::move(entry), std::size_t{length});
        }

    } // namespace

    bool parseAttributeList(std::span<const std::byte> value, std::vector<AttributeListEntry>& out) {
        const std::size_t original = out.size();
        std::size_t offset = 0;
        while (offset < value.size()) {
            auto entry = readEntry(value, offset);
            if (!entry) {
                out.resize(original);
                return false;
            }
            out.push_back(std::move(entry->first));
            offset += entry->second;
        }
        return true;
    }

} // namespace icd::core::ntfs
