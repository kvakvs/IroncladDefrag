#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/planner/LevelPlanner.h"

namespace icd::core::planner {

    // Share of a sorted zone's in-place bytes Moderate may move to sort it: only nearly sorted zones are sorted.
    inline constexpr double moderateSortTolerance = 0.2;

    // Share of used space Moderate moves at most (plan.md 9.2).
    inline constexpr double moderateBudgetShare = 0.2;

    // The Moderate level's policy (plan.md 9.2): every zone with the zones' fragment_ok, nearly sorted zones sorted,
    // misplaced files ranked by benefit per byte, small files evicted for large ones, cycles dropped (no parking),
    // budget about 20% of used space.
    [[nodiscard]] LevelPolicy moderatePolicy(const PlanInput& input);

    // Plans the Moderate level.
    [[nodiscard]] PlanResult planModerate(const PlanInput& input, const ZoneAssignment& assignment, Executor& executor,
                                          const CancelToken& token = {});

} // namespace icd::core::planner
