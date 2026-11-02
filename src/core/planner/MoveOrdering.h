#pragma once

#include "core/planner/HoleSet.h"
#include "core/planner/Placement.h"
#include "core/planner/Plan.h"
#include "core/snapshot/Snapshot.h"

#include <span>
#include <vector>

namespace icd::core::planner {

    // How cycles are handled (plan.md 9.3.7).
    struct OrderOptions {
        bool allowParking = false; // Maximum: break a cycle by parking its smallest file; otherwise drop the cycle.
        // Free runs nothing lands on, for parking; empty: the snapshot's free runs minus every destination.
        std::vector<ClusterRun> scratch{};
    };

    // What ordering did beyond emitting moves.
    struct OrderReport {
        std::size_t cycles = 0; // Cycles found.
        std::size_t parked = 0; // Files parked to break them.
        std::size_t redirected = 0; // Files sent to free space instead of their target to break them (no parking).
        std::size_t dropped = 0; // Moves dropped (cycles at levels that may not park, and what waited on them).
    };

    // Orders destinations into moves (plan.md 9.3.7): a move depends on every move whose sources its destination
    // overlaps, the graph is ordered topologically (ties in destination order, so the result is deterministic), and
    // each file moves exactly once. When only cycles are left, every cycle that waits on nothing else is broken: at
    // Maximum its smallest file is parked in scratch space first (one extra move, marked extra) and returns later;
    // otherwise its smallest file goes to scratch space for good (one move, outside its target). Without scratch space
    // the cycle is dropped together with every move that waited on it (skipped, Cycle or Dependency).
    // Moves are appended to the plan; skipped files are recorded in it.
    OrderReport orderMoves(const Snapshot& snapshot, std::span<const Destination> destinations,
                           const OrderOptions& options, Plan& plan);

} // namespace icd::core::planner
