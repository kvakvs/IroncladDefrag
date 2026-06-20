#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "DiskGeometry.h"
#include "FileMetadata.h"
#include "FreeSpaceMap.h"
#include "Units.h"

namespace icd {

enum class DriveKind { Unknown, Mechanical, SolidState, Removable, Network };

struct DriveInfo {
    std::wstring rootPath;
    std::wstring displayName;
    DriveKind kind = DriveKind::Unknown;
    bool isFixed = false;
    bool supportsTrim = false;
    bool supportsFileMove = false;
};

struct VolumeInfo {
    std::wstring label;
    std::wstring fileSystem;
    byte_count64_t totalBytes;
    byte_count64_t freeBytes;
    byte_count64_t bytesPerCluster;
};

enum class DiskZoneRole { Fast, Balanced, Slow, LargeFile, FreeSpaceReserve };

struct DiskZone {
    DiskZoneRole role = DiskZoneRole::Balanced;
    index64_t startSector;
    count64_t sectorCount;
    bool enabled = true;
};

enum class FileSizeClass { Unknown, Tiny, Small, Medium, Large, Huge };
enum class FileTemperature { Unknown, Hot, Warm, Cool, Cold, Stale };

struct FileClass {
    FileSizeClass sizeClass = FileSizeClass::Unknown;
    FileTemperature temperature = FileTemperature::Unknown;
    FileMetadata::FileType type = FileMetadata::FileType::Unknown;
    bool excluded = false;
    bool moveOnlyWhenExplicit = false;
};

enum class OptimizationMode {
    BalancedDataDrive,
    LargeFileContiguous,
    SmallFileFastZone,
    ColdArchive,
    DirectoryClustering,
    FileTypeSegregation,
    SizeBasedPlacement,
    FreeSpaceOptimization,
    SingleFileDefragmentation
};

struct OptimizationSettings {
    bool enableFastZone = true;
    bool enableBalancedZone = true;
    bool enableSlowZone = true;
    bool enableLargeFileZone = true;
    bool enableFreeSpaceReserve = true;
    byte_count64_t smallFileThreshold = byte_count64_t(1ull * 1024ull * 1024ull);
    byte_count64_t largeFileThreshold = byte_count64_t(128ull * 1024ull * 1024ull);
    byte_count64_t hugeFileThreshold = byte_count64_t(4ull * 1024ull * 1024ull * 1024ull);
    byte_count64_t maximumBytesToMove;
    double minimumBenefitScore = 0.0;
    bool preserveDirectoryLocality = true;
    bool allowLargeFileMoves = true;
    bool prioritizeFreeSpaceConsolidation = false;
    bool dryRunOnly = true;
    std::chrono::hours hotRecency = std::chrono::hours(24 * 14);
    std::chrono::hours coldRecency = std::chrono::hours(24 * 180);
};

struct OptimizationProfile {
    OptimizationMode mode = OptimizationMode::BalancedDataDrive;
    std::wstring name = L"Balanced data-drive optimization";
    OptimizationSettings settings;
};

struct AnalysisResult {
    DriveInfo drive;
    VolumeInfo volume;
    DiskGeometry geometry;
    std::vector<FileMetadata> files;
    FreeSpaceMap freeSpace;
    bool synthetic = false;
    std::wstring summary;
};

struct PlacementPlan {
    OptimizationProfile profile;
    std::vector<DiskZone> zones;
    std::wstring summary;
};

struct MoveOperation {
    std::filesystem::path filePath;
    index64_t sourceStartCluster;
    index64_t targetStartCluster;
    count64_t clusterCount;
    byte_count64_t estimatedBytes;
    std::wstring reason;
};

struct MovePlan {
    OptimizationProfile profile;
    std::vector<MoveOperation> operations;
    byte_count64_t estimatedBytesToMove;
    bool dryRun = true;
    std::wstring summary;
};

enum class JobState { Idle, Running, Cancelling, Cancelled, Completed, Failed };

struct JobProgress {
    JobState state = JobState::Idle;
    double percentComplete = 0.0;
    std::wstring statusMessage;
    std::wstring currentItem;
    count64_t itemsProcessed;
    count64_t totalItems;
    bool cancellationRequested = false;
};

} // namespace icd
