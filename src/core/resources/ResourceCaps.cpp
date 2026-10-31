#include "core/resources/ResourceCaps.h"

#include "core/format/ByteSize.h"

#include <algorithm>
#include <format>

namespace icd::core {

    namespace {

        // Auto or manual worker count for the detected processors.
        void capWorkers(const DetectedResources& detected, const ResourceSettings& settings, const CapTargets& targets,
                        ResourceCaps& caps) {
            const std::uint32_t processors = std::max<std::uint32_t>(detected.logicalProcessors, 1);
            if (settings.workerThreads != 0) {
                caps.workerThreads = std::min(settings.workerThreads, processors);
                caps.workerReason = settings.workerThreads > processors ? CapReason::ManualCapped : CapReason::Manual;
                return;
            }
            const std::uint32_t spare = std::max<std::uint32_t>(processors - 1, 1);
            const std::uint32_t target = std::max<std::uint32_t>(targets.workerThreads, 1);
            caps.workerThreads = std::min(target, spare);
            caps.workerReason = target <= spare ? CapReason::Target : CapReason::AvailableProcessors;
        }

        // Auto or manual memory budget for the detected memory.
        void capMemory(const DetectedResources& detected, const ResourceSettings& settings, const CapTargets& targets,
                       ResourceCaps& caps) {
            if (settings.memoryBudgetBytes != 0) {
                caps.memoryBudgetBytes = std::min(settings.memoryBudgetBytes, detected.availableMemoryBytes);
                caps.memoryReason = settings.memoryBudgetBytes > detected.availableMemoryBytes ? CapReason::ManualCapped
                                                                                               : CapReason::Manual;
                return;
            }
            const std::uint64_t share = detected.availableMemoryBytes / 100 * targets.availableMemoryPercent;
            caps.memoryBudgetBytes = std::min(targets.memoryBudgetBytes, share);
            caps.memoryReason = targets.memoryBudgetBytes <= share ? CapReason::Target : CapReason::AvailableMemory;
        }

        // Explanation suffix for one cap; empty when Auto simply reached its target.
        std::string reasonText(CapReason reason, const char* limitedBy) {
            switch (reason) {
            case CapReason::Target:
                return {};
            case CapReason::AvailableProcessors:
            case CapReason::AvailableMemory:
                return std::format(" (limited by {})", limitedBy);
            case CapReason::Manual:
                return " (manual)";
            case CapReason::ManualCapped:
                return std::format(" (manual, lowered to {})", limitedBy);
            }
            return {};
        }

    } // namespace

    ResourceCaps computeCaps(const DetectedResources& detected, const ResourceSettings& settings,
                             const CapTargets& targets) {
        ResourceCaps caps;
        capWorkers(detected, settings, targets, caps);
        capMemory(detected, settings, targets, caps);
        return caps;
    }

    std::string describeCaps(const DetectedResources& detected, const ResourceCaps& caps) {
        return std::format("{} of {} threads{}, memory budget {}{}", caps.workerThreads, detected.logicalProcessors,
                           reasonText(caps.workerReason, "available processors"), formatBytes(caps.memoryBudgetBytes),
                           reasonText(caps.memoryReason, "available RAM"));
    }

} // namespace icd::core
