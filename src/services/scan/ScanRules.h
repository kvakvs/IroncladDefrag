#pragma once

#include <cstdint>
#include <string_view>

// Rules both scanners apply the same way, so the API and MFT paths flag every file identically (plan.md 10.2 special
// handling). Win32-free: attribute values are the FILE_ATTRIBUTE_* numbers.
namespace icd::services {

    // Name of the placeholder file that owns used clusters no readable file claims ($LogFile, $Bitmap, $Boot,
    // $BadClus or locked files on the API path); it is unmovable.
    inline constexpr const char* unattributedName = "$Unattributed";

    // MFT record of the root directory.
    inline constexpr std::uint64_t rootRecordNumber = 5;

    // NTFS system records 0-11 (12-15 are reserved and unused): they and everything below them are metadata.
    inline constexpr std::uint64_t systemRecordCount = 12;

    // Whether a root-level name is one of the files Windows keeps open and in place (pagefile, hiberfil, swapfile).
    [[nodiscard]] bool isPinnedRootName(std::string_view name);

    // File flags from FILE_ATTRIBUTE_* bits: directory, compressed, sparse, encrypted, reparse point.
    [[nodiscard]] std::uint16_t fileFlagsFromAttributes(std::uint32_t attributes);

    // Extent flags for the runs of a file with these file flags: directory index, compression unit, unmovable.
    [[nodiscard]] std::uint16_t extentFlagsFor(std::uint16_t fileFlags);

    // Whether a file without clusters is resident: its data lives in the MFT record (directories and sparse files
    // without clusters are not).
    [[nodiscard]] bool isResident(std::uint16_t fileFlags, bool hasClusters);

} // namespace icd::services
