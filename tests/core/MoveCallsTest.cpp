// Move calls (T102): pieces of plain files cut into chunks, compressed files moved in whole units without reaching
// into the next piece, replacement destinations, and verification of where moved clusters landed.

#include "core/execute/MoveCalls.h"

#include <gtest/gtest.h>

namespace icd::core::execute { namespace {

    TEST(MoveCalls, PlainPiecesAreCutIntoChunks) {
        const std::vector<MovePiece> pieces{
            {0, 0, 10, 100, 10, false}, {0, 10, 30, 110, 3, true}, {1, 0, 50, 113, 4, false}};
        const auto calls = moveCalls(pieces, {.chunkClusters = 4, .compressed = false});
        EXPECT_EQ(calls,
                  (std::vector<MoveCall>{{0, 0, 4, 100, 4}, {0, 4, 4, 104, 4}, {0, 8, 2, 108, 2}, {1, 0, 4, 113, 4}}));
        EXPECT_EQ(allocatedOf(calls), 14U);
    }

    TEST(MoveCalls, CompressedFilesMoveWholeUnits) {
        // Units of 16 clusters, each compressed to 10 allocated clusters followed by a hole.
        std::vector<MovePiece> pieces;
        for (std::uint64_t u = 0; u < 4; ++u) {
            pieces.push_back({0, u * 16, 1000 + u * 37, 500 + u * 10, 10, false});
        }
        const auto calls = moveCalls(pieces, {.chunkClusters = 32, .compressed = true});
        // Joined over the holes while the destination continues; cut at a unit boundary at the chunk size.
        EXPECT_EQ(calls, (std::vector<MoveCall>{{0, 0, 32, 500, 20}, {0, 32, 32, 520, 20}}));
    }

    TEST(MoveCalls, CompressedCallsStopBeforeTheNextPiece) {
        // The second half of unit 0 is already done: the first call may not round up over it.
        const std::vector<MovePiece> pieces{
            {0, 0, 1000, 500, 8, false}, {0, 8, 1008, 508, 8, true}, {0, 16, 1016, 900, 16, false}};
        const auto calls = moveCalls(pieces, {.chunkClusters = 4096, .compressed = true});
        EXPECT_EQ(calls, (std::vector<MoveCall>{{0, 0, 8, 500, 8}, {0, 16, 16, 900, 16}}));
    }

    TEST(MoveCalls, RetargetSendsCallsToConsecutiveClusters) {
        std::vector<MoveCall> calls{{0, 0, 4, 100, 4}, {0, 4, 32, 104, 20}};
        EXPECT_EQ(retarget(calls, 700), (std::vector<ClusterRun>{{700, 24}}));
        EXPECT_EQ(calls[0].to, 700U);
        EXPECT_EQ(calls[1].to, 704U);
    }

    TEST(MoveCalls, LandedPrefixOfARefusedCall) {
        // A call of 40 clusters from VCN 10 to LCN 500; NTFS moved the first 16 before refusing.
        const MoveCall call{0, 10, 40, 500, 40};
        const std::vector<StreamRun> now{{0, 0, 100, 10}, {0, 10, 500, 16}, {0, 26, 126, 24}};
        const LandedPrefix prefix = landedPrefix(now, call);
        EXPECT_EQ(prefix.allocated, 16U);
        EXPECT_EQ(prefix.vcnEnd, 26U);
        EXPECT_EQ(remainderOf(call, prefix), (MoveCall{0, 26, 24, 516, 24}));
        // Nothing moved: the remainder is the call.
        const std::vector<StreamRun> unmoved{{0, 0, 100, 50}};
        EXPECT_EQ(landedPrefix(unmoved, call).allocated, 0U);
        EXPECT_EQ(remainderOf(call, landedPrefix(unmoved, call)), call);
        // All of it: nothing left.
        const std::vector<StreamRun> all{{0, 0, 100, 10}, {0, 10, 500, 40}};
        EXPECT_EQ(landedPrefix(all, call).allocated, 40U);
    }

    TEST(MoveCalls, LandedChecksEveryStretch) {
        const std::vector<StreamRun> after{{0, 0, 100, 6}, {0, 16, 300, 10}, {1, 0, 50, 2}};
        EXPECT_TRUE(landed(after, std::vector<Landing>{{0, 0, 4, 100, 4}, {0, 4, 2, 104, 2}}));
        EXPECT_TRUE(landed(after, std::vector<Landing>{{0, 16, 16, 300, 10}, {1, 0, 2, 50, 2}}));
        EXPECT_FALSE(landed(after, std::vector<Landing>{{0, 0, 6, 101, 6}})); // Elsewhere.
        EXPECT_FALSE(landed(after, std::vector<Landing>{{0, 0, 32, 100, 16}})); // Not contiguous.
        EXPECT_FALSE(landed(after, std::vector<Landing>{{2, 0, 1, 50, 1}})); // No such stream.
    }

}} // namespace icd::core::execute
