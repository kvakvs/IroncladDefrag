#pragma once

#include "core/planner/Plan.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <optional>

namespace icd::core::planner {

    // How long moves take, for budgets and the Plan tab (plan.md 9.2.2: 2 x bytes / throughput + moves x seek cost).
    struct TimeModel {
        double bytesPerSecond = 150.0 * 1024 * 1024; // Sequential throughput of the drive (read and write once each).
        double seekSeconds = 0.015; // Average cost of one move's head travel and call overhead.

        // Seconds one move of `clusters` clusters takes.
        [[nodiscard]] double moveSeconds(ClusterCount clusters, std::uint32_t bytesPerCluster) const;
    };

    // A level's budget (plan.md 9.2): whichever limit is hit first.
    struct BudgetOptions {
        std::optional<ClusterCount> maxClusters{}; // Most clusters moved.
        std::optional<double> maxSeconds{}; // Most estimated time.
        TimeModel time{}; // The time estimate.
    };

    // What a move is worth (plan.md 9.3.8): fragments it removes, a file brought into its zone, a hole it closes.
    // Moves marked extra are worth nothing on their own; they come along with the moves that need them.
    [[nodiscard]] double moveBenefit(const Snapshot& snapshot, const Plan& plan, const Move& move);

    // Estimated seconds a whole plan takes.
    [[nodiscard]] double planSeconds(const Plan& plan, std::uint32_t bytesPerCluster, const TimeModel& time);

    // Cuts a plan to its budget (plan.md 9.3.8, 9.2.1 step 5): moves are taken by benefit per byte, each with every
    // earlier move it depends on (so the kept set stays valid), while the budget lasts; the kept moves are then
    // ordered with the most valuable ready move first, so stopping after any move leaves the volume better than before.
    // Moves left out count as deferred (extra ones as nothing). Fills Move::benefit. Plans with parking are left whole
    // (only Maximum parks, and it has no budget).
    void applyBudget(const Snapshot& snapshot, Plan& plan, const BudgetOptions& budget);

} // namespace icd::core::planner
