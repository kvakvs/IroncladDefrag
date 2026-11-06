#pragma once

#include "core/exec/Executor.h"
#include "core/execute/ExecutionPlan.h"

#include <cstdint>

namespace icd::core::execute {

    // What ordering by head position changed, for the log.
    struct HeadOrderReport {
        std::uint64_t travelBefore = 0; // Clusters the head travels between moves in plan order.
        std::uint64_t travelAfter = 0; // The same in the new order.
    };

    // The clusters a hard drive's head travels between moves: from where each move's last write ends to where the
    // next move's first read starts (the travel inside a move is the same in any order). Starts at cluster 0.
    [[nodiscard]] std::uint64_t headTravel(const ExecutionPlan& plan);

    // Reorders the moves so the head travels less between them (T133): each next move is the one whose first read is
    // nearest where the last write ended, among the moves free to go. Two moves whose runs overlap (sources or
    // destinations), or that move the same file, keep their plan order, so every move finds its clusters as planned and
    // the volume ends the same as in plan order (and any prefix stays a valid plan). On equal distance the move ahead
    // of the head goes first, then the earlier one in plan order.
    [[nodiscard]] ExecutionPlan orderForHead(const ExecutionPlan& plan, HeadOrderReport* report = nullptr,
                                             Executor* executor = nullptr);

} // namespace icd::core::execute
