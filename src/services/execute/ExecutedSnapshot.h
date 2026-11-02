#pragma once

#include "core/exec/Executor.h"
#include "core/execute/ExecutionPlan.h"
#include "core/snapshot/Snapshot.h"
#include "services/execute/Executor.h"

#include <optional>
#include <span>

namespace icd::services {

    // The volume after execution (plan.md 10.4: the snapshot updated from verified results). NTFS does not journal
    // moves, so a saved snapshot that does not know them would be caught up wrongly later: it is either this or
    // removed. `base` is the volume as revalidation read it; every file a record moved clusters of (wholly or in
    // part) gets the runs read back after its last move, its unmovable extents kept, and its old clusters are freed.
    // Returns nullopt (base untouched) when that cannot be consistent: a moved file whose runs were not read back, a
    // file missing from the base, or runs overlapping files the base still holds there (something else changed). On
    // success `base` is spent.
    [[nodiscard]] std::optional<core::Snapshot> snapshotAfter(core::Snapshot& base,
                                                              const core::execute::ExecutionPlan& plan,
                                                              std::span<const MoveRecord> records,
                                                              core::Executor& executor);

} // namespace icd::services
