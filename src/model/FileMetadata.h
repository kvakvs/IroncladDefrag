#pragma once

#include <filesystem>
#include <vector>

#include "Units.h"

namespace icd {
    class FileMetadata {
    public:
        enum class FileType { Unknown, Executable, Document, Media, System, Archive, Other };

        struct FragmentLocation {
            index64_t startCluster;
            count64_t clusterCount;
        };

    private:
        std::filesystem::path filePath;
        byte_count64_t fileSize;
        std::vector<FragmentLocation> fragments;
        std::filesystem::file_time_type lastAccessTime;
        std::filesystem::file_time_type creationTime;
        std::filesystem::file_time_type modificationTime;
        FileType fileType = FileType::Unknown;
        std::filesystem::path parentDirectory;
    };
} // namespace icd
