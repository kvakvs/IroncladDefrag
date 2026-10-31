#include "platform/win/SystemResources.h"

#include <gtest/gtest.h>

#include <windows.h>

namespace icd::platform { namespace {

    // Detection on the test machine returns plausible numbers.
    TEST(SystemResources, DetectsPlausibleValues) {
        const core::DetectedResources detected = detectSystemResources();
        EXPECT_GE(detected.logicalProcessors, 1u);
        EXPECT_LE(detected.logicalProcessors, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        EXPECT_GE(detected.physicalCores, 1u);
        EXPECT_LE(detected.physicalCores, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
        EXPECT_GT(detected.availableMemoryBytes, 0u);
        EXPECT_LE(detected.availableMemoryBytes, detected.totalMemoryBytes);
    }

    // The low-memory signal can be opened and queried (its value depends on the machine).
    TEST(SystemResources, LowMemorySignalQueries) {
        const LowMemorySignal signal;
        EXPECT_NO_FATAL_FAILURE((void)signal.signalled());
    }

}} // namespace icd::platform
