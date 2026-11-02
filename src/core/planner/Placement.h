#pragma once

#include "core/planner/HoleSet.h"
#include "core/planner/Plan.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <optional>
#include <span>
#include <vector>

namespace icd::core::planner {

    // What a file needs from free space.
    struct SpaceRequest {
        ClusterCount clusters = 0; // Clusters to place.
        std::span<const ClusterRun>
            own{}; // The file's current runs, sorted by LCN: a destination may not overlap them.
        ClusterRun window{}; // Where to look.
    };

    // The lowest free run of `clusters` contiguous clusters inside the window that avoids the file's own runs.
    [[nodiscard]] std::optional<ClusterRun> findFirst(const HoleSet& pool, const SpaceRequest& request);

    // The highest such run (packed against the window's end).
    [[nodiscard]] std::optional<ClusterRun> findLast(const HoleSet& pool, const SpaceRequest& request);

    // The such run closest to `near`: one covering near when the hole there is big enough, else the first one starting
    // after it or the last one ending before it, whichever is closer (ties go forward).
    [[nodiscard]] std::optional<ClusterRun> findNear(const HoleSet& pool, const SpaceRequest& request, Lcn near);

    // Several pieces of at least `minPiece` clusters each (the last one too), in LCN order from the window's start,
    // for files whose fragments may be large instead of one (plan.md 3, fact 2). nullopt when the free space has no
    // such pieces.
    [[nodiscard]] std::optional<std::vector<ClusterRun>> findPieces(const HoleSet& pool, const SpaceRequest& request,
                                                                    ClusterCount minPiece);

    // Takes runs out of the pool; they must be free.
    void takeRuns(HoleSet& pool, std::span<const ClusterRun> runs);

    // Returns runs to the pool (the clusters a move vacates).
    void releaseRuns(HoleSet& pool, std::span<const ClusterRun> runs);

    // Where one file goes, before moves are ordered (plan.md 6.4 step 5 assigns these, then orders them).
    struct Destination {
        FileIndex file = noFile; // The file.
        std::vector<ClusterRun> runs{}; // Its new runs, in VCN order.
        MoveReason reason = MoveReason::Relocate; // Why it moves.
        ZoneIndex zone = noZone; // The zone it lands in, or noZone.
    };

    // Appends one move per destination in the given order (the caller has ordered them, or they do not depend on
    // each other).
    void appendMoves(const Snapshot& snapshot, std::span<const Destination> destinations, Plan& plan);

    // Runs sorted by LCN, for SpaceRequest::own.
    [[nodiscard]] std::vector<ClusterRun> sortedRuns(std::span<const ClusterRun> runs);

} // namespace icd::core::planner
