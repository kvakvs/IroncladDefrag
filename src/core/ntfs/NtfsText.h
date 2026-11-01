#pragma once

#include <cstddef>
#include <span>
#include <string>

namespace icd::core::ntfs {

    // NTFS names (UTF-16LE code units, unvalidated) as UTF-8 for the name pool. Unpaired surrogates, which NTFS
    // allows, become U+FFFD exactly as WideCharToMultiByte does, so raw MFT names equal the names the API path
    // reports. A trailing odd byte is ignored.
    [[nodiscard]] std::string utf16ToUtf8(std::span<const std::byte> utf16le);

} // namespace icd::core::ntfs
