#include "services/snapshot/MappedStorage.h"

#include "core/log/Log.h"
#include "platform/win/LogSinks.h"
#include "platform/win/MappedMemory.h"

namespace icd::services {

    std::filesystem::path defaultCacheDirectory() { return platform::defaultLogDirectory().parent_path() / L"cache"; }

    std::shared_ptr<std::pmr::memory_resource> storageFor(const core::MemoryPlan& plan,
                                                          const std::filesystem::path& directory) {
        if (!plan.mappedStorage()) {
            return nullptr;
        }
        return std::make_shared<platform::MappedFileResource>(directory.empty() ? defaultCacheDirectory() : directory);
    }

    MemoryGovernor::MemoryGovernor(const core::SnapshotEstimate& estimate, const core::MemoryPlan& plan) :
        estimate_(estimate), plan_(plan) {}

    bool MemoryGovernor::check() {
        if (!signal_.signalled() || plan_.mappedStorage()) {
            return false;
        }
        plan_ = core::giveWay(plan_, estimate_);
        core::logging::warning("memory", {{"low_memory", core::describeMemoryPlan(plan_)}});
        return true;
    }

} // namespace icd::services
