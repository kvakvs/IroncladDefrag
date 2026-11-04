#pragma once

#include "core/job/CancelToken.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace icd::platform {

    // "No parent": the entry sits directly in the walked folder.
    inline constexpr std::size_t walkRoot = std::numeric_limits<std::size_t>::max();

    // One file or folder found by walkFolder().
    struct FolderEntry {
        std::string name; // Own name, UTF-8.
        std::size_t parent = walkRoot; // Index of its folder in the walk's entries, or walkRoot.
        std::uint64_t sizeBytes = 0; // Logical size (0 for folders).
        std::int64_t modified = 0; // FILETIME of the last write.
        bool directory = false; // A folder.
        // A junction, symbolic link or other reparse point; folders of this kind are not entered.
        bool reparsePoint = false;
    };

    // Everything below a folder, in walk order (a folder before its contents).
    struct FolderWalk {
        std::vector<FolderEntry> entries; // Files and folders.
        std::size_t unreadable = 0; // Folders that could not be listed (access denied and the like); skipped.
    };

    // Lists a folder tree with the ordinary directory API (no volume handle, no administrator rights), for classifying
    // a folder without analysing its volume. Never follows junctions or symbolic links, so a tree is walked once.
    // Reads only. Throws std::runtime_error when the folder itself cannot be listed, OperationCancelled on cancel.
    [[nodiscard]] FolderWalk walkFolder(const std::wstring& folder, const core::CancelToken& token = {});

} // namespace icd::platform
