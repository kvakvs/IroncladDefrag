#pragma once

#include "core/planner/LowerBound.h"
#include "core/planner/ZoneAssignment.h"

#include <cstddef>
#include <vector>

namespace icd::core::planner {

    // The files a level works on after the good-enough filter (plan.md 6.4 step 2, 9.3.3): good-enough files, churn
    // (in-place) files and fixed files are gone, which on typical drives removes most files before any expensive step.
    struct Candidates {
        std::vector<std::vector<FileIndex>> perZone{}; // Per script zone: its Placed files that must move, by index.
        std::vector<FileIndex> defragment{}; // Defragment-only files (and files without a zone) that must move.
        ClusterCount clusters = 0; // Movable clusters of all candidates: the lower bound.
    };

    // Splits the must-move files of a lower bound by zone; the same for any executor.
    [[nodiscard]] Candidates filterCandidates(const LowerBound& bound, const ZoneAssignment& assignment,
                                              std::size_t zones);

} // namespace icd::core::planner
