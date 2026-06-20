#pragma once

#include "../../model/FileMetadata.h"
#include "../../model/FreeSpaceMap.h"

#include <filesystem>

namespace icd::win {

// Carries the retrieval-pointer result for one file without exposing Win32 buffers.
struct FileExtentsResult {
    bool available = false;
    std::vector<FileMetadata::FragmentLocation> fragments;
};

// Carries the volume bitmap result without exposing FSCTL buffer details.
struct FreeSpaceQueryResult {
    bool available = false;
    FreeSpaceMap freeSpace;
    count64_t largestFreeBlockSectors;
};

FileExtentsResult QueryFileExtents(const std::filesystem::path& path);
FreeSpaceQueryResult QueryVolumeFreeSpace(const std::wstring& rootPath);

} // namespace icd::win
