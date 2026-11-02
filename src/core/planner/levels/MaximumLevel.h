#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/planner/LevelPlanner.h"

namespace icd::core::planner {

    // The Maximum level's policy (plan.md 9.2): the complete target layout, every movable file in one fragment, exact
    // order in every sorted zone, small files evicted for large ones, cycles parked in scratch space, no budget.
    [[nodiscard]] LevelPolicy maximumPolicy(const PlanInput& input);

    // Plans the Maximum level.
    [[nodiscard]] PlanResult planMaximum(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                                         const CancelToken& token = {});

} // namespace icd::core::planner
