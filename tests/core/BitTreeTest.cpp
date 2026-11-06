// The bit tree behind the head-order ready set (T133): next and prev agree with std::set for sizes around word and
// level boundaries.

#include "core/util/BitTree.h"

#include <gtest/gtest.h>

#include <random>
#include <set>

namespace icd::core { namespace {

    // Checks next() and prev() at every index against a std::set.
    void expectSame(const BitTree& tree, const std::set<std::size_t>& set, std::size_t size) {
        ASSERT_EQ(tree.empty(), set.empty());
        for (std::size_t i = 0; i < size; ++i) {
            const auto after = set.lower_bound(i);
            EXPECT_EQ(tree.next(i), after == set.end() ? BitTree::npos : *after) << "next " << i;
            const auto upTo = set.upper_bound(i);
            EXPECT_EQ(tree.prev(i), upTo == set.begin() ? BitTree::npos : *std::prev(upTo)) << "prev " << i;
            EXPECT_EQ(tree.contains(i), set.contains(i));
        }
    }

    TEST(BitTree, MatchesASetAcrossSizes) {
        std::mt19937 random(7);
        for (const std::size_t size : {1U, 2U, 63U, 64U, 65U, 4095U, 4096U, 4097U, 9000U}) {
            BitTree tree(size);
            std::set<std::size_t> set;
            expectSame(tree, set, size);
            std::uniform_int_distribution<std::size_t> pick(0, size - 1);
            for (int round = 0; round < 4; ++round) {
                for (std::size_t k = 0; k < size / 3 + 1; ++k) {
                    const std::size_t i = pick(random);
                    if (random() % 3 == 0) {
                        tree.erase(i);
                        set.erase(i);
                    }
                    else {
                        tree.insert(i);
                        set.insert(i);
                    }
                }
                expectSame(tree, set, size);
            }
        }
    }

    TEST(BitTree, ErasingTheLastMemberEmptiesIt) {
        BitTree tree(5000);
        tree.insert(4999);
        tree.insert(0);
        tree.erase(0);
        EXPECT_EQ(tree.next(0), 4999U);
        tree.erase(4999);
        EXPECT_TRUE(tree.empty());
        EXPECT_EQ(tree.next(0), BitTree::npos);
        EXPECT_EQ(tree.prev(4999), BitTree::npos);
        EXPECT_TRUE(BitTree(0).empty());
    }

}} // namespace icd::core
