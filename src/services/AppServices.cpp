#include "services/AppServices.h"

#include "platform/win/SystemResources.h"
#include "services/drives/FakeDriveProvider.h"
#include "services/drives/SystemDriveProvider.h"

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
        resources_(platform::detectSystemResources()), caps_(core::computeCaps(resources_, {})),
        pool_(std::make_unique<ThreadPool>(caps_.workerThreads)), jobs_(std::make_unique<JobRunner>(*pool_)),
        driveSource_(drives), drives_(makeDriveProvider(drives)), settings_(std::move(settingsFile)) {}

    core::DetectedResources AppServices::detectResources() const { return platform::detectSystemResources(); }

    AppServices::~AppServices() {
        jobs_.reset(); // Waits for jobs while the pool still runs them.
        pool_.reset();
    }

} // namespace icd::services
