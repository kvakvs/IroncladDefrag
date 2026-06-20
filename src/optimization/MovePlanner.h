#pragma once

#include "../model/DomainTypes.h"

namespace icd {

// Converts placement intent into a conservative dry-run move plan without disk writes.
class MovePlanner {
public:
    MovePlan Build(const AnalysisResult& analysis,
                   const PlacementPlan& placementPlan,
                   const OptimizationProfile& profile) const;
};

} // namespace icd
