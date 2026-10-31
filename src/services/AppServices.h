#pragma once

#include "core/resources/ResourceCaps.h"
#include "services/drives/IDriveProvider.h"
#include "services/exec/ThreadPool.h"
#include "services/job/JobRunner.h"

#include <memory>

namespace icd::services {

    // Composition root of the service layer: detected resources, the active caps, the shared thread pool and
    // the job runner and the drive provider. The GUI and the CLI create one and pass it down, so neither builds
    // services itself.
    class AppServices {
    public:
        // Detects resources, applies the Auto caps and starts the pool.
        AppServices();

        // Cancels and waits for running jobs, then stops the pool.
        ~AppServices();

        AppServices(const AppServices&) = delete;
        AppServices& operator=(const AppServices&) = delete;

        // What the machine offered at start-up.
        [[nodiscard]] const core::DetectedResources& resources() const { return resources_; }

        // Worker count and memory budget in force.
        [[nodiscard]] const core::ResourceCaps& caps() const { return caps_; }

        // The shared pool.
        [[nodiscard]] ThreadPool& pool() { return *pool_; }

        // Runner for long operations.
        [[nodiscard]] JobRunner& jobs() { return *jobs_; }

        // Source of the all-disks list (fake until real enumeration exists in Phase 3).
        [[nodiscard]] IDriveProvider& drives() { return *drives_; }

    private:
        core::DetectedResources resources_; // Detected at construction.
        core::ResourceCaps caps_; // From resources_ and the (default) settings.
        std::unique_ptr<ThreadPool> pool_; // Created before and destroyed after jobs_.
        std::unique_ptr<JobRunner> jobs_; // Runs jobs on pool_.
        std::unique_ptr<IDriveProvider> drives_; // Drive list source.
    };

} // namespace icd::services
