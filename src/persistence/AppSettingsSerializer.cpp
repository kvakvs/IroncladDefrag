#include "../precompiled.h"
#include "AppSettingsSerializer.h"

#include <sstream>
#include <unordered_map>

namespace icd {

namespace {
std::wstring BoolText(bool value)
{
    return value ? L"true" : L"false";
}

bool ParseBool(const std::wstring& value)
{
    return value == L"true" || value == L"1";
}

std::uint64_t ParseU64(const std::unordered_map<std::wstring, std::wstring>& values,
                       const std::wstring& key,
                       std::uint64_t fallback)
{
    const auto found = values.find(key);
    if (found == values.end()) {
        return fallback;
    }
    try {
        return std::stoull(found->second);
    } catch (...) {
        return fallback;
    }
}

double ParseDouble(const std::unordered_map<std::wstring, std::wstring>& values,
                   const std::wstring& key,
                   double fallback)
{
    const auto found = values.find(key);
    if (found == values.end()) {
        return fallback;
    }
    try {
        return std::stod(found->second);
    } catch (...) {
        return fallback;
    }
}

bool ParseStoredBool(const std::unordered_map<std::wstring, std::wstring>& values,
                     const std::wstring& key,
                     bool fallback)
{
    const auto found = values.find(key);
    return found == values.end() ? fallback : ParseBool(found->second);
}

OptimizationMode ParseMode(std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(OptimizationMode::SingleFileDefragmentation)) {
        return OptimizationMode::BalancedDataDrive;
    }
    return static_cast<OptimizationMode>(value);
}

DiskZoneRole ParseRole(std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(DiskZoneRole::FreeSpaceReserve)) {
        return DiskZoneRole::Balanced;
    }
    return static_cast<DiskZoneRole>(value);
}

ZoneBoundaryMode ParseBoundaryMode(std::uint64_t value)
{
    if (value > static_cast<std::uint64_t>(ZoneBoundaryMode::Bytes)) {
        return ZoneBoundaryMode::Percent;
    }
    return static_cast<ZoneBoundaryMode>(value);
}

std::wstring Prefix(const std::wstring& root, std::uint64_t index)
{
    return root + L"." + std::to_wstring(index) + L".";
}

void SerializeProfile(std::wstringstream& text, const std::wstring& prefix, const OptimizationProfile& profile)
{
    const OptimizationSettings& settings = profile.settings;
    text << prefix << L"mode=" << static_cast<int>(profile.mode) << L"\n";
    text << prefix << L"name=" << profile.name << L"\n";
    text << prefix << L"enableFastZone=" << BoolText(settings.enableFastZone) << L"\n";
    text << prefix << L"enableBalancedZone=" << BoolText(settings.enableBalancedZone) << L"\n";
    text << prefix << L"enableSlowZone=" << BoolText(settings.enableSlowZone) << L"\n";
    text << prefix << L"enableLargeFileZone=" << BoolText(settings.enableLargeFileZone) << L"\n";
    text << prefix << L"enableFreeSpaceReserve=" << BoolText(settings.enableFreeSpaceReserve) << L"\n";
    text << prefix << L"smallFileThreshold=" << settings.smallFileThreshold.getValue() << L"\n";
    text << prefix << L"largeFileThreshold=" << settings.largeFileThreshold.getValue() << L"\n";
    text << prefix << L"hugeFileThreshold=" << settings.hugeFileThreshold.getValue() << L"\n";
    text << prefix << L"maximumBytesToMove=" << settings.maximumBytesToMove.getValue() << L"\n";
    text << prefix << L"minimumBenefitScore=" << settings.minimumBenefitScore << L"\n";
    text << prefix << L"preserveDirectoryLocality=" << BoolText(settings.preserveDirectoryLocality) << L"\n";
    text << prefix << L"allowLargeFileMoves=" << BoolText(settings.allowLargeFileMoves) << L"\n";
    text << prefix << L"prioritizeFreeSpaceConsolidation=" << BoolText(settings.prioritizeFreeSpaceConsolidation) << L"\n";
    text << prefix << L"dryRunOnly=" << BoolText(settings.dryRunOnly) << L"\n";
    text << prefix << L"hotHours=" << settings.hotRecency.count() << L"\n";
    text << prefix << L"warmHours=" << settings.warmRecency.count() << L"\n";
    text << prefix << L"coolHours=" << settings.coolRecency.count() << L"\n";
    text << prefix << L"coldHours=" << settings.coldRecency.count() << L"\n";
    text << prefix << L"staleHours=" << settings.staleRecency.count() << L"\n";

    text << prefix << L"zoneCount=" << settings.zoneBoundaries.size() << L"\n";
    for (std::size_t index = 0; index < settings.zoneBoundaries.size(); ++index) {
        const ZoneBoundarySetting& zone = settings.zoneBoundaries[index];
        const std::wstring zonePrefix = prefix + Prefix(L"zone", static_cast<std::uint64_t>(index));
        text << zonePrefix << L"role=" << static_cast<int>(zone.role) << L"\n";
        text << zonePrefix << L"mode=" << static_cast<int>(zone.mode) << L"\n";
        text << zonePrefix << L"startPercent=" << zone.startPercent << L"\n";
        text << zonePrefix << L"sizePercent=" << zone.sizePercent << L"\n";
        text << zonePrefix << L"startBytes=" << zone.startBytes.getValue() << L"\n";
        text << zonePrefix << L"sizeBytes=" << zone.sizeBytes.getValue() << L"\n";
        text << zonePrefix << L"enabled=" << BoolText(zone.enabled) << L"\n";
    }
}

OptimizationProfile DeserializeProfile(const std::unordered_map<std::wstring, std::wstring>& values,
                                       const std::wstring& prefix)
{
    OptimizationProfile profile;
    profile.mode = ParseMode(ParseU64(values, prefix + L"mode", static_cast<std::uint64_t>(profile.mode)));
    if (const auto found = values.find(prefix + L"name"); found != values.end()) {
        profile.name = found->second;
    }

    OptimizationSettings& settings = profile.settings;
    settings.enableFastZone = ParseStoredBool(values, prefix + L"enableFastZone", settings.enableFastZone);
    settings.enableBalancedZone = ParseStoredBool(values, prefix + L"enableBalancedZone", settings.enableBalancedZone);
    settings.enableSlowZone = ParseStoredBool(values, prefix + L"enableSlowZone", settings.enableSlowZone);
    settings.enableLargeFileZone = ParseStoredBool(values, prefix + L"enableLargeFileZone", settings.enableLargeFileZone);
    settings.enableFreeSpaceReserve = ParseStoredBool(values, prefix + L"enableFreeSpaceReserve", settings.enableFreeSpaceReserve);
    settings.smallFileThreshold =
        byte_count64_t(ParseU64(values, prefix + L"smallFileThreshold", settings.smallFileThreshold.getValue()));
    settings.largeFileThreshold =
        byte_count64_t(ParseU64(values, prefix + L"largeFileThreshold", settings.largeFileThreshold.getValue()));
    settings.hugeFileThreshold =
        byte_count64_t(ParseU64(values, prefix + L"hugeFileThreshold", settings.hugeFileThreshold.getValue()));
    settings.maximumBytesToMove =
        byte_count64_t(ParseU64(values, prefix + L"maximumBytesToMove", settings.maximumBytesToMove.getValue()));
    settings.minimumBenefitScore = ParseDouble(values, prefix + L"minimumBenefitScore", settings.minimumBenefitScore);
    settings.preserveDirectoryLocality =
        ParseStoredBool(values, prefix + L"preserveDirectoryLocality", settings.preserveDirectoryLocality);
    settings.allowLargeFileMoves = ParseStoredBool(values, prefix + L"allowLargeFileMoves", settings.allowLargeFileMoves);
    settings.prioritizeFreeSpaceConsolidation =
        ParseStoredBool(values, prefix + L"prioritizeFreeSpaceConsolidation", settings.prioritizeFreeSpaceConsolidation);
    settings.dryRunOnly = ParseStoredBool(values, prefix + L"dryRunOnly", settings.dryRunOnly);
    settings.hotRecency = std::chrono::hours(ParseU64(values, prefix + L"hotHours", settings.hotRecency.count()));
    settings.warmRecency = std::chrono::hours(ParseU64(values, prefix + L"warmHours", settings.warmRecency.count()));
    settings.coolRecency = std::chrono::hours(ParseU64(values, prefix + L"coolHours", settings.coolRecency.count()));
    settings.coldRecency = std::chrono::hours(ParseU64(values, prefix + L"coldHours", settings.coldRecency.count()));
    settings.staleRecency = std::chrono::hours(ParseU64(values, prefix + L"staleHours", settings.staleRecency.count()));

    const std::uint64_t zoneCount = ParseU64(values, prefix + L"zoneCount", 0);
    settings.zoneBoundaries.clear();
    for (std::uint64_t index = 0; index < zoneCount; ++index) {
        const std::wstring zonePrefix = prefix + Prefix(L"zone", index);
        ZoneBoundarySetting zone;
        zone.role = ParseRole(ParseU64(values, zonePrefix + L"role", static_cast<std::uint64_t>(zone.role)));
        zone.mode =
            ParseBoundaryMode(ParseU64(values, zonePrefix + L"mode", static_cast<std::uint64_t>(zone.mode)));
        zone.startPercent = ParseDouble(values, zonePrefix + L"startPercent", zone.startPercent);
        zone.sizePercent = ParseDouble(values, zonePrefix + L"sizePercent", zone.sizePercent);
        zone.startBytes = byte_count64_t(ParseU64(values, zonePrefix + L"startBytes", zone.startBytes.getValue()));
        zone.sizeBytes = byte_count64_t(ParseU64(values, zonePrefix + L"sizeBytes", zone.sizeBytes.getValue()));
        zone.enabled = ParseStoredBool(values, zonePrefix + L"enabled", zone.enabled);
        settings.zoneBoundaries.push_back(zone);
    }
    return profile;
}

std::unordered_map<std::wstring, std::wstring> ParseValues(const std::wstring& text)
{
    std::unordered_map<std::wstring, std::wstring> values;
    std::wstringstream input(text);
    std::wstring line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        const std::size_t separator = line.find(L'=');
        if (separator == std::wstring::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}
} // namespace

std::wstring AppSettingsSerializer::Serialize(const AppSettings& settings) const
{
    std::wstringstream text;
    text << L"version=1\n";
    text << L"activeProfileMode=" << static_cast<int>(settings.activeProfileMode) << L"\n";

    const SafetySettings& safety = settings.safetySettings;
    text << L"safety.defaultDryRunOnly=" << BoolText(safety.defaultDryRunOnly) << L"\n";
    text << L"safety.excludeProtectedSystemPaths=" << BoolText(safety.excludeProtectedSystemPaths) << L"\n";
    text << L"safety.globalMaximumBytesToMove=" << safety.globalMaximumBytesToMove.getValue() << L"\n";

    text << L"safety.directoryCount=" << safety.excludedDirectories.size() << L"\n";
    for (std::size_t index = 0; index < safety.excludedDirectories.size(); ++index) {
        text << L"safety.directory." << index << L"=" << safety.excludedDirectories[index].wstring() << L"\n";
    }

    text << L"safety.extensionCount=" << safety.excludedExtensions.size() << L"\n";
    for (std::size_t index = 0; index < safety.excludedExtensions.size(); ++index) {
        text << L"safety.extension." << index << L"=" << safety.excludedExtensions[index] << L"\n";
    }

    text << L"safety.sizeRangeCount=" << safety.excludedSizeRanges.size() << L"\n";
    for (std::size_t index = 0; index < safety.excludedSizeRanges.size(); ++index) {
        const SizeExclusionRange& range = safety.excludedSizeRanges[index];
        text << L"safety.sizeRange." << index << L".hasMinimum=" << BoolText(range.hasMinimum) << L"\n";
        text << L"safety.sizeRange." << index << L".hasMaximum=" << BoolText(range.hasMaximum) << L"\n";
        text << L"safety.sizeRange." << index << L".minimumBytes=" << range.minimumBytes.getValue() << L"\n";
        text << L"safety.sizeRange." << index << L".maximumBytes=" << range.maximumBytes.getValue() << L"\n";
    }

    text << L"profileCount=" << settings.profiles.size() << L"\n";
    for (std::size_t index = 0; index < settings.profiles.size(); ++index) {
        SerializeProfile(text, Prefix(L"profile", static_cast<std::uint64_t>(index)), settings.profiles[index]);
    }

    text << L"recentCount=" << settings.recentAnalysisSummaries.size() << L"\n";
    for (std::size_t index = 0; index < settings.recentAnalysisSummaries.size(); ++index) {
        const RecentAnalysisSummary& summary = settings.recentAnalysisSummaries[index];
        const std::wstring prefix = Prefix(L"recent", static_cast<std::uint64_t>(index));
        text << prefix << L"driveRoot=" << summary.driveRoot << L"\n";
        text << prefix << L"displayName=" << summary.displayName << L"\n";
        text << prefix << L"fileSystem=" << summary.fileSystem << L"\n";
        text << prefix << L"analyzedAt=" << summary.analyzedAt << L"\n";
        text << prefix << L"totalBytes=" << summary.totalBytes.getValue() << L"\n";
        text << prefix << L"freeBytes=" << summary.freeBytes.getValue() << L"\n";
        text << prefix << L"scannedFiles=" << summary.scannedFiles.getValue() << L"\n";
        text << prefix << L"skippedFiles=" << summary.skippedFiles.getValue() << L"\n";
        text << prefix << L"inaccessibleFiles=" << summary.inaccessibleFiles.getValue() << L"\n";
        text << prefix << L"fragmentedFiles=" << summary.fragmentedFiles.getValue() << L"\n";
        text << prefix << L"freeSpaceBlocks=" << summary.freeSpaceBlocks.getValue() << L"\n";
        text << prefix << L"freeSpaceMapAvailable=" << BoolText(summary.freeSpaceMapAvailable) << L"\n";
    }
    return text.str();
}

std::optional<AppSettings> AppSettingsSerializer::Deserialize(const std::wstring& text) const
{
    const auto values = ParseValues(text);
    if (values.empty() || ParseU64(values, L"version", 0) != 1) {
        return std::nullopt;
    }

    AppSettings settings;
    settings.activeProfileMode = ParseMode(ParseU64(values, L"activeProfileMode", 0));

    SafetySettings& safety = settings.safetySettings;
    safety.defaultDryRunOnly = ParseStoredBool(values, L"safety.defaultDryRunOnly", safety.defaultDryRunOnly);
    safety.excludeProtectedSystemPaths =
        ParseStoredBool(values, L"safety.excludeProtectedSystemPaths", safety.excludeProtectedSystemPaths);
    safety.globalMaximumBytesToMove =
        byte_count64_t(ParseU64(values, L"safety.globalMaximumBytesToMove", safety.globalMaximumBytesToMove.getValue()));

    const std::uint64_t directoryCount = ParseU64(values, L"safety.directoryCount", 0);
    for (std::uint64_t index = 0; index < directoryCount; ++index) {
        if (const auto found = values.find(L"safety.directory." + std::to_wstring(index)); found != values.end()) {
            safety.excludedDirectories.emplace_back(found->second);
        }
    }

    const std::uint64_t extensionCount = ParseU64(values, L"safety.extensionCount", 0);
    for (std::uint64_t index = 0; index < extensionCount; ++index) {
        if (const auto found = values.find(L"safety.extension." + std::to_wstring(index)); found != values.end()) {
            safety.excludedExtensions.push_back(found->second);
        }
    }

    const std::uint64_t rangeCount = ParseU64(values, L"safety.sizeRangeCount", 0);
    for (std::uint64_t index = 0; index < rangeCount; ++index) {
        const std::wstring prefix = L"safety.sizeRange." + std::to_wstring(index) + L".";
        SizeExclusionRange range;
        range.hasMinimum = ParseStoredBool(values, prefix + L"hasMinimum", false);
        range.hasMaximum = ParseStoredBool(values, prefix + L"hasMaximum", false);
        range.minimumBytes = byte_count64_t(ParseU64(values, prefix + L"minimumBytes", 0));
        range.maximumBytes = byte_count64_t(ParseU64(values, prefix + L"maximumBytes", 0));
        if (range.hasMinimum || range.hasMaximum) {
            safety.excludedSizeRanges.push_back(range);
        }
    }

    const std::uint64_t profileCount = ParseU64(values, L"profileCount", 0);
    for (std::uint64_t index = 0; index < profileCount; ++index) {
        settings.profiles.push_back(DeserializeProfile(values, Prefix(L"profile", index)));
    }

    const std::uint64_t recentCount = ParseU64(values, L"recentCount", 0);
    for (std::uint64_t index = 0; index < recentCount; ++index) {
        const std::wstring prefix = Prefix(L"recent", index);
        RecentAnalysisSummary summary;
        if (const auto found = values.find(prefix + L"driveRoot"); found != values.end()) {
            summary.driveRoot = found->second;
        }
        if (summary.driveRoot.empty()) {
            continue;
        }
        if (const auto found = values.find(prefix + L"displayName"); found != values.end()) {
            summary.displayName = found->second;
        }
        if (const auto found = values.find(prefix + L"fileSystem"); found != values.end()) {
            summary.fileSystem = found->second;
        }
        if (const auto found = values.find(prefix + L"analyzedAt"); found != values.end()) {
            summary.analyzedAt = found->second;
        }
        summary.totalBytes = byte_count64_t(ParseU64(values, prefix + L"totalBytes", 0));
        summary.freeBytes = byte_count64_t(ParseU64(values, prefix + L"freeBytes", 0));
        summary.scannedFiles = count64_t(ParseU64(values, prefix + L"scannedFiles", 0));
        summary.skippedFiles = count64_t(ParseU64(values, prefix + L"skippedFiles", 0));
        summary.inaccessibleFiles = count64_t(ParseU64(values, prefix + L"inaccessibleFiles", 0));
        summary.fragmentedFiles = count64_t(ParseU64(values, prefix + L"fragmentedFiles", 0));
        summary.freeSpaceBlocks = count64_t(ParseU64(values, prefix + L"freeSpaceBlocks", 0));
        summary.freeSpaceMapAvailable = ParseStoredBool(values, prefix + L"freeSpaceMapAvailable", false);
        settings.recentAnalysisSummaries.push_back(std::move(summary));
    }

    return settings;
}

} // namespace icd
