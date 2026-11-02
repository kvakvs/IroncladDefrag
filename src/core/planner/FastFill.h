#pragma once

#include "core/planner/HoleSet.h"
#include "core/planner/Placement.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::planner {

    // Which end of a region is packed.
    enum class PackFrom : std::uint8_t {
        Front, // Holes nearest the region's start are filled first; free space gathers at its end.
        Back, // Holes nearest the region's end are filled first (pack-to-back zones, plan.md 8.6).
    };

    // How a region is filled.
    struct FillOptions {
        PackFrom from = PackFrom::Front; // Which end is packed.
        ZoneIndex zone = noZone; // Recorded in the destinations.
        // The area the files count as already in (the zone's span, a hot spot): moves that stay inside it are
        // compaction (or defragmentation), others relocations.
        ClusterRun home{};
        std::optional<std::uint64_t> fragmentOk{}; // Allows large pieces for files no single hole holds.
    };

    // Destinations of the files that fit, and the files that did not.
    struct FillResult {
        std::vector<Destination> placed{}; // One per file that fits, in placement order.
        std::vector<FileIndex> overflow{}; // Files no hole of the region holds.
    };

    // Fast-fill (plan.md 8.1, 9.3.5): fills the region's holes from the packed end, each hole with the largest file
    // that still fits (best fit by size), so every move removes part of a hole and shortens the zone. The region is the
    // zone's exact target, so the files beyond its packed boundary are exactly the ones given, and when they all fit
    // the bytes moved equal the lower bound. `pool` holds the space free after the plan (free runs plus what moving
    // files vacate); destinations are taken out of it and never overlap the file's own runs.
    [[nodiscard]] FillResult fastFill(const Snapshot& snapshot, std::span<const ClusterRun> region,
                                      std::span<const FileIndex> files, HoleSet& pool, const FillOptions& options);

    // Why a file moves to a destination: inside its home it is compaction (or defragmentation of a fragmented file),
    // from outside a relocation.
    [[nodiscard]] MoveReason reasonFor(std::span<const ClusterRun> runs, ClusterRun home);

} // namespace icd::core::planner
