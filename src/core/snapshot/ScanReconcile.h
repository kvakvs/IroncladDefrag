#pragma once

#include "core/snapshot/SnapshotBuilder.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <vector>

namespace icd::core {

    // What reconcileScan() had to correct, for the analysis summary and the log.
    struct ReconcileStats {
        std::uint64_t droppedExtents = 0; // Extents of files that changed while the scan read other files.
        ClusterCount freeTaken = 0; // Clusters free in the bitmap but claimed by extents read later.
        ClusterCount unattributed = 0; // Used clusters no extent claims (unreadable system or locked files).
        std::uint64_t unattributedRuns = 0; // Runs of those clusters.
    };

    // Makes a live scan consistent and complete before it is built into a snapshot. The bitmap is read first
    // and files one after another, so on a volume in use the pieces can disagree:
    //  - extents overlapping each other: the file read earlier changed; its extent is dropped (files are read
    //    in index order, so the higher index is the newer read);
    //  - extents over bitmap free space: allocated after the bitmap was read; taken out of the free runs;
    //  - clusters neither free nor in any extent: files that cannot be opened ($LogFile, $Bitmap, $Boot,
    //    $BadClus, locked files) or dropped extents; they become unmovable extents of `unattributedFile`.
    // Afterwards extents and free runs are disjoint and together cover [0, totalClusters) exactly. Both inputs
    // may be in any order; extents come back sorted by LCN, free runs sorted and merged.
    ReconcileStats reconcileScan(std::vector<ExtentEntry>& extents, std::vector<ClusterRun>& freeRuns,
                                 ClusterCount totalClusters, FileIndex unattributedFile);

} // namespace icd::core
