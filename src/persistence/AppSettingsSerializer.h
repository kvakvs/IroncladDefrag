#pragma once

#include "../model/DomainTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace icd {

// Stores persisted application settings without executable analysis snapshots.
struct AppSettings {
    std::vector<OptimizationProfile> profiles;
    OptimizationMode activeProfileMode = OptimizationMode::BalancedDataDrive;
    SafetySettings safetySettings;
    std::vector<RecentAnalysisSummary> recentAnalysisSummaries;
};

// Converts app settings to and from a deterministic line-oriented format.
class AppSettingsSerializer {
public:
    std::wstring Serialize(const AppSettings& settings) const;
    std::optional<AppSettings> Deserialize(const std::wstring& text) const;
};

} // namespace icd
