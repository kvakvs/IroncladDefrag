#pragma once

#include "core/classify/Catalog.h"
#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace icd::core::planner {

    // Index of a zone in its script; 16 bits keep the per-file column small at 20 M files.
    using ZoneIndex = std::uint16_t;

    // "No zone": a file no zone selects; it is defragmented where it is.
    inline constexpr ZoneIndex noZone = std::numeric_limits<ZoneIndex>::max();

    // How the planner may treat a file.
    enum class FileRole : std::uint8_t {
        Fixed, // Never moves: unmovable, NTFS metadata, resident, encrypted, or without movable clusters.
        InPlace, // A keep-in-place zone's file (churn): stays, and is an obstacle for every zone.
        Defragment, // Defragment-only (or no zone): joins its fragments near where it is; an obstacle for zones.
        Placed, // Belongs to a zone with a place on the volume.
    };

    // What classifying needs besides the snapshot.
    struct AssignInputs {
        std::span<const classify::ClassId> classes{}; // Per file: its catalog class.
        std::span<const std::uint8_t> churn{}; // Per file: non-zero when it churns; empty when unknown.
        std::int64_t now = 0; // FILETIME ages are measured from (the scan time), so results are reproducible.
    };

    // Each file's zone and role, plus its movable clusters, computed once per snapshot and script and shared by the
    // three effort levels.
    struct ZoneAssignment {
        std::vector<ZoneIndex> zoneOf{}; // Per file: the first zone whose selector matches, or noZone.
        std::vector<FileRole> roles{}; // Per file: how it may be treated.
        std::vector<ClusterCount> movable{}; // Per file: clusters of its movable extents.
        std::vector<ClusterCount> zoneClusters{}; // Per zone: movable clusters of its Placed files.
    };

    // Whether a file can never be moved, whatever its zone: flags, no movable clusters, or a file Windows owns in
    // "System Volume Information".
    [[nodiscard]] bool fixedFile(const Snapshot& snapshot, FileIndex file, ClusterCount movable);

    // Assigns every file of a snapshot to the first zone whose selector matches (plan.md 8.1) and decides its role.
    // Class ids in selectors are looked up in `catalog`; path globs and extensions run through one rule engine per
    // zone that has them. The same for any executor.
    [[nodiscard]] ZoneAssignment assignZones(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                             const classify::Catalog& catalog, const AssignInputs& inputs,
                                             Executor& executor, const CancelToken& token = {});

} // namespace icd::core::planner
