#include "../precompiled.h"
#include "MovePlanner.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace icd {

namespace {
// Stores an available simulated free range while the dry-run planner reserves targets.
struct AvailableRange {
    index64_t startCluster;
    count64_t clusterCount;
};

// Stores a scored candidate before final budget and destination reservation decisions.
struct MoveCandidate {
    const PlacementPlan::FilePlacementIntent* intent = nullptr;
    const FileMetadata* file = nullptr;
    ExpectedPlacementZone currentZone = ExpectedPlacementZone::None;
    ExpectedPlacementZone targetZone = ExpectedPlacementZone::None;
    std::uint64_t requiredClusters = 0;
    double benefitScore = 0.0;
    double benefitPerByte = 0.0;
    MoveRisk risk = MoveRisk::Low;
};

std::uint64_t BytesPerCluster(const AnalysisResult& analysis)
{
    const std::uint64_t value = analysis.volume.bytesPerCluster.getValue() > 0
                                    ? analysis.volume.bytesPerCluster.getValue()
                                    : analysis.drive.volume.bytesPerCluster.getValue();
    return value > 0 ? value : 4096;
}

std::uint64_t SectorsPerCluster(const AnalysisResult& analysis)
{
    const std::uint64_t value = analysis.drive.sectorsPerCluster.getValue();
    return value > 0 ? value : 1;
}

std::uint64_t CeilDiv(std::uint64_t value, std::uint64_t divisor)
{
    if (value == 0 || divisor == 0) {
        return 0;
    }
    return ((value - 1) / divisor) + 1;
}

std::uint64_t RangeEnd(std::uint64_t start, std::uint64_t count)
{
    const std::uint64_t end = start + count;
    return end < start ? UINT64_MAX : end;
}

ExpectedPlacementZone PlacementForRole(DiskZoneRole role)
{
    switch (role) {
    case DiskZoneRole::Fast:
        return ExpectedPlacementZone::Fast;
    case DiskZoneRole::Balanced:
        return ExpectedPlacementZone::Balanced;
    case DiskZoneRole::Slow:
        return ExpectedPlacementZone::Slow;
    case DiskZoneRole::LargeFile:
        return ExpectedPlacementZone::LargeFile;
    case DiskZoneRole::FreeSpaceReserve:
    default:
        return ExpectedPlacementZone::None;
    }
}

std::wstring ZoneName(ExpectedPlacementZone zone)
{
    switch (zone) {
    case ExpectedPlacementZone::Fast:
        return L"fast";
    case ExpectedPlacementZone::Balanced:
        return L"balanced";
    case ExpectedPlacementZone::Slow:
        return L"slow";
    case ExpectedPlacementZone::LargeFile:
        return L"large-file";
    case ExpectedPlacementZone::None:
    default:
        return L"none";
    }
}

bool ZoneContainsCluster(const DiskZone& zone, std::uint64_t cluster, std::uint64_t sectorsPerCluster)
{
    const std::uint64_t zoneStartCluster = zone.startSector.getValue() / sectorsPerCluster;
    const std::uint64_t zoneClusters = zone.sectorCount.getValue() / sectorsPerCluster;
    return cluster >= zoneStartCluster && cluster < RangeEnd(zoneStartCluster, zoneClusters);
}

ExpectedPlacementZone CurrentZoneForFile(const FileMetadata& file,
                                         const PlacementPlan& placementPlan,
                                         std::uint64_t sectorsPerCluster)
{
    if (file.GetFragments().empty()) {
        return ExpectedPlacementZone::None;
    }

    const std::uint64_t firstCluster = file.GetFragments().front().startCluster.getValue();
    for (const DiskZone& zone : placementPlan.zones) {
        if (zone.enabled && ZoneContainsCluster(zone, firstCluster, sectorsPerCluster)) {
            return PlacementForRole(zone.role);
        }
    }
    return ExpectedPlacementZone::None;
}

bool FileAlreadyGoodEnough(const FileMetadata& file,
                           ExpectedPlacementZone currentZone,
                           ExpectedPlacementZone targetZone)
{
    return currentZone == targetZone && file.GetFragments().size() <= 1;
}

MoveRisk RiskForFile(const FileMetadata& file)
{
    const FileMetadata::AttributeFlags& flags = file.GetAttributeFlags();
    if (flags.system || flags.reparsePoint || flags.encrypted || file.IsRiskyOrUnmovable()) {
        return MoveRisk::High;
    }
    if (flags.sparse || flags.compressed || flags.hidden) {
        return MoveRisk::Medium;
    }
    return MoveRisk::Low;
}

void AddSkip(MovePlan& plan, std::size_t fileIndex, MoveSkipReason reason, std::wstring detail)
{
    plan.skippedCandidates.push_back({fileIndex, reason, std::move(detail)});
    plan.metrics.skippedFiles += count64_t(1);
}

std::vector<AvailableRange> BuildAvailableRangesForZone(const AnalysisResult& analysis,
                                                        const PlacementPlan& placementPlan,
                                                        ExpectedPlacementZone targetZone)
{
    const std::uint64_t sectorsPerCluster = SectorsPerCluster(analysis);
    std::vector<AvailableRange> ranges;
    for (const FreeSpaceMap::FreeBlock& block : analysis.freeSpace.GetFreeBlocks()) {
        const std::uint64_t blockStart = block.startSector.getValue();
        const std::uint64_t blockEnd = RangeEnd(blockStart, block.sectorCount.getValue());
        for (const DiskZone& zone : placementPlan.zones) {
            if (!zone.enabled || PlacementForRole(zone.role) != targetZone) {
                continue;
            }

            const std::uint64_t zoneStart = zone.startSector.getValue() / sectorsPerCluster;
            const std::uint64_t zoneEnd = RangeEnd(zoneStart, zone.sectorCount.getValue() / sectorsPerCluster);
            const std::uint64_t start = (std::max)(blockStart, zoneStart);
            const std::uint64_t end = (std::min)(blockEnd, zoneEnd);
            if (end > start) {
                ranges.push_back({index64_t(start), count64_t(end - start)});
            }
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const AvailableRange& lhs, const AvailableRange& rhs) {
        if (lhs.clusterCount.getValue() == rhs.clusterCount.getValue()) {
            return lhs.startCluster.getValue() < rhs.startCluster.getValue();
        }
        return lhs.clusterCount.getValue() > rhs.clusterCount.getValue();
    });
    return ranges;
}

bool ReserveRange(std::vector<AvailableRange>& ranges,
                  std::uint64_t requiredClusters,
                  ExpectedPlacementZone zone,
                  ReservedTargetRange& reserved)
{
    for (AvailableRange& range : ranges) {
        if (range.clusterCount.getValue() < requiredClusters) {
            continue;
        }

        reserved.zone = zone;
        reserved.startCluster = range.startCluster;
        reserved.clusterCount = count64_t(requiredClusters);
        range.startCluster = index64_t(range.startCluster.getValue() + requiredClusters);
        range.clusterCount = count64_t(range.clusterCount.getValue() - requiredClusters);
        return true;
    }
    return false;
}

std::unordered_map<std::size_t, const PlacementPlan::FilePlacementIntent*> BuildIntentIndex(const PlacementPlan& placementPlan)
{
    std::unordered_map<std::size_t, const PlacementPlan::FilePlacementIntent*> byFileIndex;
    for (const auto& intent : placementPlan.fileIntents) {
        byFileIndex[intent.fileIndex] = &intent;
    }
    return byFileIndex;
}
} // namespace

MovePlan MovePlanner::Build(const AnalysisResult& analysis,
                            const PlacementPlan& placementPlan,
                            const OptimizationProfile& profile) const
{
    MovePlan plan;
    plan.profile = profile;
    plan.dryRun = true;

    if (!analysis.stats.freeSpaceMapAvailable || analysis.freeSpace.IsEmpty()) {
        plan.partial = true;
        plan.impossible = true;
        plan.issues.push_back({L"Free-space bitmap is unavailable; destination ranges cannot be reserved.", true});
    }

    const std::uint64_t bytesPerCluster = BytesPerCluster(analysis);
    const std::uint64_t sectorsPerCluster = SectorsPerCluster(analysis);
    const std::uint64_t maxBytes = profile.settings.maximumBytesToMove.getValue();
    const bool unlimitedBudget = maxBytes == 0;
    const auto intentsByFile = BuildIntentIndex(placementPlan);
    std::vector<MoveCandidate> candidates;

    for (std::size_t fileIndex = 0; fileIndex < analysis.files.size(); ++fileIndex) {
        const auto intentFound = intentsByFile.find(fileIndex);
        if (intentFound == intentsByFile.end() || intentFound->second->targetZone == ExpectedPlacementZone::None) {
            AddSkip(plan, fileIndex, MoveSkipReason::DisabledTargetZone, L"no placement target");
            continue;
        }

        const FileMetadata& file = analysis.files[fileIndex];
        if (!file.HasExtents() || file.GetFragments().empty()) {
            AddSkip(plan, fileIndex, MoveSkipReason::MissingExtents, L"extent data unavailable");
            continue;
        }

        const MoveRisk risk = RiskForFile(file);
        if (risk == MoveRisk::High) {
            AddSkip(plan, fileIndex, MoveSkipReason::TooRisky, L"high-risk file attributes");
            continue;
        }

        const auto* intent = intentFound->second;
        const ExpectedPlacementZone currentZone = CurrentZoneForFile(file, placementPlan, sectorsPerCluster);
        if (FileAlreadyGoodEnough(file, currentZone, intent->targetZone)) {
            AddSkip(plan, fileIndex, MoveSkipReason::AlreadyGoodEnough, L"already contiguous in target zone");
            continue;
        }

        const std::uint64_t requiredClusters = CeilDiv(file.GetSize().getValue(), bytesPerCluster);
        if (requiredClusters == 0) {
            AddSkip(plan, fileIndex, MoveSkipReason::AlreadyGoodEnough, L"empty file");
            continue;
        }

        const double benefit = intent->benefitScore + (file.GetFragments().size() > 1 ? 1.0 : 0.0) +
                               (currentZone == intent->targetZone ? 0.0 : 0.5);
        if (benefit < profile.settings.minimumBenefitScore) {
            AddSkip(plan, fileIndex, MoveSkipReason::AlreadyGoodEnough, L"benefit below threshold");
            continue;
        }

        const double bytes = static_cast<double>((std::max)(std::uint64_t(1), file.GetSize().getValue()));
        candidates.push_back({intent,
                              &file,
                              currentZone,
                              intent->targetZone,
                              requiredClusters,
                              benefit,
                              benefit / bytes,
                              risk});
    }

    std::sort(candidates.begin(), candidates.end(), [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
        if (lhs.benefitPerByte == rhs.benefitPerByte) {
            return lhs.benefitScore > rhs.benefitScore;
        }
        return lhs.benefitPerByte > rhs.benefitPerByte;
    });

    std::unordered_map<int, std::vector<AvailableRange>> rangesByZone;
    for (ExpectedPlacementZone zone : {ExpectedPlacementZone::Fast,
                                       ExpectedPlacementZone::Balanced,
                                       ExpectedPlacementZone::Slow,
                                       ExpectedPlacementZone::LargeFile}) {
        rangesByZone.emplace(static_cast<int>(zone), BuildAvailableRangesForZone(analysis, placementPlan, zone));
    }

    for (const MoveCandidate& candidate : candidates) {
        const std::uint64_t fileBytes = candidate.file->GetSize().getValue();
        if (!unlimitedBudget && plan.estimatedBytesToMove.getValue() + fileBytes > maxBytes) {
            AddSkip(plan, candidate.intent->fileIndex, MoveSkipReason::OverBudget, L"maximum bytes-to-move reached");
            plan.partial = true;
            continue;
        }

        ReservedTargetRange reserved;
        auto& ranges = rangesByZone[static_cast<int>(candidate.targetZone)];
        if (!ReserveRange(ranges, candidate.requiredClusters, candidate.targetZone, reserved)) {
            AddSkip(plan, candidate.intent->fileIndex, MoveSkipReason::NoDestinationRange, L"no free range in target zone");
            plan.partial = true;
            continue;
        }

        MoveOperation operation;
        operation.fileIndex = candidate.intent->fileIndex;
        operation.filePath = candidate.file->GetPath();
        operation.sourceStartCluster = candidate.file->GetFragments().front().startCluster;
        operation.targetStartCluster = reserved.startCluster;
        operation.clusterCount = reserved.clusterCount;
        operation.estimatedBytes = candidate.file->GetSize();
        operation.currentZone = candidate.currentZone;
        operation.targetZone = candidate.targetZone;
        operation.fragmentCountBefore = count64_t(static_cast<std::uint64_t>(candidate.file->GetFragments().size()));
        operation.fragmentCountAfterEstimate = count64_t(1);
        operation.benefitScore = candidate.benefitScore;
        operation.risk = candidate.risk;
        operation.reason = L"move from " + ZoneName(candidate.currentZone) + L" to " + ZoneName(candidate.targetZone);
        operation.cancellationBoundary = L"safe before starting this file move";
        operation.rollbackNote = L"dry-run only; future execution must leave the source file unchanged until replacement succeeds";

        plan.operations.push_back(std::move(operation));
        plan.reservedTargetRanges.push_back(reserved);
        plan.metrics.affectedFiles += count64_t(1);
        plan.metrics.estimatedBytesToMove += candidate.file->GetSize();
        plan.estimatedBytesToMove += candidate.file->GetSize();
        if (candidate.currentZone != candidate.targetZone) {
            plan.metrics.expectedZoneChanges += count64_t(1);
        }
        if (candidate.file->GetFragments().size() > 1) {
            plan.metrics.fragmentationImprovementFiles += count64_t(1);
        }
        plan.metrics.freeSpaceReservations += count64_t(1);
    }

    if (plan.operations.empty() && !candidates.empty()) {
        plan.impossible = true;
        plan.issues.push_back({L"No candidate file could reserve a simulated destination range.", true});
    }
    if (plan.partial) {
        plan.issues.push_back({L"Plan is partial because budget or target free ranges prevented some candidate moves.", false});
    }

    std::wstringstream summary;
    summary << L"Move plan for " << profile.name << L": " << plan.metrics.affectedFiles.getValue()
            << L" files, " << plan.estimatedBytesToMove.getValue() << L" bytes, "
            << plan.metrics.skippedFiles.getValue() << L" skipped";
    if (plan.partial) {
        summary << L", partial";
    }
    if (plan.impossible) {
        summary << L", impossible";
    }
    summary << L".";
    plan.summary = summary.str();
    return plan;
}

} // namespace icd
