#pragma once

#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace icd::core::planner {

    // What a move is for (plan reports, benefit ranking, the plan preview).
    enum class MoveReason : std::uint8_t {
        Defragment, // Joins a file's fragments (near where it is, or in its zone).
        Relocate, // Brings a file into its target zone.
        Compact, // Fills a hole inside a zone or hot spot so the zone gets shorter.
        Sort, // Puts a file into its zone's order.
        Park, // Moves a file out of the way to break a dependency cycle (plan.md 9.3.7); it moves again later.
        Evict, // Moves a small file out of a large file's way ("move the smaller thing", plan.md 9.3.6).
    };

    // Stable report name of a reason: "defragment", "relocate", ...
    [[nodiscard]] std::string_view reasonKey(MoveReason reason);

    // One move: all movable clusters of one file, from where they are to new runs. The clusters keep their order: the
    // sources, read in VCN order, are written into the destinations in order.
    struct Move {
        FileIndex file = noFile; // The file.
        MoveReason reason = MoveReason::Relocate; // Why.
        ZoneIndex zone = noZone; // The zone the destination belongs to; noZone for in-place and scratch space.
        bool fromParking = false; // The second move of a parked file: its sources are the scratch runs.
        bool extra = false; // Not needed by the lower bound (parking, evictions, sorted-zone anchors given up).
        std::uint32_t firstSource = 0; // First source run in Plan::runs.
        std::uint32_t sourceCount = 0; // Source runs.
        std::uint32_t firstDestination = 0; // First destination run in Plan::runs.
        std::uint32_t destinationCount = 0; // Destination runs.
        ClusterCount clusters = 0; // Clusters moved.
        double benefit = 0.0; // Value of the move for ranking under a budget (plan.md 9.3.8).
    };

    // Why a file that had to move does not.
    enum class SkipReason : std::uint8_t {
        NoSpace, // No free space of the shape the file needs.
        Cycle, // Part of a dependency cycle at a level that may not park (plan.md 9.3.7).
        Dependency, // Its destination is held by a file that does not move.
        Unaligned, // Only unaligned destinations exist and unaligned moves are blocked (VSS, plan.md 9.4).
    };

    // Stable report name of a skip reason.
    [[nodiscard]] std::string_view skipKey(SkipReason reason);

    // A file the target needs moved that the plan leaves where it is.
    struct SkippedFile {
        FileIndex file = noFile; // The file.
        SkipReason reason = SkipReason::NoSpace; // Why.
        ClusterCount clusters = 0; // Its movable clusters.
    };

    // Figures of a plan (Plan tab, reports); the validator recomputes and compares them (plan.md 9.5).
    struct PlanTotals {
        ClusterCount clustersMoved = 0; // Every move's clusters, parking included.
        std::uint64_t moves = 0; // Moves.
        std::uint64_t files = 0; // Distinct files moved.
        std::uint64_t fragmentsBefore = 0; // Fragments of the moved files before.
        std::uint64_t fragmentsAfter = 0; // Their fragments after the plan.

        // Totals compare as values.
        friend bool operator==(const PlanTotals&, const PlanTotals&) = default;
    };

    // A plan: ordered moves that are valid one after another from the snapshot they were made for, plus its accounting
    // against the lower bound (plan.md 9.3.1). Plain data, so it can be shown, saved and replayed.
    struct Plan {
        strategy::Effort effort = strategy::Effort::Fast; // The level it was made for.
        std::vector<Move> moves{}; // In execution order.
        std::vector<ClusterRun> runs{}; // Source and destination runs of all moves.
        ClusterCount lowerBound = 0; // Clusters any plan must move to reach this level's target.
        ClusterCount extra = 0; // Clusters of moves marked extra: parking, evictions, anchors a sorted zone gave up.
        ClusterCount skippedClusters = 0; // Clusters of skipped files.
        ClusterCount deferredClusters = 0; // Clusters left for a later run by the budget (plan.md 9.2.1).
        std::vector<SkippedFile> skipped{}; // Files that had to move but do not.
        std::vector<std::string> notes{}; // Goals not reached and other remarks for the Plan tab.
        PlanTotals totals{}; // Figures, filled by finishTotals().

        // A move's source runs.
        [[nodiscard]] std::span<const ClusterRun> sources(const Move& move) const;

        // A move's destination runs.
        [[nodiscard]] std::span<const ClusterRun> destinations(const Move& move) const;

        // Appends a move with its runs; clusters are taken from the sources.
        void add(Move move, std::span<const ClusterRun> sources, std::span<const ClusterRun> destinations);
    };

    // A file's movable runs in VCN order (unmovable extents left out), as a move takes them.
    [[nodiscard]] std::vector<ClusterRun> movableRuns(const Snapshot& snapshot, FileIndex file);

    // clustersOf(movableRuns(snapshot, file)) without building the runs.
    [[nodiscard]] ClusterCount movableClusters(const Snapshot& snapshot, FileIndex file);

    // fragmentsOf(movableRuns(snapshot, file)) without building the runs (totals count millions of files).
    [[nodiscard]] std::uint64_t movableFragments(const Snapshot& snapshot, FileIndex file);

    // Clusters in a run list.
    [[nodiscard]] ClusterCount clustersOf(std::span<const ClusterRun> runs);

    // Fragments of a run list: runs that continue exactly where the previous one ended count once.
    [[nodiscard]] std::uint64_t fragmentsOf(std::span<const ClusterRun> runs);

    // Fills plan.totals from its moves (fragments from the snapshot's runs and the last move of each file).
    void finishTotals(Plan& plan, const Snapshot& snapshot);

} // namespace icd::core::planner
