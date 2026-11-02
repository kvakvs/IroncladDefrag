// Matching a file's clusters against a move (T101): pieces in file order with their stream and VCN, moves cut short
// recognised piece by piece, changed files refused, and a parked file's return remapped from wherever it lies.

#include "core/execute/MovePieces.h"

#include <gtest/gtest.h>

namespace icd::core::execute { namespace {

    // Runs of one move: sources then destinations.
    struct Runs {
        std::vector<ClusterRun> sources; // Before.
        std::vector<ClusterRun> destinations; // After.

        // The move as matchMove() takes it.
        [[nodiscard]] MoveRuns view() const { return {sources, destinations}; }
    };

    TEST(MovePieces, FileAtItsSourcesIsAllPending) {
        const Runs m{{{10, 4}, {20, 2}}, {{100, 6}}};
        const std::vector<StreamRun> now{{0, 0, 10, 4}, {0, 4, 20, 2}};
        const auto pieces = matchMove(now, m.view());
        ASSERT_TRUE(pieces.has_value());
        EXPECT_EQ(*pieces, (std::vector<MovePiece>{{0, 0, 10, 100, 4, false}, {0, 4, 20, 104, 2, false}}));
        EXPECT_FALSE(allDone(*pieces));
        EXPECT_EQ(pendingClusters(*pieces), 6U);
    }

    TEST(MovePieces, StreamsKeepTheirVcns) {
        const Runs m{{{10, 3}, {50, 2}}, {{200, 5}}};
        const std::vector<StreamRun> now{{0, 0, 10, 3}, {1, 0, 50, 2}};
        const auto pieces = matchMove(now, m.view());
        ASSERT_TRUE(pieces.has_value());
        EXPECT_EQ(*pieces, (std::vector<MovePiece>{{0, 0, 10, 200, 3, false}, {1, 0, 50, 203, 2, false}}));
    }

    TEST(MovePieces, MoveCutShortIsRecognised) {
        const Runs m{{{10, 8}}, {{100, 8}}};
        // The first 5 clusters were moved before a crash.
        const std::vector<StreamRun> now{{0, 0, 100, 5}, {0, 5, 15, 3}};
        const auto pieces = matchMove(now, m.view());
        ASSERT_TRUE(pieces.has_value());
        EXPECT_EQ(*pieces, (std::vector<MovePiece>{{0, 0, 10, 100, 5, true}, {0, 5, 15, 105, 3, false}}));
        EXPECT_EQ(pendingClusters(*pieces), 3U);
        const std::vector<StreamRun> finished{{0, 0, 100, 8}};
        const auto done = matchMove(finished, m.view());
        ASSERT_TRUE(done.has_value());
        EXPECT_TRUE(allDone(*done));
    }

    TEST(MovePieces, ChangedFilesAreRefused) {
        const Runs m{{{10, 4}}, {{100, 4}}};
        EXPECT_FALSE(matchMove(std::vector<StreamRun>{{0, 0, 10, 3}, {0, 3, 40, 1}}, m.view())); // A cluster elsewhere.
        EXPECT_FALSE(matchMove(std::vector<StreamRun>{{0, 0, 10, 5}}, m.view())); // Grew.
        EXPECT_FALSE(matchMove(std::vector<StreamRun>{{0, 0, 10, 3}}, m.view())); // Shrank.
        EXPECT_FALSE(matchMove(std::vector<StreamRun>{{0, 0, 10, 4}}, Runs{{{10, 4}}, {{100, 3}}}.view()));
    }

    TEST(MovePieces, EmptyMoveIsDone) {
        const auto pieces = matchMove({}, Runs{}.view());
        ASSERT_TRUE(pieces.has_value());
        EXPECT_TRUE(pieces->empty());
        EXPECT_TRUE(allDone(*pieces));
    }

    TEST(MovePieces, RemapStartsFromWhereTheFileIs) {
        const std::vector<StreamRun> now{{0, 0, 70, 2}, {0, 2, 90, 2}};
        const std::vector<ClusterRun> destinations{{300, 4}};
        const auto pieces = remapMove(now, destinations);
        ASSERT_TRUE(pieces.has_value());
        EXPECT_EQ(*pieces, (std::vector<MovePiece>{{0, 0, 70, 300, 2, false}, {0, 2, 90, 302, 2, false}}));
        EXPECT_FALSE(remapMove(now, std::vector<ClusterRun>{{300, 5}}));
    }

    TEST(MovePieces, LcnRunsJoinWhatContinuesOnDisk) {
        const std::vector<StreamRun> runs{{0, 0, 10, 2}, {0, 2, 12, 3}, {1, 0, 15, 1}, {1, 1, 30, 1}};
        EXPECT_EQ(lcnRunsOf(runs), (std::vector<ClusterRun>{{10, 6}, {30, 1}}));
        EXPECT_EQ(asStreamRuns(std::vector<ClusterRun>{{5, 2}, {9, 1}}),
                  (std::vector<StreamRun>{{0, 0, 5, 2}, {0, 2, 9, 1}}));
    }

}} // namespace icd::core::execute
