#pragma once

#include "services/job/JobRunner.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace icd::services {

    // Shape of the fake job, so tests can make it tiny and the Debug menu can make it long enough to watch.
    struct FakeJobOptions {
        std::size_t chunks = 10'000; // Parallel work items in the main stage.
        std::uint32_t spinIterations = 100000; // CPU work per chunk (about 1 ms; shows up on all cores).
        std::chrono::microseconds sleepPerChunk{9000}; // Waiting per chunk (stands in for disk reads).
    };

    // A job that looks like a scan without touching any disk: a short serial stage, then a parallel stage
    // over the pool, with progress and cancellation. Used by the Debug menu to check that long jobs keep the
    // UI responsive (plan.md Phase 0 exit) and by tests of the job runner.
    JobFunction makeFakeParallelJob(FakeJobOptions options = {});

} // namespace icd::services
