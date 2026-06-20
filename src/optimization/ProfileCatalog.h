#pragma once

#include "../model/DomainTypes.h"

#include <vector>

namespace icd {

// Provides the built-in optimization profiles available before persistence exists.
class ProfileCatalog {
public:
    std::vector<OptimizationProfile> CreateDefaultProfiles() const;
};

} // namespace icd
