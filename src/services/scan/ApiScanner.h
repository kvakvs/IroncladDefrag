#pragma once

#include "core/job/CancelToken.h"
#include "core/job/Progress.h"
#include "core/snapshot/ScanReconcile.h"
#include "core/snapshot/Snapshot.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace icd::services {

    // What the API scan saw, beyond the snapshot itself, for the summary and the log.
    struct ApiScanStats {
        std::uint64_t files = 0; // Files and directories in the snapshot (the placeholder included).
        std::uint64_t directories = 0; // Directories among them.
        std::uint64_t streams = 0; // Named data streams read.
        std::uint64_t unreadable = 0; // Files that could not be opened or queried (listed without extents).
        std::vector<core::FileIndex> unreadableFiles; // The first 100 of them, for the log.
        core::ReconcileStats reconcile; // Corrections for changes during the scan and unreadable areas.
    };

    // The snapshot and its statistics.
    struct ApiScanResult {
        core::Snapshot snapshot; // Files, extents and free space of the volume.
        ApiScanStats stats; // What it took to get there.
    };

    // Name of the placeholder file that owns used clusters no readable file claims ($LogFile, $Bitmap,
    // $Boot, $BadClus, locked files); it is unmovable.
    inline constexpr const char* unattributedName = "$Unattributed";

    // Receives a snapshot of what the scan has read so far: files not read yet own nothing, and the clusters not
    // claimed yet belong to the placeholder. Lets the map fill in while the scan runs. Called on the scan's thread.
    using PartialScanSink = std::function<void(core::Snapshot)>;

    // The reference scanner of plan.md 10.2 (API path): file list from FSCTL_ENUM_USN_DATA, then for each file
    // in MFT record order its metadata and the retrieval pointers of every data stream (directories: their
    // index allocation). Flags resident, compressed, sparse, encrypted, reparse points, hard links, alternate
    // streams, NTFS metadata and unmovable system files. Reads only; never writes to the volume.
    //   volume     handle with read access (administrators)
    //   root       path the volume is mounted on ("T:\"), for opening named streams by path
    //   freeRuns   free space from the bitmap, read before the files
    //   partial    optional; gets a partial snapshot about every `partialInterval` while files are read
    // Throws std::runtime_error when the volume cannot be enumerated and OperationCancelled on cancel.
    [[nodiscard]] ApiScanResult scanFilesApi(HANDLE volume, const std::wstring& root, core::VolumeGeometry geometry,
                                             std::vector<core::ClusterRun> freeRuns, const core::CancelToken& token,
                                             core::ProgressSink* progress, const PartialScanSink& partial = {},
                                             std::chrono::milliseconds partialInterval = std::chrono::seconds(3));

} // namespace icd::services
