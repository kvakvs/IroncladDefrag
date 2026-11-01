#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/BitmapToRuns.h"
#include "core/snapshot/FreeRunList.h"

#include <windows.h>

#include <cstddef>
#include <functional>

namespace icd::platform {

    // Reads the volume bitmap with FSCTL_GET_VOLUME_BITMAP in pieces of about `chunkBytes` of bitmap and
    // hands each to `onChunk` in LCN order; the buffer is reused, so the chunk is only valid during the call.
    // The volume handle needs read access (administrators). Throws std::runtime_error on failure and
    // OperationCancelled when the token is cancelled between pieces.
    void readVolumeBitmap(HANDLE volume, std::size_t chunkBytes,
                          const std::function<void(const core::BitmapChunk&)>& onChunk,
                          const core::CancelToken& token = {});

    // Progress of a free-space read: clusters covered so far out of the volume's total.
    using BitmapProgress = std::function<void(core::ClusterCount done, core::ClusterCount total)>;

    // The volume's free space as runs (plan.md 6.2 principle 5): bitmap pieces of 32 MB (about 1 TB of 4 KB
    // clusters) read one after another, each turned into runs in parallel on the executor and stitched.
    [[nodiscard]] core::FreeRunList readFreeRuns(HANDLE volume, core::ClusterCount totalClusters,
                                                 core::Executor& executor, const core::CancelToken& token = {},
                                                 const BitmapProgress& progress = {});

} // namespace icd::platform
