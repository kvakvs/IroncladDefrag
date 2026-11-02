// Cycle guard (T095b): placement learns which files wait for which, and a destination that would make a file wait for
// itself (a swap, a longer cycle) is reported before it is taken; cycleEnd() says where a later start avoids it.

#include "core/planner/CycleGuard.h"

#include "core/PlannerTestUtil.h"

#include <gtest/gtest.h>

namespace icd::core::planner { namespace {

    // One run as a span.
    std::span<const ClusterRun> one(const ClusterRun& r) { return {&r, 1}; }

    TEST(CycleGuard, SwapsAndLongerCyclesAreReported) {
        //  0 1 2 3 4 5    A, B, C one cluster each; free space at 3..5.
        //  A B C . . .
        const auto v = test::pictureVolume("ABC...");
        const FileIndex a = v.files.at('A');
        const FileIndex b = v.files.at('B');
        const FileIndex c = v.files.at('C');
        CycleGuard guard(v.snapshot);
        const ClusterRun atA{0, 1};
        const ClusterRun atB{1, 1};
        const ClusterRun atC{2, 1};
        const ClusterRun free{3, 1};
        EXPECT_FALSE(guard.closesCycle(a, one(free))); // Free space waits for nobody.
        EXPECT_FALSE(guard.closesCycle(a, one(atB)));
        guard.record(a, one(atB)); // A waits for B.
        EXPECT_TRUE(guard.closesCycle(b, one(atA))); // B onto A's place would swap them.
        EXPECT_FALSE(guard.closesCycle(b, one(atC)));
        guard.record(b, one(atC)); // B waits for C.
        EXPECT_TRUE(guard.closesCycle(c, one(atA))); // C onto A's place closes A -> B -> C -> A.
        EXPECT_FALSE(guard.closesCycle(c, one(free)));
    }

    TEST(CycleGuard, CycleEndSkipsPastTheFileThatClosesIt) {
        //  0 1 2 3 4 5 6    A waits for B (it takes B's place); B looking at [0, 3) would take A's place at 2.
        //  B . A A . . .
        const auto v = test::pictureVolume("B.AA...");
        const FileIndex a = v.files.at('A');
        const FileIndex b = v.files.at('B');
        CycleGuard guard(v.snapshot);
        const ClusterRun atB{0, 1};
        guard.record(a, one(atB));
        EXPECT_EQ(guard.cycleEnd(b, {1, 3}), std::optional<Lcn>(4)); // Past A's last cluster.
        EXPECT_EQ(guard.cycleEnd(b, {4, 2}), std::nullopt);
    }

    TEST(CycleGuard, OrderRepairKeepsFindingCycles) {
        // Waits recorded against the order the guard first gave the files still find the cycle they close.
        //  0 1 2 3 4 5 6 7
        //  A B C D . . . .
        const auto v = test::pictureVolume("ABCD....");
        const FileIndex a = v.files.at('A');
        const FileIndex b = v.files.at('B');
        const FileIndex c = v.files.at('C');
        const FileIndex d = v.files.at('D');
        CycleGuard guard(v.snapshot);
        guard.record(c, one({3, 1})); // C waits for D.
        guard.record(a, one({1, 1})); // A waits for B.
        guard.record(b, one({2, 1})); // B waits for C (B was ordered after C: repaired).
        EXPECT_TRUE(guard.closesCycle(d, one({0, 1}))); // D onto A: A -> B -> C -> D -> A.
        EXPECT_FALSE(guard.closesCycle(d, one({4, 1})));
    }

}} // namespace icd::core::planner
