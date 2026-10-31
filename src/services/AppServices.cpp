#include "services/AppServices.h"

#include "platform/win/SystemResources.h"
#include "services/drives/FakeDriveProvider.h"

namespace icd::services {

    AppServices::AppServices() :
        resources_(platform::detectSystemResources()), caps_(core::computeCaps(resources_, {})),
        pool_(std::make_unique<ThreadPool>(caps_.workerThreads)), jobs_(std::make_unique<JobRunner>(*pool_)),
        drives_(std::make_unique<FakeDriveProvider>()) {}

    AppServices::~AppServices() {
        jobs_.reset(); // Waits for jobs while the pool still runs them.
        pool_.reset();
    }

} // namespace icd::services
