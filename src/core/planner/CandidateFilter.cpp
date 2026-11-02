#include "core/planner/CandidateFilter.h"

namespace icd::core::planner {

    Candidates filterCandidates(const LowerBound& bound, const ZoneAssignment& assignment, std::size_t zones) {
        Candidates out;
        out.perZone.resize(zones);
        for (std::size_t f = 0; f < bound.mustMove.size(); ++f) {
            if (bound.mustMove[f] == 0) {
                continue;
            }
            const auto file = static_cast<FileIndex>(f);
            const ZoneIndex zone = assignment.zoneOf[f];
            if (assignment.roles[f] == FileRole::Placed && zone < zones) {
                out.perZone[zone].push_back(file);
            }
            else {
                out.defragment.push_back(file);
            }
            out.clusters += assignment.movable[f];
        }
        return out;
    }

} // namespace icd::core::planner
