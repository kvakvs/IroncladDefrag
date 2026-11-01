#pragma once

#include <cstddef>
#include <span>

namespace icd::core::ntfs {

    // Undoes NTFS multi-sector protection in place: the last two bytes of every 512-byte block of a record were
    // replaced by the update sequence number when it was written, and their real values sit in the update sequence
    // array. A block whose end does not carry the number was torn by an interrupted write or damaged. Returns false
    // (record unchanged) when the array is malformed or a block end does not match.
    [[nodiscard]] bool applyFixups(std::span<std::byte> record);

} // namespace icd::core::ntfs
