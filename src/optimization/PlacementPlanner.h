#pragma once

#include "../model/DomainTypes.h"

namespace icd {

// Defines the read-only contract for producing target placement intent.
class IPlacementStrategy {
public:
    virtual ~IPlacementStrategy() = default;
    virtual PlacementPlan Build(const AnalysisResult& analysis, const OptimizationProfile& profile) const = 0;
};

// Selects a dry-run placement strategy and produces target intent without move operations.
class PlacementPlanner {
public:
    PlacementPlan Build(const AnalysisResult& analysis, const OptimizationProfile& profile) const;
};

} // namespace icd
