#pragma once

#include "core/execute/MovePieces.h"

#include <cstdint>
#include <span>
#include <vector>

namespace icd::core::execute {

    // NTFS compression unit in clusters: compressed files move only in whole units (plan.md 10.2).
    inline constexpr std::uint64_t compressionUnitClusters = 16;

    // One FSCTL_MOVE_FILE call as the executor issues it: `span` virtual clusters of a stream from `vcn`, whose
    // `allocated` clusters go to `to` one after another (span and allocated differ only where compressed units leave
    // holes).
    struct MoveCall {
        std::uint16_t stream = 0; // Stream ordinal.
        std::uint64_t vcn = 0; // First virtual cluster.
        std::uint64_t span = 0; // Virtual clusters the call covers.
        Lcn to = 0; // First destination cluster.
        ClusterCount allocated = 0; // Clusters it moves.

        // Calls compare as values (tests).
        friend bool operator==(const MoveCall&, const MoveCall&) = default;
    };

    // How pieces become calls.
    struct CallShape {
        ClusterCount chunkClusters = 4096; // Largest call (16 MB in the executor), so pause and cancel act often.
        bool compressed = false; // Whole compression units: a call starts only at a unit boundary.
    };

    // The calls that move the pieces still pending, in file order. Plain files: each piece cut into calls of at most
    // chunkClusters. Compressed files: pieces of one stream whose destinations continue each other are joined over the
    // holes between them, and calls are cut only where a piece starts on a unit boundary, so every call moves whole
    // units (a call may then exceed chunkClusters by less than a unit).
    [[nodiscard]] std::vector<MoveCall> moveCalls(std::span<const MovePiece> pieces, CallShape shape);

    // Sends calls to consecutive clusters from `start` (a replacement destination after a stale bitmap); returns the
    // runs they now fill.
    std::vector<ClusterRun> retarget(std::span<MoveCall> calls, Lcn start);

    // Where a stretch of a file must lie after its move, for verification: the allocated clusters of `stream` within
    // [vcn, vcn + span) form one run starting at `to` of `allocated` clusters.
    struct Landing {
        std::uint16_t stream = 0; // Stream ordinal.
        std::uint64_t vcn = 0; // First virtual cluster.
        std::uint64_t span = 0; // Virtual clusters.
        Lcn to = 0; // Expected first cluster.
        ClusterCount allocated = 0; // Expected clusters.
    };

    // The landing of a call that went through.
    [[nodiscard]] Landing landingOf(const MoveCall& call);

    // The landing of a piece that was already done.
    [[nodiscard]] Landing landingOf(const MovePiece& piece);

    // Whether the file's runs after a move (re-read) put every landing where it should be.
    [[nodiscard]] bool landed(std::span<const StreamRun> after, std::span<const Landing> landings);

    // How much of a refused call landed anyway: NTFS moves a large call in internal pieces and keeps the pieces it
    // moved before refusing (seen on the test bed: 512 and 1024 of 833 and 2738 clusters).
    struct LandedPrefix {
        ClusterCount allocated = 0; // Clusters of the call now at its target, from the start.
        std::uint64_t vcnEnd = 0; // The virtual cluster after the last of them (the call's start when none).
    };

    // The prefix of a call the file's runs (read back after a refusal) show at the call's target.
    [[nodiscard]] LandedPrefix landedPrefix(std::span<const StreamRun> now, const MoveCall& call);

    // What is left of a call after a landed prefix.
    [[nodiscard]] MoveCall remainderOf(const MoveCall& call, LandedPrefix landed);

    // Clusters the calls move.
    [[nodiscard]] ClusterCount allocatedOf(std::span<const MoveCall> calls);

} // namespace icd::core::execute
