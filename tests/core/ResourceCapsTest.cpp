#include "core/resources/ResourceCaps.h"

#include <gtest/gtest.h>

namespace icd::core { namespace {

    constexpr std::uint64_t GB = 1ull << 30;

    // Detected resources with the given processors and free memory (total is twice the free memory).
    DetectedResources machine(std::uint32_t processors, std::uint64_t availableBytes) {
        return {processors, processors, availableBytes * 2, availableBytes};
    }

    // The design machine: 8 processors leave 7 workers; 32 GB free allows the full 8 GB target.
    TEST(ComputeCaps, DesignMachine) {
        const ResourceCaps caps = computeCaps(machine(8, 32 * GB), {});
        EXPECT_EQ(caps.workerThreads, 7u);
        EXPECT_EQ(caps.workerReason, CapReason::AvailableProcessors);
        EXPECT_EQ(caps.memoryBudgetBytes, 8 * GB);
        EXPECT_EQ(caps.memoryReason, CapReason::Target);
    }

    // A single processor still gets one worker.
    TEST(ComputeCaps, OneProcessor) {
        const ResourceCaps caps = computeCaps(machine(1, 32 * GB), {});
        EXPECT_EQ(caps.workerThreads, 1u);
        EXPECT_EQ(caps.workerReason, CapReason::AvailableProcessors);
    }

    // Zero detected processors (a failed query) is treated as one.
    TEST(ComputeCaps, ZeroProcessorsTreatedAsOne) {
        const ResourceCaps caps = computeCaps(machine(0, 32 * GB), {});
        EXPECT_EQ(caps.workerThreads, 1u);
    }

    // 2 GB free: the budget is 60% of it, and the reason says why.
    TEST(ComputeCaps, LowMemory) {
        const ResourceCaps caps = computeCaps(machine(8, 2 * GB), {});
        EXPECT_EQ(caps.memoryBudgetBytes, 2 * GB / 100 * 60);
        EXPECT_EQ(caps.memoryReason, CapReason::AvailableMemory);
    }

    // 128 threads: Auto stops at the worker target instead of using all of them.
    TEST(ComputeCaps, ManyProcessorsStopAtTarget) {
        const ResourceCaps caps = computeCaps(machine(128, 256 * GB), {});
        EXPECT_EQ(caps.workerThreads, CapTargets{}.workerThreads);
        EXPECT_EQ(caps.workerReason, CapReason::Target);
        EXPECT_EQ(caps.memoryBudgetBytes, 8 * GB);
    }

    // Manual values within the machine are used as given, even above the Auto targets.
    TEST(ComputeCaps, ManualWithinLimits) {
        const ResourceCaps caps = computeCaps(machine(128, 256 * GB), {64, 16 * GB});
        EXPECT_EQ(caps.workerThreads, 64u);
        EXPECT_EQ(caps.workerReason, CapReason::Manual);
        EXPECT_EQ(caps.memoryBudgetBytes, 16 * GB);
        EXPECT_EQ(caps.memoryReason, CapReason::Manual);
    }

    // Manual values above the machine are lowered to what was detected.
    TEST(ComputeCaps, ManualAboveDetectedIsCapped) {
        const ResourceCaps caps = computeCaps(machine(4, 2 * GB), {16, 8 * GB});
        EXPECT_EQ(caps.workerThreads, 4u);
        EXPECT_EQ(caps.workerReason, CapReason::ManualCapped);
        EXPECT_EQ(caps.memoryBudgetBytes, 2 * GB);
        EXPECT_EQ(caps.memoryReason, CapReason::ManualCapped);
    }

    // Custom targets (benchmarks, tests) are respected.
    TEST(ComputeCaps, CustomTargets) {
        const ResourceCaps caps = computeCaps(machine(16, 64 * GB), {}, {4, 1 * GB, 50});
        EXPECT_EQ(caps.workerThreads, 4u);
        EXPECT_EQ(caps.memoryBudgetBytes, 1 * GB);
    }

    // The summary line names the limits that apply.
    TEST(DescribeCaps, ExplainsLimits) {
        const DetectedResources detected = machine(8, 2 * GB);
        const std::string text = describeCaps(detected, computeCaps(detected, {}));
        EXPECT_EQ(text,
                  "7 of 8 threads (limited by available processors), memory budget 1.20 GB "
                  "(limited by available RAM)");
    }

    // Nothing is added when Auto reached its targets.
    TEST(DescribeCaps, PlainWhenTargetsReached) {
        const DetectedResources detected = machine(128, 256 * GB);
        EXPECT_EQ(describeCaps(detected, computeCaps(detected, {})), "32 of 128 threads, memory budget 8.00 GB");
    }

}} // namespace icd::core
