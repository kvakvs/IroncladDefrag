#pragma once

#include "core/resources/ResourceCaps.h"
#include "services/drives/IDriveProvider.h"
#include "services/exec/ThreadPool.h"
#include "services/job/JobRunner.h"
#include "services/settings/SettingsStore.h"
#include "services/settings/StrategyLibrary.h"

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
        // Loads the settings file (tests pass their own) and the custom strategies beside it, detects resources,
        // applies the caps from the settings, starts the pool and picks the drive list.
        explicit AppServices(DriveSource drives = DriveSource::System,
                             std::filesystem::path settingsFile = SettingsStore::defaultPath());

        // Cancels and waits for running jobs, then stops the pool.
        ~AppServices();

        AppServices(const AppServices&) = delete;
        AppServices& operator=(const AppServices&) = delete;

        // What the machine offered when the caps were last computed.
        [[nodiscard]] const core::DetectedResources& resources() const { return resources_; }

        // What the machine offers right now (free memory changes), for the About box and before jobs.
        [[nodiscard]] core::DetectedResources detectResources() const;

        // Worker count and memory budget in force.
        [[nodiscard]] const core::ResourceCaps& caps() const { return caps_; }

        // Detects resources again and applies the caps from the settings to the pool, so the next job gets what
        // the machine offers now (plan.md 6.6: detected at start-up and again before each job). Call between jobs.
        void refreshCaps();

        // Saves new settings and applies their caps (refreshCaps()). False when the file could not be written; the
        // settings still apply for this run.
        bool applySettings(Settings settings);

        // The shared pool.
        [[nodiscard]] ThreadPool& pool() { return *pool_; }

        // Runner for long operations.
        [[nodiscard]] JobRunner& jobs() { return *jobs_; }

        // Source of the all-disks list.
        [[nodiscard]] IDriveProvider& drives() { return *drives_; }

        // Settings kept between runs (UI thread only).
        [[nodiscard]] SettingsStore& settings() { return settings_; }

        // Built-in and custom strategies (UI thread only).
        [[nodiscard]] StrategyLibrary& strategies() { return strategies_; }

        // Whether the drive list is the fake one (analysis then stays synthetic).
        [[nodiscard]] bool fakeDrives() const { return driveSource_ == DriveSource::Fake; }

    private:
        SettingsStore settings_; // The settings file; first, since the caps come from it.
        StrategyLibrary strategies_; // Custom strategies in the "strategies" directory beside the settings file.
        core::DetectedResources resources_; // Detected at construction and by refreshCaps().
        core::ResourceCaps caps_; // From resources_ and the settings' manual caps.
        std::unique_ptr<ThreadPool> pool_; // Created before and destroyed after jobs_.
        std::unique_ptr<JobRunner> jobs_; // Runs jobs on pool_.
        DriveSource driveSource_; // Real or fake drives.
        std::unique_ptr<IDriveProvider> drives_; // Drive list source.
    };

} // namespace icd::services
