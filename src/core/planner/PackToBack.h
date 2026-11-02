#pragma once

#include "core/planner/FastFill.h"
#include "core/planner/SortZone.h"
#include "core/planner/TargetLayout.h"
#include "core/snapshot/Snapshot.h"

#include <span>

namespace icd::core::planner {

    // Pack-to-back (plan.md 8.1, 8.6): the zone's target already ends at the end of the volume, so packing means
    // filling it from the back. Without an order (video at Fast and Moderate) holes nearest the end are filled first
    // with the largest file that fits, which closes the holes deleted files left; with an order (age at Maximum: newest
    // first, oldest at the very end) the zone is sorted. Files already packed with good-enough fragments are not
    // candidates, so they never move. `decision` is the zone's sort decision at the level (unsorted when it has no
    // order there).
    [[nodiscard]] FillResult packToBack(const Snapshot& snapshot, const ZoneTarget& target, const SortedZone& decision,
                                        std::span<const FileIndex> movers, HoleSet& pool, FillOptions options);

} // namespace icd::core::planner
