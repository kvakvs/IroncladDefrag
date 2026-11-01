#pragma once

#include "core/resources/MemoryPlanner.h"
#include "platform/win/SystemResources.h"

#include <filesystem>
#include <memory>
#include <memory_resource>

namespace icd::services {

    // %LOCALAPPDATA%\IroncladDefrag\cache: the memory-mapped files of the last memory rung (plan.md 6.6).
    [[nodiscard]] std::filesystem::path defaultCacheDirectory();

    // Where a scan's extent index and name pool go under a memory plan: memory-mapped files in `directory` (default
    // directory when empty) on the last rung, the heap (null) otherwise. The snapshot keeps the storage alive.
    [[nodiscard]] std::shared_ptr<std::pmr::memory_resource> storageFor(const core::MemoryPlan& plan,
                                                                        const std::filesystem::path& directory = {});

    // Keeps a job's memory plan and steps it down the ladder when Windows signals low physical memory, so the job
    // drops the next items in the plan.md 6.6 order instead of failing. One per job, used from its thread.
    class MemoryGovernor {
    public:
        // Starts from a plan made for an estimate.
        MemoryGovernor(const core::SnapshotEstimate& estimate, const core::MemoryPlan& plan);

        // The current plan.
        [[nodiscard]] const core::MemoryPlan& plan() const { return plan_; }

        // Checks the signal; when memory is low, gives way one rung (logged) and returns true.
        bool check();

    private:
        core::SnapshotEstimate estimate_; // What the job needs.
        core::MemoryPlan plan_; // Current mode.
        platform::LowMemorySignal signal_; // Windows' low-memory notification.
    };

} // namespace icd::services
