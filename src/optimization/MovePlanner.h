#pragma once

#include "../model/DomainTypes.h"

#include <functional>

namespace icd {

// Converts placement intent into a conservative dry-run move plan without disk writes.
class MovePlanner {
public:
    MovePlan Build(const AnalysisResult& analysis,
                   const PlacementPlan& placementPlan,
                   const OptimizationProfile& profile,
                   const std::function<bool(double, const std::wstring&)>& progressCallback = {}) const;
};

} // namespace icd
