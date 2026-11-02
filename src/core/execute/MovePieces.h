#pragma once

#include "core/snapshot/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::execute {

    // One allocated run of one stream of a file as the file system reports it now (holes left out).
    struct StreamRun {
        std::uint16_t stream = 0; // Stream ordinal (0 main data or directory index, 1..n named streams).
        std::uint64_t vcn = 0; // Position of the run inside the stream.
        Lcn lcn = 0; // First cluster on disk.
        ClusterCount length = 0; // Clusters.

        // Runs compare as values (tests).
        friend bool operator==(const StreamRun&, const StreamRun&) = default;
    };

    // A stretch of a file's clusters and where a move takes it: from `from` to `to`, `length` clusters at `vcn` of
    // `stream`. `done` when the clusters already lie at `to` (a move cut short by a crash or cancel, then resumed).
    struct MovePiece {
        std::uint16_t stream = 0; // Stream ordinal.
        std::uint64_t vcn = 0; // First virtual cluster in the stream.
        Lcn from = 0; // Where the clusters were planned to come from.
        Lcn to = 0; // Where they go.
        ClusterCount length = 0; // Clusters.
        bool done = false; // Already at `to`.

        // Pieces compare as values (tests).
        friend bool operator==(const MovePiece&, const MovePiece&) = default;
    };

    // The source and destination runs of one move, in the file's (stream, VCN) order.
    struct MoveRuns {
        std::span<const ClusterRun> sources; // Where the clusters are planned to be before the move.
        std::span<const ClusterRun> destinations; // Where they go, cluster for cluster in the same order.
    };

    // Matches a file's clusters now against a move: cluster i of the file (in stream and VCN order) must lie at
    // cluster i of the sources or of the destinations. Returns the pieces in file order, or nullopt when the file
    // changed: another cluster count, or clusters at neither place. A move with every piece done has nothing to do.
    [[nodiscard]] std::optional<std::vector<MovePiece>> matchMove(std::span<const StreamRun> now, MoveRuns move);

    // The pieces that take a file from wherever it is now to the destinations, cluster for cluster; nullopt when the
    // cluster counts differ. Used for a file whose earlier move went to a replacement destination, so later moves of it
    // (the return of a parked file) start from there instead of from their planned sources.
    [[nodiscard]] std::optional<std::vector<MovePiece>> remapMove(std::span<const StreamRun> now,
                                                                  std::span<const ClusterRun> destinations);

    // The LCN runs of a file's stream runs in order, runs that continue on disk joined.
    [[nodiscard]] std::vector<ClusterRun> lcnRunsOf(std::span<const StreamRun> runs);

    // Stream runs for planned runs without stream facts (stream 0, VCNs counted along), for replays on a snapshot.
    [[nodiscard]] std::vector<StreamRun> asStreamRuns(std::span<const ClusterRun> runs);

    // Whether every piece is done.
    [[nodiscard]] bool allDone(std::span<const MovePiece> pieces);

    // Clusters of the pieces still to move.
    [[nodiscard]] ClusterCount pendingClusters(std::span<const MovePiece> pieces);

} // namespace icd::core::execute
