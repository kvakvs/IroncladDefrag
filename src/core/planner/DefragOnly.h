#pragma once

#include "core/planner/FastFill.h"
#include "core/planner/HoleSet.h"
#include "core/planner/Plan.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>
#include <optional>
#include <span>

namespace icd::core::planner {

    // How defragment-only moves may place a file.
    struct DefragOptions {
        // Smallest acceptable fragment when no single free run holds the file (nullopt: the file must be one run).
        std::optional<std::uint64_t> fragmentOk{};
        ClusterRun window{}; // Where destinations may go; empty means the whole volume.
        bool reuseVacated = true; // Later files may use what earlier moves free (moves keep their order).
    };

    // Defragments files near where they are (plan.md 8.1 defragment-only): each file goes to the free run closest to
    // its first cluster that holds it in one piece, or, when allowed, to pieces of at least fragmentOk. Larger files go
    // first so they find room; files that fit nowhere are skipped with a reason. Moves are appended to the plan in an
    // order that is valid as it stands; destinations come out of `pool`.
    void planDefragment(const Snapshot& snapshot, std::span<const FileIndex> files, HoleSet& pool,
                        const DefragOptions& options, Plan& plan);

    // Destinations for defragment-only files out of after-plan free space (no reuse step: the pool already holds what
    // moving files vacate, and ordering comes later): each file to the run nearest its first cluster, or large pieces
    // when allowed. Files that fit nowhere are returned as overflow.
    [[nodiscard]] FillResult defragmentDestinations(const Snapshot& snapshot, std::span<const FileIndex> files,
                                                    HoleSet& pool, const DefragOptions& options);

} // namespace icd::core::planner
