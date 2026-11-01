#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace icd::core {

    // A piece of a volume bitmap as FSCTL_GET_VOLUME_BITMAP returns it: bit i (least significant bit first
    // within each byte) set means cluster startLcn + i is in use. The full bitmap of a 30 TB volume is
    // 915 MB, so it is only ever handled in pieces (plan.md 6.2 principle 5).
    struct BitmapChunk {
        Lcn startLcn = 0; // Cluster of bit 0; a multiple of 8.
        ClusterCount clusters = 0; // Valid bits; bits past this are ignored.
        std::span<const std::uint8_t> bits; // At least (clusters + 7) / 8 bytes.
    };

    // Free runs of one chunk in LCN order. A run that touches the chunk's end stops there; stitchRuns()
    // joins it with the next chunk's first run.
    [[nodiscard]] std::vector<ClusterRun> freeRunsOf(const BitmapChunk& chunk);

    // Appends `next` (runs of the following chunk) to `runs`, joining the last run of `runs` with the first
    // of `next` when they meet at the chunk edge.
    void stitchRuns(std::vector<ClusterRun>& runs, std::span<const ClusterRun> next);

    // freeRunsOf() for a large chunk, cut into pieces of `grainBytes` of bitmap that are scanned in parallel
    // and stitched in LCN order, so the result is identical for any executor. grainBytes is rounded up to
    // a multiple of 8 (whole 64-bit words).
    [[nodiscard]] std::vector<ClusterRun> freeRunsParallel(Executor& executor, const BitmapChunk& chunk,
                                                           std::size_t grainBytes = 1U << 20U,
                                                           const CancelToken& token = {});

} // namespace icd::core
