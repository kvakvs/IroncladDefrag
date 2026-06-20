#pragma once

#include "../model/DomainTypes.h"

#include <optional>
#include <string>

namespace icd {

// Converts optimization profiles to and from deterministic text for future persistence.
class OptimizationSettingsSerializer {
public:
    std::wstring Serialize(const OptimizationProfile& profile) const;
    std::optional<OptimizationProfile> Deserialize(const std::wstring& text) const;
};

} // namespace icd
