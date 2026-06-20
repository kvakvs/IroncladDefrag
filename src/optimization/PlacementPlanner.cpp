#include "../precompiled.h"
#include "PlacementPlanner.h"

#include <algorithm>
#include <memory>
#include <sstream>

namespace icd {

namespace {
std::uint64_t ClampU64(std::uint64_t value, std::uint64_t upper)
{
    return value > upper ? upper : value;
}

std::uint64_t TotalBytes(const AnalysisResult& analysis)
{
    const std::uint64_t fromVolume = analysis.volume.totalBytes.getValue();
    return fromVolume > 0 ? fromVolume : analysis.drive.volume.totalBytes.getValue();
}

std::uint64_t BytesPerSector(const AnalysisResult& analysis)
{
    const std::uint64_t fromGeometry = analysis.geometry.GetBytesPerSector().getValue();
    if (fromGeometry > 0) {
        return fromGeometry;
    }
    const std::uint64_t fromDrive = analysis.drive.bytesPerSector.getValue();
    return fromDrive > 0 ? fromDrive : 512;
}

ExpectedPlacementZone PlacementForRole(DiskZoneRole role)
{
    switch (role) {
    case DiskZoneRole::Fast:
        return ExpectedPlacementZone::Fast;
    case DiskZoneRole::Slow:
        return ExpectedPlacementZone::Slow;
    case DiskZoneRole::LargeFile:
        return ExpectedPlacementZone::LargeFile;
    case DiskZoneRole::Balanced:
        return ExpectedPlacementZone::Balanced;
    case DiskZoneRole::FreeSpaceReserve:
    default:
        return ExpectedPlacementZone::None;
    }
}

DiskZoneRole RoleForPlacement(ExpectedPlacementZone placement)
{
    switch (placement) {
    case ExpectedPlacementZone::Fast:
        return DiskZoneRole::Fast;
    case ExpectedPlacementZone::Slow:
        return DiskZoneRole::Slow;
    case ExpectedPlacementZone::LargeFile:
        return DiskZoneRole::LargeFile;
    case ExpectedPlacementZone::Balanced:
        return DiskZoneRole::Balanced;
    case ExpectedPlacementZone::None:
    default:
        return DiskZoneRole::FreeSpaceReserve;
    }
}

bool IsZoneEnabled(const OptimizationSettings& settings, ExpectedPlacementZone placement)
{
    switch (placement) {
    case ExpectedPlacementZone::Fast:
        return settings.enableFastZone;
    case ExpectedPlacementZone::Balanced:
        return settings.enableBalancedZone;
    case ExpectedPlacementZone::Slow:
        return settings.enableSlowZone;
    case ExpectedPlacementZone::LargeFile:
        return settings.enableLargeFileZone;
    case ExpectedPlacementZone::None:
    default:
        return false;
    }
}

double BenefitForClass(const FileClass& classification)
{
    if (classification.fragmentationBenefitScore > 0.0) {
        return classification.fragmentationBenefitScore;
    }

    switch (classification.fragmentationBenefit) {
    case FragmentationBenefit::High:
        return 1.0;
    case FragmentationBenefit::Medium:
        return 0.6;
    case FragmentationBenefit::Low:
        return 0.25;
    case FragmentationBenefit::None:
    case FragmentationBenefit::Unknown:
    default:
        return 0.0;
    }
}

std::vector<DiskZone> BuildZones(const AnalysisResult& analysis, const OptimizationSettings& settings)
{
    const std::uint64_t driveBytes = TotalBytes(analysis);
    const std::uint64_t sectorBytes = BytesPerSector(analysis);
    const std::uint64_t totalSectors = sectorBytes == 0 ? 0 : driveBytes / sectorBytes;
    std::vector<DiskZone> zones;

    for (const ZoneBoundarySetting& boundary : settings.zoneBoundaries) {
        const ExpectedPlacementZone placement = PlacementForRole(boundary.role);
        const bool enabled = boundary.enabled &&
                             (placement == ExpectedPlacementZone::None || IsZoneEnabled(settings, placement)) &&
                             (boundary.role != DiskZoneRole::FreeSpaceReserve || settings.enableFreeSpaceReserve);

        std::uint64_t startBytes = boundary.startBytes.getValue();
        std::uint64_t sizeBytes = boundary.sizeBytes.getValue();
        if (boundary.mode == ZoneBoundaryMode::Percent) {
            startBytes = static_cast<std::uint64_t>((static_cast<long double>(driveBytes) * boundary.startPercent) / 100.0L);
            sizeBytes = static_cast<std::uint64_t>((static_cast<long double>(driveBytes) * boundary.sizePercent) / 100.0L);
        }

        const std::uint64_t startSector = sectorBytes == 0 ? 0 : ClampU64(startBytes / sectorBytes, totalSectors);
        const std::uint64_t endByte = ClampU64(startBytes + sizeBytes, driveBytes);
        const std::uint64_t endSector = sectorBytes == 0 ? startSector : ClampU64(endByte / sectorBytes, totalSectors);
        const std::uint64_t sectorCount = endSector > startSector ? endSector - startSector : 0;
        zones.push_back({boundary.role, index64_t(startSector), count64_t(sectorCount), enabled});
    }

    return zones;
}

ExpectedPlacementZone ModeSpecificPlacement(const FileClassification& fileClass, OptimizationMode mode)
{
    const FileClass& classification = fileClass.classification;
    switch (mode) {
    case OptimizationMode::LargeFileContiguous:
        if (classification.sizeClass == FileSizeClass::Large || classification.sizeClass == FileSizeClass::Huge ||
            classification.broadType == BroadFileType::Media || classification.broadType == BroadFileType::VirtualDisk ||
            classification.broadType == BroadFileType::Archive) {
            return ExpectedPlacementZone::LargeFile;
        }
        return ExpectedPlacementZone::Balanced;
    case OptimizationMode::SmallFileFastZone:
        if ((classification.sizeClass == FileSizeClass::Tiny || classification.sizeClass == FileSizeClass::Small) &&
            (classification.temperature == FileTemperature::Hot || classification.temperature == FileTemperature::Warm ||
             classification.broadType == BroadFileType::SourceProject)) {
            return ExpectedPlacementZone::Fast;
        }
        return classification.expectedPlacement;
    case OptimizationMode::ColdArchive:
        if (classification.temperature == FileTemperature::Cold || classification.temperature == FileTemperature::Stale ||
            classification.broadType == BroadFileType::Archive || classification.broadType == BroadFileType::Backup) {
            return ExpectedPlacementZone::Slow;
        }
        return ExpectedPlacementZone::Balanced;
    case OptimizationMode::DirectoryClustering:
        return classification.expectedPlacement == ExpectedPlacementZone::None ? ExpectedPlacementZone::Balanced
                                                                               : classification.expectedPlacement;
    case OptimizationMode::FileTypeSegregation:
        if (classification.broadType == BroadFileType::Media || classification.broadType == BroadFileType::Archive ||
            classification.broadType == BroadFileType::VirtualDisk) {
            return ExpectedPlacementZone::LargeFile;
        }
        if (classification.broadType == BroadFileType::SourceProject) {
            return ExpectedPlacementZone::Fast;
        }
        return ExpectedPlacementZone::Balanced;
    case OptimizationMode::SizeBasedPlacement:
        if (classification.sizeClass == FileSizeClass::Tiny || classification.sizeClass == FileSizeClass::Small) {
            return ExpectedPlacementZone::Fast;
        }
        if (classification.sizeClass == FileSizeClass::Large || classification.sizeClass == FileSizeClass::Huge) {
            return ExpectedPlacementZone::LargeFile;
        }
        return ExpectedPlacementZone::Balanced;
    case OptimizationMode::FreeSpaceOptimization:
        return ExpectedPlacementZone::Balanced;
    case OptimizationMode::SingleFileDefragmentation:
        return BenefitForClass(classification) > 0.0 ? ExpectedPlacementZone::Balanced : ExpectedPlacementZone::None;
    case OptimizationMode::BalancedDataDrive:
    default:
        return classification.expectedPlacement;
    }
}

std::wstring ReasonForPlacement(OptimizationMode mode, ExpectedPlacementZone placement)
{
    std::wstringstream reason;
    reason << L"profile mode " << static_cast<int>(mode) << L" targets zone " << static_cast<int>(placement);
    return reason.str();
}

// Implements the shared dry-run placement-intent behavior for all Phase 4 profiles.
class BasicPlacementStrategy : public IPlacementStrategy {
public:
    PlacementPlan Build(const AnalysisResult& analysis,
                        const OptimizationProfile& profile,
                        const std::function<bool(double, const std::wstring&)>& progressCallback) const override
    {
        PlacementPlan plan;
        plan.profile = profile;
        plan.zones = BuildZones(analysis, profile.settings);

        if (progressCallback && !progressCallback(0.0, L"Preparing placement intent")) {
            return plan;
        }

        const std::size_t total = analysis.classifications.size();
        for (std::size_t index = 0; index < total; ++index) {
            const FileClassification& item = analysis.classifications[index];
            if (item.fileIndex >= analysis.files.size()) {
                continue;
            }

            const FileClass& classification = item.classification;
            const FileMetadata& file = analysis.files[item.fileIndex];
            if (progressCallback && (index % 128 == 0 || index + 1 == total)) {
                const double percent = total == 0 ? 100.0 : (static_cast<double>(index + 1) / static_cast<double>(total)) * 100.0;
                if (!progressCallback(percent, file.GetPath().wstring())) {
                    return plan;
                }
            }

            ExpectedPlacementZone target = ExpectedPlacementZone::None;
            double benefit = BenefitForClass(classification);

            if (!classification.excluded && !classification.moveOnlyWhenExplicit &&
                benefit >= profile.settings.minimumBenefitScore) {
                target = ModeSpecificPlacement(item, profile.mode);
                if (!IsZoneEnabled(profile.settings, target)) {
                    target = ExpectedPlacementZone::None;
                }
            }

            PlacementPlan::FilePlacementIntent intent;
            intent.fileIndex = item.fileIndex;
            intent.targetZone = target;
            intent.benefitScore = benefit;
            intent.bytesConsidered = file.GetSize();
            intent.reason = target == ExpectedPlacementZone::None ? L"excluded, explicit-only, low benefit, or disabled zone"
                                                                  : ReasonForPlacement(profile.mode, target);
            plan.fileIntents.push_back(std::move(intent));
            plan.bytesConsidered += file.GetSize();

            if (target == ExpectedPlacementZone::None) {
                plan.noTargetFiles += count64_t(1);
            }
            else {
                plan.targetedFiles += count64_t(1);
            }
        }

        std::wstringstream summary;
        summary << L"Placement intent for " << profile.name << L": " << plan.targetedFiles.getValue()
                << L" targeted files, " << plan.noTargetFiles.getValue() << L" without target.";
        plan.summary = summary.str();
        if (progressCallback) {
            progressCallback(100.0, L"Placement intent built");
        }
        return plan;
    }
};
} // namespace

PlacementPlan PlacementPlanner::Build(const AnalysisResult& analysis,
                                      const OptimizationProfile& profile,
                                      const std::function<bool(double, const std::wstring&)>& progressCallback) const
{
    BasicPlacementStrategy strategy;
    return strategy.Build(analysis, profile, progressCallback);
}

} // namespace icd
