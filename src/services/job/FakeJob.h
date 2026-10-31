#pragma once

#include "core/job/PauseGate.h"
#include "services/job/JobRunner.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace icd::services {

    // Shape of the fake job, so tests can make it tiny and the Debug menu can make it long enough to watch.
    struct FakeJobOptions {
        std::size_t chunks = 10'000; // Parallel work items in the main stage.
        std::uint32_t spinIterations = 100000; // CPU work per chunk (about 1 ms; shows up on all cores).
        std::chrono::microseconds sleepPerChunk{9000}; // Waiting per chunk (stands in for disk reads).
        std::string serialStage = "Preparing"; // Progress name of the short serial stage.
        std::string parallelStage = "Crunching"; // Progress name of the parallel stage.
        std::function<std::string(std::size_t)> itemLabel{}; // Progress message per chunk ("current file"); optional.
        std::shared_ptr<core::PauseGate> pause{}; // Checked before each chunk; null when the job cannot pause.
    };

    // A job that looks like a scan, plan or move without touching any disk: a short serial stage, then a parallel stage
    // over the pool, with progress and cancellation. Stands in for every workflow step in the Phase 1 UI shell
    // and is used by tests of the job runner.
    JobFunction makeFakeParallelJob(FakeJobOptions options = {});

} // namespace icd::services
