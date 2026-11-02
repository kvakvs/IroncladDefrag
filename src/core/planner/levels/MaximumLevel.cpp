#include "core/planner/levels/MaximumLevel.h"

namespace icd::core::planner {

    LevelPolicy maximumPolicy(const PlanInput& input) {
        return {.effort = strategy::Effort::Maximum,
                .goodEnough = {.wholeFiles = true},
                .sortTolerance = 1.0,
                .allowParking = true,
                .makeRoom = true,
                .budget = {.maxClusters = std::nullopt, .maxSeconds = std::nullopt, .time = input.time}};
    }

    PlanResult planMaximum(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                           const CancelToken& token) {
        return planZones(input, assignment, maximumPolicy(input), executor, token);
    }

} // namespace icd::core::planner
