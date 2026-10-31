#pragma once

#include "core/resources/ResourceCaps.h"

namespace icd::platform {

    // Reads processors and memory available to this process: all processor groups, the process affinity
    // mask, job-object CPU rate and memory limits, and physical memory (plan.md 6.6). Cheap; call it before
    // each job so caps follow what the machine has right now.
    core::DetectedResources detectSystemResources();

    // Windows' low-memory signal (CreateMemoryResourceNotification), so long jobs can drop optional caches
    // while running instead of failing halfway.
    class LowMemorySignal {
    public:
        // Opens the system notification; when that fails, signalled() is always false.
        LowMemorySignal();
        ~LowMemorySignal();
        LowMemorySignal(const LowMemorySignal&) = delete;
        LowMemorySignal& operator=(const LowMemorySignal&) = delete;

        // Whether Windows currently reports low physical memory.
        [[nodiscard]] bool signalled() const;

    private:
        void* handle_ = nullptr; // Notification handle (HANDLE); kept as void* so this header needs no <windows.h>.
    };

} // namespace icd::platform
