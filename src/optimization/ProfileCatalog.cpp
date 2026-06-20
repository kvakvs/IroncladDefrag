#include "../precompiled.h"
#include "ProfileCatalog.h"

namespace icd {

namespace {
ZoneBoundarySetting PercentZone(DiskZoneRole role, double start, double size, bool enabled)
{
    ZoneBoundarySetting zone;
    zone.role = role;
    zone.mode = ZoneBoundaryMode::Percent;
    zone.startPercent = start;
    zone.sizePercent = size;
    zone.enabled = enabled;
    return zone;
}

std::vector<ZoneBoundarySetting> DefaultZones()
{
    return {PercentZone(DiskZoneRole::Fast, 0.0, 15.0, true),
            PercentZone(DiskZoneRole::Balanced, 15.0, 55.0, true),
            PercentZone(DiskZoneRole::Slow, 70.0, 15.0, true),
            PercentZone(DiskZoneRole::LargeFile, 85.0, 10.0, true),
            PercentZone(DiskZoneRole::FreeSpaceReserve, 95.0, 5.0, true)};
}

OptimizationProfile MakeProfile(OptimizationMode mode, const std::wstring& name)
{
    OptimizationProfile profile;
    profile.mode = mode;
    profile.name = name;
    profile.settings.zoneBoundaries = DefaultZones();
    profile.settings.maximumBytesToMove = byte_count64_t();
    return profile;
}
} // namespace

std::vector<OptimizationProfile> ProfileCatalog::CreateDefaultProfiles() const
{
    std::vector<OptimizationProfile> profiles;
    profiles.push_back(MakeProfile(OptimizationMode::BalancedDataDrive, L"Balanced data-drive optimization"));

    auto largeFile = MakeProfile(OptimizationMode::LargeFileContiguous, L"Large-file contiguous optimization");
    largeFile.settings.preserveDirectoryLocality = false;
    largeFile.settings.allowLargeFileMoves = true;
    profiles.push_back(std::move(largeFile));

    auto smallFast = MakeProfile(OptimizationMode::SmallFileFastZone, L"Small-file fast-zone optimization");
    smallFast.settings.enableSlowZone = false;
    profiles.push_back(std::move(smallFast));

    auto coldArchive = MakeProfile(OptimizationMode::ColdArchive, L"Cold archive optimization");
    coldArchive.settings.allowLargeFileMoves = false;
    profiles.push_back(std::move(coldArchive));

    auto directory = MakeProfile(OptimizationMode::DirectoryClustering, L"Directory clustering");
    directory.settings.preserveDirectoryLocality = true;
    profiles.push_back(std::move(directory));

    profiles.push_back(MakeProfile(OptimizationMode::FileTypeSegregation, L"File-type segregation"));
    profiles.push_back(MakeProfile(OptimizationMode::SizeBasedPlacement, L"Size-based placement"));

    auto freeSpace = MakeProfile(OptimizationMode::FreeSpaceOptimization, L"Free-space optimization");
    freeSpace.settings.prioritizeFreeSpaceConsolidation = true;
    profiles.push_back(std::move(freeSpace));

    auto singleFile = MakeProfile(OptimizationMode::SingleFileDefragmentation, L"Single-file defragmentation");
    singleFile.settings.enableSlowZone = false;
    singleFile.settings.enableLargeFileZone = false;
    profiles.push_back(std::move(singleFile));

    return profiles;
}

} // namespace icd
