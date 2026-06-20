#include "../precompiled.h"
#include "OptimizationSettingsSerializer.h"

#include <cwctype>
#include <sstream>
#include <unordered_map>

namespace icd {

namespace {
constexpr std::uint64_t HoursPerDay = 24;

std::wstring BoolText(bool value)
{
    return value ? L"true" : L"false";
}

bool ParseBool(const std::wstring& value)
{
    return value == L"true" || value == L"1";
}

std::uint64_t DaysFromHours(std::chrono::hours value)
{
    return static_cast<std::uint64_t>(value.count() / HoursPerDay);
}

std::chrono::hours HoursFromDays(std::uint64_t days)
{
    return std::chrono::hours(static_cast<std::chrono::hours::rep>(days * HoursPerDay));
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
} // namespace

std::wstring OptimizationSettingsSerializer::Serialize(const OptimizationProfile& profile) const
{
    const OptimizationSettings& settings = profile.settings;
    std::wstringstream text;
    text << L"version=1\n";
    text << L"mode=" << static_cast<int>(profile.mode) << L"\n";
    text << L"name=" << profile.name << L"\n";
    text << L"enableFastZone=" << BoolText(settings.enableFastZone) << L"\n";
    text << L"enableBalancedZone=" << BoolText(settings.enableBalancedZone) << L"\n";
    text << L"enableSlowZone=" << BoolText(settings.enableSlowZone) << L"\n";
    text << L"enableLargeFileZone=" << BoolText(settings.enableLargeFileZone) << L"\n";
    text << L"enableFreeSpaceReserve=" << BoolText(settings.enableFreeSpaceReserve) << L"\n";
    text << L"smallFileThreshold=" << settings.smallFileThreshold.getValue() << L"\n";
    text << L"largeFileThreshold=" << settings.largeFileThreshold.getValue() << L"\n";
    text << L"hugeFileThreshold=" << settings.hugeFileThreshold.getValue() << L"\n";
    text << L"maximumBytesToMove=" << settings.maximumBytesToMove.getValue() << L"\n";
    text << L"minimumBenefitScore=" << settings.minimumBenefitScore << L"\n";
    text << L"preserveDirectoryLocality=" << BoolText(settings.preserveDirectoryLocality) << L"\n";
    text << L"allowLargeFileMoves=" << BoolText(settings.allowLargeFileMoves) << L"\n";
    text << L"prioritizeFreeSpaceConsolidation=" << BoolText(settings.prioritizeFreeSpaceConsolidation) << L"\n";
    text << L"dryRunOnly=" << BoolText(settings.dryRunOnly) << L"\n";
    text << L"hotDays=" << DaysFromHours(settings.hotRecency) << L"\n";
    text << L"warmDays=" << DaysFromHours(settings.warmRecency) << L"\n";
    text << L"coolDays=" << DaysFromHours(settings.coolRecency) << L"\n";
    text << L"coldDays=" << DaysFromHours(settings.coldRecency) << L"\n";
    text << L"staleDays=" << DaysFromHours(settings.staleRecency) << L"\n";
    text << L"zoneCount=" << settings.zoneBoundaries.size() << L"\n";
    for (std::size_t index = 0; index < settings.zoneBoundaries.size(); ++index) {
        const ZoneBoundarySetting& zone = settings.zoneBoundaries[index];
        text << L"zone." << index << L".role=" << static_cast<int>(zone.role) << L"\n";
        text << L"zone." << index << L".mode=" << static_cast<int>(zone.mode) << L"\n";
        text << L"zone." << index << L".startPercent=" << zone.startPercent << L"\n";
        text << L"zone." << index << L".sizePercent=" << zone.sizePercent << L"\n";
        text << L"zone." << index << L".startBytes=" << zone.startBytes.getValue() << L"\n";
        text << L"zone." << index << L".sizeBytes=" << zone.sizeBytes.getValue() << L"\n";
        text << L"zone." << index << L".enabled=" << BoolText(zone.enabled) << L"\n";
    }
    return text.str();
}

std::optional<OptimizationProfile> OptimizationSettingsSerializer::Deserialize(const std::wstring& text) const
{
    std::unordered_map<std::wstring, std::wstring> values;
    std::wstringstream input(text);
    std::wstring line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find(L'=');
        if (separator == std::wstring::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }

    if (values.empty()) {
        return std::nullopt;
    }

    OptimizationProfile profile;
    profile.mode = ParseMode(ParseU64(values, L"mode", 0));
    if (const auto found = values.find(L"name"); found != values.end()) {
        profile.name = found->second;
    }

    OptimizationSettings& settings = profile.settings;
    settings.enableFastZone = ParseStoredBool(values, L"enableFastZone", settings.enableFastZone);
    settings.enableBalancedZone = ParseStoredBool(values, L"enableBalancedZone", settings.enableBalancedZone);
    settings.enableSlowZone = ParseStoredBool(values, L"enableSlowZone", settings.enableSlowZone);
    settings.enableLargeFileZone = ParseStoredBool(values, L"enableLargeFileZone", settings.enableLargeFileZone);
    settings.enableFreeSpaceReserve = ParseStoredBool(values, L"enableFreeSpaceReserve", settings.enableFreeSpaceReserve);
    settings.smallFileThreshold = byte_count64_t(ParseU64(values, L"smallFileThreshold", settings.smallFileThreshold.getValue()));
    settings.largeFileThreshold = byte_count64_t(ParseU64(values, L"largeFileThreshold", settings.largeFileThreshold.getValue()));
    settings.hugeFileThreshold = byte_count64_t(ParseU64(values, L"hugeFileThreshold", settings.hugeFileThreshold.getValue()));
    settings.maximumBytesToMove = byte_count64_t(ParseU64(values, L"maximumBytesToMove", settings.maximumBytesToMove.getValue()));
    settings.minimumBenefitScore = ParseDouble(values, L"minimumBenefitScore", settings.minimumBenefitScore);
    settings.preserveDirectoryLocality = ParseStoredBool(values, L"preserveDirectoryLocality", settings.preserveDirectoryLocality);
    settings.allowLargeFileMoves = ParseStoredBool(values, L"allowLargeFileMoves", settings.allowLargeFileMoves);
    settings.prioritizeFreeSpaceConsolidation =
        ParseStoredBool(values, L"prioritizeFreeSpaceConsolidation", settings.prioritizeFreeSpaceConsolidation);
    settings.dryRunOnly = ParseStoredBool(values, L"dryRunOnly", settings.dryRunOnly);
    settings.hotRecency = HoursFromDays(ParseU64(values, L"hotDays", DaysFromHours(settings.hotRecency)));
    settings.warmRecency = HoursFromDays(ParseU64(values, L"warmDays", DaysFromHours(settings.warmRecency)));
    settings.coolRecency = HoursFromDays(ParseU64(values, L"coolDays", DaysFromHours(settings.coolRecency)));
    settings.coldRecency = HoursFromDays(ParseU64(values, L"coldDays", DaysFromHours(settings.coldRecency)));
    settings.staleRecency = HoursFromDays(ParseU64(values, L"staleDays", DaysFromHours(settings.staleRecency)));

    const std::uint64_t zoneCount = ParseU64(values, L"zoneCount", 0);
    settings.zoneBoundaries.clear();
    for (std::uint64_t index = 0; index < zoneCount; ++index) {
        const std::wstring prefix = L"zone." + std::to_wstring(index) + L".";
        ZoneBoundarySetting zone;
        zone.role = static_cast<DiskZoneRole>(ParseU64(values, prefix + L"role", static_cast<std::uint64_t>(zone.role)));
        zone.mode =
            static_cast<ZoneBoundaryMode>(ParseU64(values, prefix + L"mode", static_cast<std::uint64_t>(zone.mode)));
        zone.startPercent = ParseDouble(values, prefix + L"startPercent", zone.startPercent);
        zone.sizePercent = ParseDouble(values, prefix + L"sizePercent", zone.sizePercent);
        zone.startBytes = byte_count64_t(ParseU64(values, prefix + L"startBytes", zone.startBytes.getValue()));
        zone.sizeBytes = byte_count64_t(ParseU64(values, prefix + L"sizeBytes", zone.sizeBytes.getValue()));
        zone.enabled = ParseStoredBool(values, prefix + L"enabled", zone.enabled);
        settings.zoneBoundaries.push_back(zone);
    }

    return profile;
}

} // namespace icd
