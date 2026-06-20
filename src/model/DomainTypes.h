#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "DiskGeometry.h"
#include "FileMetadata.h"
#include "FreeSpaceMap.h"
#include "Units.h"

namespace icd {

enum class DriveKind { Unknown, Mechanical, SolidState, Removable, Network };

// Describes user-visible volume facts collected during read-only drive discovery.
struct VolumeInfo {
    std::wstring label;
    std::wstring fileSystem;
    byte_count64_t totalBytes = byte_count64_t();
    byte_count64_t freeBytes = byte_count64_t();
    byte_count64_t bytesPerCluster = byte_count64_t();
};

// Describes which read-only drive-analysis operations are available for a drive.
struct DriveCapabilityStatus {
    bool mediaKnown = false;
    bool canOpenVolumeMetadata = false;
    bool canOpenVolume = false;
    bool canQueryBitmap = false;
    bool canQueryExtents = false;
    bool canAnalyze = false;
    std::wstring statusReason;
    std::wstring disabledReason;
};

// Describes a discovered drive and the analysis capabilities attached to it.
struct DriveInfo {
    std::wstring rootPath;
    std::wstring displayName;
    DriveKind kind = DriveKind::Unknown;
    bool isFixed = false;
    bool supportsTrim = false;
    bool supportsFileMove = false;
    VolumeInfo volume;
    byte_count64_t bytesPerSector = byte_count64_t();
    count64_t sectorsPerCluster = count64_t();
    DriveCapabilityStatus capabilities;
};

enum class DiskZoneRole { Fast, Balanced, Slow, LargeFile, FreeSpaceReserve };

// Describes a logical placement zone used by future optimization strategies.
struct DiskZone {
    DiskZoneRole role = DiskZoneRole::Balanced;
    index64_t startSector = index64_t();
    count64_t sectorCount = count64_t();
    bool enabled = true;
};

enum class FileSizeClass { Unknown, Tiny, Small, Medium, Large, Huge };
enum class FileTemperature { Unknown, Hot, Warm, Cool, Cold, Stale };
enum class BroadFileType { Unknown, Media, Archive, Document, Executable, SourceProject, Backup, VirtualDisk, Other };
enum class ExpectedPlacementZone { None, Fast, Balanced, Slow, LargeFile };
enum class FragmentationBenefit { Unknown, None, Low, Medium, High };

// Describes a file's coarse optimization-relevant classification.
struct FileClass {
    FileSizeClass sizeClass = FileSizeClass::Unknown;
    FileTemperature temperature = FileTemperature::Unknown;
    FileMetadata::FileType type = FileMetadata::FileType::Unknown;
    BroadFileType broadType = BroadFileType::Unknown;
    ExpectedPlacementZone expectedPlacement = ExpectedPlacementZone::Balanced;
    FragmentationBenefit fragmentationBenefit = FragmentationBenefit::Unknown;
    double fragmentationBenefitScore = 0.0;
    bool excluded = false;
    bool moveOnlyWhenExplicit = false;
    std::wstring directoryRuleReason;
    std::wstring exclusionReason;
};

// Stores classification output for one analysed file while keeping file data in AnalysisResult::files.
struct FileClassification {
    std::size_t fileIndex = 0;
    FileClass classification;
};

// Stores aggregate counts used to summarize classification without rescanning per-file results.
struct ClassificationSummary {
    std::map<FileSizeClass, count64_t> sizeCounts;
    std::map<BroadFileType, count64_t> typeCounts;
    std::map<FileTemperature, count64_t> temperatureCounts;
    std::map<ExpectedPlacementZone, count64_t> placementCounts;
    count64_t excludedFiles = count64_t();
    count64_t explicitOnlyFiles = count64_t();
    count64_t beneficialFragmentationFiles = count64_t();
    count64_t highBenefitFragmentationFiles = count64_t();
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

enum class ZoneBoundaryMode { Percent, Bytes };

// Defines one configurable placement-zone boundary using either drive percentage or absolute bytes.
struct ZoneBoundarySetting {
    DiskZoneRole role = DiskZoneRole::Balanced;
    ZoneBoundaryMode mode = ZoneBoundaryMode::Percent;
    double startPercent = 0.0;
    double sizePercent = 100.0;
    byte_count64_t startBytes = byte_count64_t();
    byte_count64_t sizeBytes = byte_count64_t();
    bool enabled = true;
};

// Stores extension-group hints that future profile persistence and strategy UI can edit.
struct ExtensionGroupRule {
    BroadFileType type = BroadFileType::Unknown;
    std::wstring name;
    std::vector<std::wstring> extensions;
};

// Stores directory placement overrides for future profile persistence and strategy UI.
struct DirectoryOverrideRule {
    std::filesystem::path path;
    ExpectedPlacementZone placement = ExpectedPlacementZone::Balanced;
    bool recursive = true;
};

// Stores configurable knobs shared by future optimization profiles.
struct OptimizationSettings {
    bool enableFastZone = true;
    bool enableBalancedZone = true;
    bool enableSlowZone = true;
    bool enableLargeFileZone = true;
    bool enableFreeSpaceReserve = true;
    std::vector<ZoneBoundarySetting> zoneBoundaries;
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
    std::chrono::hours warmRecency = std::chrono::hours(24 * 60);
    std::chrono::hours coolRecency = std::chrono::hours(24 * 180);
    std::chrono::hours coldRecency = std::chrono::hours(24 * 365);
    std::chrono::hours staleRecency = std::chrono::hours(24 * 365);
    std::vector<ExtensionGroupRule> extensionGroups;
    std::vector<DirectoryOverrideRule> directoryOverrides;
};

// Names a selectable optimization profile and the settings it uses.
struct OptimizationProfile {
    OptimizationMode mode = OptimizationMode::BalancedDataDrive;
    std::wstring name = L"Balanced data-drive optimization";
    OptimizationSettings settings;
};

// Stores the read-only analysis snapshot for one drive.
struct AnalysisResult {
    DriveInfo drive;
    VolumeInfo volume;
    DiskGeometry geometry;
    std::vector<FileMetadata> files;
    std::vector<FileClassification> classifications;
    ClassificationSummary classificationSummary;
    FreeSpaceMap freeSpace;
    // Stores aggregate metrics derived during read-only analysis.
    struct AnalysisStats {
        count64_t scannedFiles = count64_t();
        count64_t skippedFiles = count64_t();
        count64_t inaccessibleFiles = count64_t();
        count64_t filesWithExtents = count64_t();
        count64_t fragmentedFiles = count64_t();
        count64_t totalFragments = count64_t();
        count64_t freeSpaceBlocks = count64_t();
        count64_t largestFreeBlockSectors = count64_t();
        bool freeSpaceMapAvailable = false;
        bool cancelled = false;
    } stats;
    bool synthetic = false;
    std::wstring summary;
};

// Stores target placement intent before concrete move planning exists.
struct PlacementPlan {
    OptimizationProfile profile;
    std::vector<DiskZone> zones;
    // Stores dry-run target placement for one analysed file without describing a move operation.
    struct FilePlacementIntent {
        std::size_t fileIndex = 0;
        ExpectedPlacementZone targetZone = ExpectedPlacementZone::None;
        double benefitScore = 0.0;
        byte_count64_t bytesConsidered = byte_count64_t();
        std::wstring reason;
    };
    std::vector<FilePlacementIntent> fileIntents;
    count64_t targetedFiles = count64_t();
    count64_t noTargetFiles = count64_t();
    byte_count64_t bytesConsidered = byte_count64_t();
    std::wstring summary;
};

// Describes one future file move without executing it.
struct MoveOperation {
    std::filesystem::path filePath;
    index64_t sourceStartCluster = index64_t();
    index64_t targetStartCluster = index64_t();
    count64_t clusterCount = count64_t();
    byte_count64_t estimatedBytes = byte_count64_t();
    std::wstring reason;
};

// Stores a dry-run-friendly sequence of future move operations.
struct MovePlan {
    OptimizationProfile profile;
    std::vector<MoveOperation> operations;
    byte_count64_t estimatedBytesToMove = byte_count64_t();
    bool dryRun = true;
    std::wstring summary;
};

enum class JobState { Idle, Running, Cancelling, Cancelled, Completed, Failed };

// Describes progress from worker jobs without depending on wxWidgets.
struct JobProgress {
    JobState state = JobState::Idle;
    double percentComplete = 0.0;
    std::wstring statusMessage;
    std::wstring currentItem;
    count64_t itemsProcessed = count64_t();
    count64_t totalItems = count64_t();
    bool cancellationRequested = false;
};

} // namespace icd
