#pragma once

#include <cstdint>
#include <string>

namespace icd::core {

    // What the machine offers this process, detected at start-up and again before each job (plan.md 6.6).
    struct DetectedResources {
        std::uint32_t logicalProcessors = 1; // Usable by this process: all groups, affinity and job limits.
        std::uint32_t physicalCores = 1; // Physical cores, for display.
        std::uint64_t totalMemoryBytes = 0; // Installed physical memory (or the job-object limit).
        std::uint64_t availableMemoryBytes = 0; // Free physical memory right now (or the job-object limit).
    };

    // The user's choices from Settings; 0 means "Auto".
    struct ResourceSettings {
        std::uint32_t workerThreads = 0; // Manual worker count, or 0 for Auto.
        std::uint64_t memoryBudgetBytes = 0; // Manual memory budget, or 0 for Auto.

        // Field-wise equality, so the settings file can tell whether the caps changed.
        bool operator==(const ResourceSettings&) const = default;
    };

    // Design targets that Auto aims for; tests and benchmarks can change them.
    struct CapTargets {
        // Workers wanted when processors allow. 8 is the design machine; bigger machines get more until the
        // disk is the limit, and 32 keeps per-worker buffers within the memory budget.
        std::uint32_t workerThreads = 32;
        std::uint64_t memoryBudgetBytes = 8ull << 30; // 8 GB, enough for a 30 TB / 20 M-file snapshot.
        std::uint32_t availableMemoryPercent = 60; // Share of available RAM Auto may use.
    };

    // Why a cap has its value, so the UI can explain it ("limited by available RAM").
    enum class CapReason : std::uint8_t {
        Target, // Auto reached the design target.
        AvailableProcessors, // Auto was limited by the processors this process may use.
        AvailableMemory, // Auto was limited by free physical memory.
        Manual, // The user's value, within what was detected.
        ManualCapped, // The user's value was higher than the machine offers and was lowered.
    };

    // What jobs may use: thread pool size and memory budget, with the reason for each.
    struct ResourceCaps {
        std::uint32_t workerThreads = 1; // Worker threads in the pool (>= 1).
        CapReason workerReason = CapReason::Target; // Why workerThreads has its value.
        std::uint64_t memoryBudgetBytes = 0; // Memory a job may plan to use.
        CapReason memoryReason = CapReason::Target; // Why memoryBudgetBytes has its value.
    };

    // Applies plan.md 6.6: workers = min(target, logical processors - 1), at least 1 (one processor stays free
    // for the UI and the disk reader); memory = min(target, 60% of available). Manual values are used as given
    // but never above what was detected. Pure, so the same inputs always give the same caps.
    ResourceCaps computeCaps(const DetectedResources& detected, const ResourceSettings& settings,
                             const CapTargets& targets = {});

    // One line for the Progress tab and the About box, e.g.
    // "6 of 8 threads, memory budget 5.1 GB (limited by available RAM)".
    std::string describeCaps(const DetectedResources& detected, const ResourceCaps& caps);

} // namespace icd::core
