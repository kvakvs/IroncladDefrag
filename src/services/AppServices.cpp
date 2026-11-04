#include "services/AppServices.h"

#include "platform/win/SystemResources.h"
#include "services/drives/FakeDriveProvider.h"
#include "services/drives/SystemDriveProvider.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace icd::services {

    namespace {

        // The provider for a drive source.
        std::unique_ptr<IDriveProvider> makeDriveProvider(DriveSource source) {
            if (source == DriveSource::Fake) {
                return std::make_unique<FakeDriveProvider>();
            }
            return std::make_unique<SystemDriveProvider>();
        }

        // Threads the pool starts: every usable processor, so a manual cap up to that count needs no new pool.
        std::size_t poolCapacity(const core::DetectedResources& detected, const core::ResourceCaps& caps) {
            return std::max<std::size_t>(detected.logicalProcessors, caps.workerThreads);
        }

    } // namespace

    DriveSource driveSourceFromEnvironment() {
        char* value = nullptr;
        std::size_t length = 0;
        const bool fake =
            _dupenv_s(&value, &length, "ICD_FAKE_DRIVES") == 0 && value != nullptr && std::string_view(value) == "1";
        std::free(value);
        return fake ? DriveSource::Fake : DriveSource::System;
    }

    AppServices::AppServices(DriveSource drives, std::filesystem::path settingsFile) :
        settings_(std::move(settingsFile)), resources_(platform::detectSystemResources()),
        caps_(core::computeCaps(resources_, settings_.current().resources)),
        pool_(std::make_unique<ThreadPool>(poolCapacity(resources_, caps_))),
        jobs_(std::make_unique<JobRunner>(*pool_)), driveSource_(drives), drives_(makeDriveProvider(drives)) {
        pool_->setConcurrency(caps_.workerThreads);
    }

    core::DetectedResources AppServices::detectResources() const { return platform::detectSystemResources(); }

    void AppServices::refreshCaps() {
        resources_ = platform::detectSystemResources();
        caps_ = core::computeCaps(resources_, settings_.current().resources);
        pool_->setConcurrency(caps_.workerThreads); // Clamped to the threads started (processors seen at start-up).
        caps_.workerThreads = static_cast<std::uint32_t>(pool_->concurrency());
    }

    bool AppServices::applySettings(Settings settings) {
        const bool saved = settings_.update(std::move(settings));
        refreshCaps();
        return saved;
    }

    AppServices::~AppServices() {
        jobs_.reset(); // Waits for jobs while the pool still runs them.
        pool_.reset();
    }

} // namespace icd::services
