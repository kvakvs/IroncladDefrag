#pragma once

#include "core/resources/ResourceCaps.h"
#include "services/drives/IDriveProvider.h"
#include "services/exec/ThreadPool.h"
#include "services/job/JobRunner.h"
#include "services/settings/SettingsStore.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace icd::services {

    // Which drive list the app shows: the real volumes, or the Phase 1 fake list (kept for demos and for the
    // 30 TB design-target drive, which no machine here has).
    enum class DriveSource : std::uint8_t {
        System, // Enumerated volumes (plan.md 10.1).
        Fake, // FakeDriveProvider.
    };

    // The drive source asked for by the environment: ICD_FAKE_DRIVES=1 selects the fake list.
    [[nodiscard]] DriveSource driveSourceFromEnvironment();

    // Composition root of the service layer: detected resources, the active caps, the shared thread pool and
    // the job runner, the drive provider and the persisted settings. The GUI and the CLI create one and pass it down,
    // so neither builds services itself.
    class AppServices {
    public:
        // Detects resources, applies the Auto caps, starts the pool, picks the drive list and loads the settings
        // file (tests pass their own).
        explicit AppServices(DriveSource drives = DriveSource::System,
                             std::filesystem::path settingsFile = SettingsStore::defaultPath());

        // Cancels and waits for running jobs, then stops the pool.
        ~AppServices();

        AppServices(const AppServices&) = delete;
        AppServices& operator=(const AppServices&) = delete;

        // What the machine offered at start-up.
        [[nodiscard]] const core::DetectedResources& resources() const { return resources_; }

        // What the machine offers right now (free memory changes), for the About box and before jobs.
        [[nodiscard]] core::DetectedResources detectResources() const;

        // Worker count and memory budget in force.
        [[nodiscard]] const core::ResourceCaps& caps() const { return caps_; }

        // The shared pool.
        [[nodiscard]] ThreadPool& pool() { return *pool_; }

        // Runner for long operations.
        [[nodiscard]] JobRunner& jobs() { return *jobs_; }

        // Source of the all-disks list.
        [[nodiscard]] IDriveProvider& drives() { return *drives_; }

        // Settings kept between runs (UI thread only).
        [[nodiscard]] SettingsStore& settings() { return settings_; }

        // Whether the drive list is the fake one (analysis then stays synthetic).
        [[nodiscard]] bool fakeDrives() const { return driveSource_ == DriveSource::Fake; }

    private:
        core::DetectedResources resources_; // Detected at construction.
        core::ResourceCaps caps_; // From resources_ and the (default) settings.
        std::unique_ptr<ThreadPool> pool_; // Created before and destroyed after jobs_.
        std::unique_ptr<JobRunner> jobs_; // Runs jobs on pool_.
        DriveSource driveSource_; // Real or fake drives.
        std::unique_ptr<IDriveProvider> drives_; // Drive list source.
        SettingsStore settings_; // The settings file.
    };

} // namespace icd::services
