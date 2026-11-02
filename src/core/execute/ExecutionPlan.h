#pragma once

#include "core/planner/LevelPlanner.h"
#include "core/planner/Plan.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace icd::core::execute {

    // Which volume and which planning a plan belongs to (plan.md 10.3): execution refuses a plan for another volume,
    // another format of it, or one older than the allowed age.
    struct PlanIdentity {
        std::string volumeName; // "\\?\Volume{GUID}\".
        std::uint64_t volumeSerial = 0; // NTFS serial; a reformatted volume has another.
        VolumeGeometry geometry; // Cluster size and count when planned.
        std::optional<UsnCursor> usn; // The planned snapshot's journal position, if it had one.
        std::int64_t madeAt = 0; // When the planned snapshot was taken (FILETIME ticks, UTC).
        std::string strategy; // Preset id, for the log.
        strategy::Effort effort = strategy::Effort::Fast; // Level, for the log.
    };

    // One move of an execution plan. Files are named by their NTFS file reference, not a snapshot index, so the plan
    // can be saved, revalidated against a later scan and resumed after a crash (plan.md 10.4).
    struct PlannedMove {
        std::uint64_t fileReference = 0; // The file (record and sequence: a reused record is another file).
        planner::MoveReason reason = planner::MoveReason::Relocate; // Why, for the log.
        bool fromParking = false; // Second move of a parked file: it starts from scratch space.
        bool extra = false; // Not needed by the lower bound (parking, evictions).
        std::uint32_t firstSource = 0; // First source run in ExecutionPlan::runs.
        std::uint32_t sourceCount = 0; // Source runs, in the file's (stream, VCN) order.
        std::uint32_t firstDestination = 0; // First destination run in ExecutionPlan::runs.
        std::uint32_t destinationCount = 0; // Destination runs, same order.
        ClusterCount clusters = 0; // Clusters moved.
        ClusterRun region{}; // Where a replacement destination may go when the planned one is taken (its zone's span).
        std::uint16_t fileFlags = 0; // FileFlag bits when planned (compressed files move in whole units).
    };

    // A plan ready to execute: its identity, moves in order, their runs, the files' paths for the log, and the runs a
    // replacement destination must avoid (the MFT zone).
    struct ExecutionPlan {
        PlanIdentity identity; // Volume and planning.
        std::vector<PlannedMove> moves; // In execution order.
        std::vector<ClusterRun> runs; // Source and destination runs of all moves.
        std::vector<std::string> paths; // Per move: the file's path when planned ("\dir\name").
        std::vector<ClusterRun> reserved; // Kept free by replacement destinations.

        // A move's source runs.
        [[nodiscard]] std::span<const ClusterRun> sources(const PlannedMove& move) const;

        // A move's destination runs.
        [[nodiscard]] std::span<const ClusterRun> destinations(const PlannedMove& move) const;

        // Appends a move with its runs (clusters taken from the sources).
        void add(PlannedMove move, std::span<const ClusterRun> from, std::span<const ClusterRun> to, std::string path);
    };

    // The execution plan of one planned level: every move with its file's reference and path, and the span of its
    // zone's target as the region a replacement destination may use (the whole volume for moves without a zone).
    [[nodiscard]] ExecutionPlan executionPlanOf(const planner::PlanResult& result, const Snapshot& snapshot,
                                                PlanIdentity identity, std::vector<ClusterRun> reserved = {});

    // Clusters of all moves.
    [[nodiscard]] ClusterCount plannedClusters(const ExecutionPlan& plan);

} // namespace icd::core::execute
