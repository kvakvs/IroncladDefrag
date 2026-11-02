#include "core/planner/PackToBack.h"

namespace icd::core::planner {

    FillResult packToBack(const Snapshot& snapshot, const ZoneTarget& target, const SortedZone& decision,
                          std::span<const FileIndex> movers, HoleSet& pool, FillOptions options) {
        options.from = PackFrom::Back;
        if (decision.sorted && decision.keys != nullptr) {
            return sortZone(snapshot, target, decision, movers, pool, options);
        }
        return fastFill(snapshot, target.pieces, movers, pool, options);
    }

} // namespace icd::core::planner
