#pragma once

#include "core/exec/Executor.h"
#include "core/execute/ExecutionPlan.h"
#include "core/map/MapData.h"
#include "core/snapshot/Types.h"
#include "services/execute/Executor.h"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace icd::services {

    // The map after some finished moves, and the clusters whose look may have changed (plan.md 7.2: moves during
    // execution mark only the affected bins and tiles dirty).
    struct LiveMapUpdate {
        std::shared_ptr<const core::MapData> map; // The map with the moves applied.
        std::vector<core::ClusterRun> dirty; // Old and new clusters of the moved files, sorted and merged.
    };

    // The shown map laid out from the volume as revalidation read it, for the live map to start from: files deleted,
    // grown or created between the analysis and the run change the layout, and moves that use the space they freed
    // would not fit the analysed snapshot. Same world; each file keeps the class it had on the shown map (found by
    // file reference; files new since the analysis are "unknown"). Returns null when the volume lays out exactly like
    // the shown map, which is the usual case.
    [[nodiscard]] std::shared_ptr<const core::MapData> rebaseMap(const core::MapData& shown, const core::Snapshot& now,
                                                                 core::Executor& executor);

    // Whether any of records can be applied to a map: it moved clusters and its runs were read back.
    [[nodiscard]] bool anyApplicable(std::span<const MoveRecord> records);

    // Applies finished moves to a shown map while execution runs (the live map of plan.md 10.4): the map's snapshot
    // is copied and patched with the runs read back after each record (snapshotAfter()), then only the pyramid bins
    // over the moved files' old and new clusters are recomputed (MapData::update()); world, classes and file indexes
    // stay. Records without moved clusters or without runs read back are left out (the map stays a little behind for
    // them). Returns nullopt when there is nothing to apply or the patch does not fit the map's snapshot (the volume
    // changed since the map was made in ways the moves do not explain): the caller stops following then.
    [[nodiscard]] std::optional<LiveMapUpdate> applyMoves(const core::MapData& map,
                                                          const core::execute::ExecutionPlan& plan,
                                                          std::span<const MoveRecord> records,
                                                          core::Executor& executor);

} // namespace icd::services
