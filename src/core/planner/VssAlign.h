#pragma once

#include "core/planner/HoleSet.h"
#include "core/planner/Placement.h"
#include "core/planner/Plan.h"
#include "core/planner/TargetLayout.h"
#include "core/snapshot/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::core::planner {

    // Shadow copies copy on write in 16 KB blocks (plan.md 3, fact 3).
    inline constexpr std::uint32_t vssBlockBytes = 16 * 1024;

    // Clusters per copy-on-write block: 4 with 4 KB clusters, 1 from 16 KB clusters up (every move is aligned).
    [[nodiscard]] ClusterCount vssUnit(std::uint32_t bytesPerCluster);

    // The block geometry alignment is checked against.
    struct VssGeometry {
        ClusterCount unit = 4; // Clusters per copy-on-write block (vssUnit()).
        std::uint32_t bytesPerCluster = 4096; // Cluster size.
    };

    // Whether moving `sources` (VCN order) into `destinations` keeps every cluster's offset within a copy-on-write
    // block (destination LCN = source LCN modulo the unit) and moves at least one block, so shadow storage does not
    // grow.
    [[nodiscard]] bool movesAligned(std::span<const ClusterRun> sources, std::span<const ClusterRun> destinations,
                                    const VssGeometry& geometry);

    // Shadow-copy cost of a plan (plan.md 9.4).
    struct VssReport {
        std::size_t moves = 0; // Moves of file data (parking included).
        std::size_t unaligned = 0; // Moves that are not block-aligned.
        // Estimated shadow storage growth, an upper bound: the bytes unaligned moves rewrite (on the vss scenario
        // Windows copied about a sixth of it, T121).
        std::uint64_t growthBytes = 0;
    };

    // Counts the unaligned moves of a plan and estimates the shadow storage growth.
    [[nodiscard]] VssReport vssReport(const Plan& plan, std::uint32_t bytesPerCluster);

    // "Block unaligned moves" (plan.md 9.4): drops every move that is not block-aligned, and with it every later move
    // whose destination overlaps the sources of a dropped move (those clusters stay taken) and the other move of a
    // parked file, until nothing more depends on a dropped move. Dropped moves the lower bound needs become skipped
    // (Unaligned, or Dependency when they only waited on one); extra ones simply go. The plan's extra is recounted;
    // call finishTotals() afterwards. Returns how many moves were dropped.
    std::size_t dropUnaligned(Plan& plan, std::uint32_t bytesPerCluster);

    // dropUnaligned() as a planning step: adds a note saying how many moves were left out (none: no note).
    void blockUnalignedMoves(Plan& plan, std::uint32_t bytesPerCluster);

    // Prefers aligned destinations (plan.md 9.4): a single-run destination whose offset to the file's first run is
    // off by d clusters is shifted by up to unit - 1 clusters inside the after-plan free space (`pool`, which no longer
    // holds the destinations), when the shifted run stays in the same zone (with a layout) and off the file's own runs.
    // Returns how many destinations were aligned this way.
    std::size_t alignDestinations(const Snapshot& snapshot, std::vector<Destination>& destinations, HoleSet& pool,
                                  const TargetLayout* layout);

} // namespace icd::core::planner
