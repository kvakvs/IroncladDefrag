#include "core/ntfs/Fixups.h"

#include "core/ntfs/NtfsFormat.h"

#include <cstdint>
#include <cstring>

namespace icd::core::ntfs {

    namespace {

        // Whether the update sequence array described by the header fits the record and covers each block.
        bool arrayValid(std::span<const std::byte> record, std::size_t offset, std::size_t count) {
            const std::size_t blocks = record.size() / fixupStride;
            // The array lives in the first block, before that block's protected end.
            return count == blocks + 1 && offset % 2 == 0 && offset >= 8 && offset + 2 * count <= fixupStride - 2;
        }

        // Whether every block ends with the update sequence number.
        bool blocksMatch(std::span<const std::byte> record, std::uint16_t usn) {
            for (std::size_t end = fixupStride - 2; end < record.size(); end += fixupStride) {
                if (load<std::uint16_t>(record, end) != usn) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    bool applyFixups(std::span<std::byte> record) {
        if (record.size() < fixupStride || record.size() % fixupStride != 0) {
            return false;
        }
        const auto offset = load<std::uint16_t>(record, 4);
        const auto count = load<std::uint16_t>(record, 6);
        if (!arrayValid(record, offset, count)) {
            return false;
        }
        if (!blocksMatch(record, load<std::uint16_t>(record, offset))) {
            return false;
        }
        std::size_t saved = offset + 2;
        for (std::size_t end = fixupStride - 2; end < record.size(); end += fixupStride, saved += 2) {
            std::memcpy(record.data() + end, record.data() + saved, 2);
        }
        return true;
    }

} // namespace icd::core::ntfs
