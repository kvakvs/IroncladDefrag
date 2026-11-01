#include "services/scan/ScanRules.h"

#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/FileTable.h"
#include "services/scan/ScanStats.h"

#include <algorithm>
#include <array>
#include <utility>

namespace icd::services {

    namespace {

        // Root-level files Windows keeps open and in place.
        constexpr std::array<std::string_view, 3> pinnedNames{"pagefile.sys", "hiberfil.sys", "swapfile.sys"};

        // FILE_ATTRIBUTE_* bits that map one to one onto file flags.
        constexpr std::array<std::pair<std::uint32_t, std::uint16_t>, 5> attributeFlags{{
            {0x0010, core::FileDirectory}, // FILE_ATTRIBUTE_DIRECTORY
            {0x0800, core::FileCompressed}, // FILE_ATTRIBUTE_COMPRESSED
            {0x0200, core::FileSparse}, // FILE_ATTRIBUTE_SPARSE_FILE
            {0x4000, core::FileEncrypted}, // FILE_ATTRIBUTE_ENCRYPTED
            {0x0400, core::FileReparsePoint}, // FILE_ATTRIBUTE_REPARSE_POINT
        }};

    } // namespace

    const char* scannerName(ScannerKind kind) { return kind == ScannerKind::Mft ? "mft" : "api"; }

    bool isPinnedRootName(std::string_view name) {
        return std::ranges::any_of(pinnedNames, [&](std::string_view pinned) { return pinned == name; });
    }

    std::uint16_t fileFlagsFromAttributes(std::uint32_t attributes) {
        std::uint16_t flags = 0;
        for (const auto& [attribute, flag] : attributeFlags) {
            flags |= (attributes & attribute) != 0 ? flag : 0;
        }
        return flags;
    }

    std::uint16_t extentFlagsFor(std::uint16_t fileFlags) {
        std::uint16_t flags = 0;
        flags |= (fileFlags & core::FileDirectory) != 0 ? core::ExtentDirectoryIndex : 0;
        flags |= (fileFlags & core::FileCompressed) != 0 ? core::ExtentCompressionUnit : 0;
        flags |= (fileFlags & core::FileUnmovable) != 0 ? core::ExtentUnmovable : 0;
        return flags;
    }

    bool isResident(std::uint16_t fileFlags, bool hasClusters) {
        return (fileFlags & (core::FileDirectory | core::FileSparse)) == 0 && !hasClusters;
    }

} // namespace icd::services
