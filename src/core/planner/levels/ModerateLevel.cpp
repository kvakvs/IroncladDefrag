#include "core/planner/levels/ModerateLevel.h"

#include <cmath>

namespace icd::core::planner {

    LevelPolicy moderatePolicy(const PlanInput& input) {
        const auto used = static_cast<double>(usedClusters(*input.snapshot));
        return {.effort = strategy::Effort::Moderate,
                .goodEnough = {.wholeFiles = false},
                .sortTolerance = moderateSortTolerance,
                .allowParking = false,
                .makeRoom = true,
                .budget = {.maxClusters = static_cast<ClusterCount>(std::ceil(moderateBudgetShare * used)),
                           .maxSeconds = std::nullopt,
                           .time = input.time}};
    }

    PlanResult planModerate(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                            const CancelToken& token) {
        return planZones(input, assignment, moderatePolicy(input), executor, token);
    }

} // namespace icd::core::planner
