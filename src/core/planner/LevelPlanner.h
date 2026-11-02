#pragma once

#include "core/classify/Catalog.h"
#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/planner/Budget.h"
#include "core/planner/LowerBound.h"
#include "core/planner/Plan.h"
#include "core/planner/TargetLayout.h"
#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace icd::core::planner {

    // Everything a plan is made from besides the effort level.
    struct PlanInput {
        const Snapshot* snapshot = nullptr; // The volume.
        const strategy::ZoneScript* script = nullptr; // The strategy.
        const classify::Catalog* catalog = nullptr; // Classes the script names.
        std::span<const classify::ClassId> classes{}; // Per file.
        std::span<const std::uint8_t> churn{}; // Per file; empty when unknown.
        std::int64_t now = 0; // FILETIME ages are measured from (the scan time).
        std::vector<ClusterRun> reserved{}; // The MFT zone, kept free when possible.
        bool shadowCopies = false; // The volume has shadow copies: prefer aligned destinations (plan.md 9.4).
        double fastBudgetMultiplier = 1.0; // Settings: x1, x2, x4, x10 (plan.md 9.2.1 step 7).
        TimeModel time{}; // Time estimates.
    };

    // How one effort level plans (plan.md 9.2).
    struct LevelPolicy {
        strategy::Effort effort = strategy::Effort::Maximum; // The level.
        GoodEnough goodEnough{}; // Strictness of "good enough".
        double sortTolerance = 1.0; // Share of a sorted zone's in-place bytes that may move to sort it.
        bool allowParking = false; // Break cycles by parking (Maximum only).
        bool makeRoom = false; // Evict small files for large ones that fit nowhere (plan.md 9.3.6).
        BudgetOptions budget{}; // Limits.
    };

    // Figures before and after a plan (Plan tab, plan.md 9.2.2).
    struct PlanMetrics {
        double fragmentedBefore = 0.0; // Percent of files with clusters in more than one fragment, before.
        double fragmentedAfter = 0.0; // The same after the plan.
        double zoneFitBefore = 0.0; // Percent of placed files' clusters inside their zone, before.
        double zoneFitAfter = 0.0; // The same after the plan.
        std::uint64_t freeRunsBefore = 0; // Separate free runs before.
        std::uint64_t freeRunsAfter = 0; // After.
        ClusterCount largestFreeAfter = 0; // Longest free run after.
        double seconds = 0.0; // Estimated execution time.
    };

    // A planned level.
    struct PlanResult {
        Plan plan{}; // The moves.
        TargetLayout layout{}; // The target it aims for.
        PlanMetrics metrics{}; // Figures.
        std::vector<ClusterRun> hotSpots{}; // Fast only: the areas it works on.
    };

    // Zones and roles of the input's files; shared by all levels of one snapshot and script.
    [[nodiscard]] ZoneAssignment assignInput(const PlanInput& input, Executor& executor, const CancelToken& token = {});

    // Plans the zone layout at Moderate or Maximum (plan.md 6.4 steps 1-5): fitted target, lower bound with sorting,
    // candidates, destinations per zone (sort, pack-to-back, fast-fill), defragment-only files near where they are,
    // overflow next to its zone (or with small files evicted), VSS alignment, ordering, budget, metrics.
    [[nodiscard]] PlanResult planZones(const PlanInput& input, const ZoneAssignment& assignment,
                                       const LevelPolicy& policy, Executor& executor, const CancelToken& token = {});

    // Figures of a plan against its layout.
    [[nodiscard]] PlanMetrics measurePlan(const PlanInput& input, const ZoneAssignment& assignment,
                                          const TargetLayout& layout, const Plan& plan);

    // Used clusters of a volume (budgets are shares of used space, plan.md 9.2).
    [[nodiscard]] ClusterCount usedClusters(const Snapshot& snapshot);

} // namespace icd::core::planner
