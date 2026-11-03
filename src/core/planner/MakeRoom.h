#pragma once

#include "core/exec/Executor.h"
#include "core/planner/HoleSet.h"
#include "core/snapshot/Snapshot.h"

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::planner {

    // What a file that no free run holds needs, and how much may be moved out of its way.
    struct RoomRequest {
        ClusterCount clusters = 0; // Contiguous clusters the file needs.
        std::span<const ClusterRun> own{}; // The file's runs by LCN: never part of the window.
        std::span<const ClusterRun> region{}; // Where the window must lie (the zone's pieces).
        ClusterCount maxEvict = 0; // Most clusters of other files that may move for it.
    };

    // A window for a large file and the small files that have to move out of it first.
    struct Room {
        ClusterRun window{}; // Where the large file goes.
        std::vector<FileIndex> blockers{}; // Files with clusters in the window, by index.
        ClusterCount evicted = 0; // Their movable clusters (they move whole).
    };

    // "Move the smaller thing" (plan.md 9.3.6): a window of `clusters` inside the region whose clusters are all free
    // after the plan (in `pool`) or belong to files `mayEvict` accepts, choosing the window that evicts the fewest
    // clusters (and the lowest one among equals). Windows start where a free hole starts or end where one ends; at most
    // `maxStarts` starts are tried in a piece, and fewer in each further piece, which bounds the work on huge zones.
    // `region` should hold only pieces long enough for the file, the most promising first. The windows of a piece are
    // costed on `executor` (null: inline; mayEvict must then be safe to call from several threads), with the same
    // result.
    [[nodiscard]] std::optional<Room> findRoom(const Snapshot& snapshot, const HoleSet& pool,
                                               const RoomRequest& request,
                                               const std::function<bool(FileIndex)>& mayEvict,
                                               std::size_t maxStarts = 256, Executor* executor = nullptr);

} // namespace icd::core::planner
