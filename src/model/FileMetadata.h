#pragma once

#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

#include "Units.h"

namespace icd {
// Stores read-only file metadata and extent information collected during drive analysis.
class FileMetadata {
public:
    enum class FileType { Unknown, Executable, Document, Media, System, Archive, Other };

    // Describes one physical cluster run reported for a file.
    struct FragmentLocation {
        index64_t startCluster;
        count64_t clusterCount;
    };

    // Captures Windows file attributes that influence analysis and future move safety.
    struct AttributeFlags {
        bool hidden = false;
        bool system = false;
        bool reparsePoint = false;
        bool sparse = false;
        bool compressed = false;
        bool encrypted = false;
        bool riskyOrUnmovable = false;
        bool extentsAvailable = false;
    };

    FileMetadata() = default;
    FileMetadata(std::filesystem::path path,
                 byte_count64_t size,
                 std::vector<FragmentLocation> fragmentLocations,
                 std::filesystem::file_time_type lastAccess,
                 std::filesystem::file_time_type created,
                 std::filesystem::file_time_type modified,
                 FileType type = FileType::Unknown,
                 std::uint32_t attributes = 0,
                 AttributeFlags flags = {})
        : filePath(std::move(path)),
          fileSize(size),
          fragments(std::move(fragmentLocations)),
          lastAccessTime(lastAccess),
          creationTime(created),
          modificationTime(modified),
          fileType(type),
          parentDirectory(filePath.parent_path()),
          windowsAttributes(attributes),
          attributeFlags(flags)
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
    std::uint32_t GetWindowsAttributes() const { return windowsAttributes; }
    const AttributeFlags& GetAttributeFlags() const { return attributeFlags; }
    bool IsFragmented() const { return fragments.size() > 1; }
    bool HasExtents() const { return attributeFlags.extentsAvailable; }
    bool IsRiskyOrUnmovable() const { return attributeFlags.riskyOrUnmovable; }

private:
    std::filesystem::path filePath;
    byte_count64_t fileSize;
    std::vector<FragmentLocation> fragments;
    std::filesystem::file_time_type lastAccessTime{};
    std::filesystem::file_time_type creationTime{};
    std::filesystem::file_time_type modificationTime{};
    FileType fileType = FileType::Unknown;
    std::filesystem::path parentDirectory;
    std::uint32_t windowsAttributes = 0;
    AttributeFlags attributeFlags;
};
} // namespace icd
