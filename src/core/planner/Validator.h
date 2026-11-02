#pragma once

#include "core/planner/Plan.h"
#include "core/planner/TargetLayout.h"
#include "core/snapshot/Snapshot.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace icd::core::planner {

    // One broken rule of a plan.
    struct ValidationIssue {
        std::optional<std::size_t> move{}; // Index of the move, or nullopt for the plan as a whole.
        std::string message{}; // What is wrong.
    };

    // What the validator checks beyond the simulation itself.
    struct ValidateOptions {
        const TargetLayout* layout = nullptr; // When set, every move into a zone must land inside that zone.
        std::size_t maxIssues = 100; // Stop collecting after this many, so a badly broken plan stays readable.
    };

    // The outcome of replaying a plan.
    struct Validation {
        std::vector<ValidationIssue> issues{}; // Broken rules; empty when the plan is valid.
        PlanTotals totals{}; // Figures recomputed from the replay.
        ClusterCount freeClusters = 0; // Free space after the plan.
        std::size_t freeRuns = 0; // Free runs after the plan.

        // Whether the plan passed.
        [[nodiscard]] bool ok() const { return issues.empty(); }
    };

    // Replays a plan on copies of the snapshot's free runs and extents (plan.md 9.5) and checks: every destination is
    // free at the moment of its move; no unmovable file or stream is touched; each move takes exactly the file's
    // current movable clusters in VCN order and keeps their count; moves into a zone land in it (with a layout); each
    // file moves at most once, except a parked file which returns exactly once; the moved clusters equal the lower
    // bound minus skipped and deferred clusters plus the clusters of moves marked extra; and the plan's totals equal
    // the recomputed ones.
    [[nodiscard]] Validation validatePlan(const Snapshot& snapshot, const Plan& plan,
                                          const ValidateOptions& options = {});

} // namespace icd::core::planner
