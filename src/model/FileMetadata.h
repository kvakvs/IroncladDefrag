#pragma once

#include <filesystem>
#include <utility>
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

    FileMetadata() = default;
    FileMetadata(std::filesystem::path path,
                 byte_count64_t size,
                 std::vector<FragmentLocation> fragmentLocations,
                 std::filesystem::file_time_type lastAccess,
                 std::filesystem::file_time_type created,
                 std::filesystem::file_time_type modified,
                 FileType type = FileType::Unknown)
        : filePath(std::move(path)),
          fileSize(size),
          fragments(std::move(fragmentLocations)),
          lastAccessTime(lastAccess),
          creationTime(created),
          modificationTime(modified),
          fileType(type),
          parentDirectory(filePath.parent_path())
    {
    }

    const std::filesystem::path& GetPath() const { return filePath; }
    byte_count64_t GetSize() const { return fileSize; }
    const std::vector<FragmentLocation>& GetFragments() const { return fragments; }
    std::filesystem::file_time_type GetLastAccessTime() const { return lastAccessTime; }
    std::filesystem::file_time_type GetCreationTime() const { return creationTime; }
    std::filesystem::file_time_type GetModificationTime() const { return modificationTime; }
    FileType GetFileType() const { return fileType; }
    const std::filesystem::path& GetParentDirectory() const { return parentDirectory; }
    bool IsFragmented() const { return fragments.size() > 1; }

private:
    std::filesystem::path filePath;
    byte_count64_t fileSize;
    std::vector<FragmentLocation> fragments;
    std::filesystem::file_time_type lastAccessTime{};
    std::filesystem::file_time_type creationTime{};
    std::filesystem::file_time_type modificationTime{};
    FileType fileType = FileType::Unknown;
    std::filesystem::path parentDirectory;
};
} // namespace icd
