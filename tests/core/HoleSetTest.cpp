// HoleSet (T082): random takes, adds and fit queries against a per-cluster model, across block splits and merges.

#include "core/planner/HoleSet.h"
#include "core/synth/SynthRandom.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace icd::core::planner { namespace {

    // Free runs of a per-cluster model.
    std::vector<ClusterRun> runsOf(const std::vector<bool>& free) {
        std::vector<ClusterRun> out;
        for (std::size_t i = 0; i < free.size();) {
            if (!free[i]) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < free.size() && free[j]) {
                ++j;
            }
            out.push_back({i, j - i});
            i = j;
        }
        return out;
    }

    // First (or last) model run clipped to window with at least n clusters.
    std::optional<ClusterRun> modelFit(const std::vector<bool>& free, ClusterCount n, ClusterRun window, bool up) {
        std::optional<ClusterRun> found;
        for (const ClusterRun& r : runsOf(free)) {
            const Lcn start = std::max(r.start, window.start);
            const Lcn end = std::min(r.end(), window.end());
            if (end > start && end - start >= n) {
                found = ClusterRun{start, end - start};
                if (up) {
                    return found;
                }
            }
        }
        return found;
    }

    // Whether every cluster of run is free in the model.
    bool modelFree(const std::vector<bool>& free, ClusterRun run) {
        for (Lcn l = run.start; l < run.end(); ++l) {
            if (l >= free.size() || !free[l]) {
                return false;
            }
        }
        return true;
    }

    // Whether no cluster of run is free in the model.
    bool modelUsed(const std::vector<bool>& free, ClusterRun run) {
        for (Lcn l = run.start; l < run.end(); ++l) {
            if (l < free.size() && free[l]) {
                return false;
            }
        }
        return true;
    }

    // Applies one random operation to both and compares.
    void step(SynthRandom& random, std::vector<bool>& model, HoleSet& set) {
        const ClusterCount size = model.size();
        const Lcn start = random.below(size);
        const ClusterRun run{start, 1 + random.below(std::min<ClusterCount>(40, size - start))};
        if (random.below(2) == 0) {
            const bool expect = modelFree(model, run);
            ASSERT_EQ(set.take(run), expect);
            for (Lcn l = run.start; expect && l < run.end(); ++l) {
                model[l] = false;
            }
        }
        else {
            const bool expect = modelUsed(model, run);
            ASSERT_EQ(set.add(run), expect);
            for (Lcn l = run.start; expect && l < run.end(); ++l) {
                model[l] = true;
            }
        }
    }

    TEST(HoleSet, RandomOperationsMatchAPerClusterModel) {
        SynthRandom random(11);
        for (int round = 0; round < 6; ++round) {
            const ClusterCount size = 20'000;
            std::vector<bool> model(size, false);
            std::vector<ClusterRun> initial;
            for (Lcn l = 0; l < size; l += 3 + random.below(5)) { // Thousands of short runs: many blocks.
                initial.push_back({l, std::min<ClusterCount>(1 + random.below(2), size - l)});
            }
            for (const ClusterRun& r : initial) {
                for (Lcn l = r.start; l < r.end() && l < size; ++l) {
                    model[l] = true;
                }
            }
            HoleSet set(initial);
            for (int op = 0; op < 4000; ++op) {
                step(random, model, set);
                if (op % 400 != 0) {
                    continue;
                }
                const auto got = set.runs();
                const auto want = runsOf(model);
                for (std::size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
                    ASSERT_EQ(got[i], want[i])
                        << "round " << round << " op " << op << " run " << i << ": got [" << got[i].start << ","
                        << got[i].end() << ") want [" << want[i].start << "," << want[i].end() << ")";
                }
                ASSERT_EQ(got.size(), want.size());
                const Lcn ws = random.below(size);
                const ClusterRun window{ws, 1 + random.below(size - ws)};
                for (ClusterCount n : {1ULL, 2ULL, 3ULL, 8ULL, 40ULL}) {
                    EXPECT_EQ(set.nextFit(n, window), modelFit(model, n, window, true)) << n;
                    EXPECT_EQ(set.prevFit(n, window), modelFit(model, n, window, false)) << n;
                }
            }
            const auto runs = runsOf(model);
            ClusterCount total = 0;
            for (const ClusterRun& r : runs) {
                total += r.length;
            }
            EXPECT_EQ(set.total(), total);
            EXPECT_EQ(set.size(), runs.size());
        }
    }

    TEST(HoleSet, EmptiedBlocksStaySearchable) {
        // Thousands of one-cluster holes two apart: whole blocks empty when their holes are taken, then holes grow back
        // across the emptied blocks' ranges.
        const ClusterCount size = 40'000;
        std::vector<bool> model(size, false);
        std::vector<ClusterRun> initial;
        for (Lcn l = 0; l < size; l += 2) {
            initial.push_back({l, 1});
            model[l] = true;
        }
        HoleSet set(initial);
        SynthRandom random(3);
        for (Lcn l = 4000; l < 30'000; l += 2) { // Empties dozens of blocks.
            ASSERT_TRUE(set.take({l, 1}));
            model[l] = false;
        }
        for (int op = 0; op < 3000; ++op) {
            const Lcn start = 3000 + random.below(28'000);
            const ClusterRun run{start, 1 + random.below(300)};
            bool used = true;
            for (Lcn l = run.start; l < run.end(); ++l) {
                used = used && !model[l];
            }
            ASSERT_EQ(set.add(run), used) << op;
            for (Lcn l = run.start; used && l < run.end(); ++l) {
                model[l] = true;
            }
            const Lcn probe = random.below(size);
            const ClusterRun window{probe, std::min<ClusterCount>(size - probe, 1 + random.below(2000))};
            ASSERT_EQ(set.nextFit(5, window), modelFit(model, 5, window, true)) << op;
            ASSERT_EQ(set.prevFit(5, window), modelFit(model, 5, window, false)) << op;
            ASSERT_EQ(set.holeAt(probe).has_value(), model[probe]) << op;
        }
        EXPECT_EQ(set.runs(), runsOf(model));
    }

    TEST(HoleSet, MergesAcrossNeighboursAndClipsWindows) {
        HoleSet set(std::vector<ClusterRun>{{10, 5}, {0, 3}, {2, 4}}); // Unsorted and overlapping.
        EXPECT_EQ(set.runs(), (std::vector<ClusterRun>{{0, 6}, {10, 5}}));
        EXPECT_TRUE(set.add({6, 4})); // Joins both neighbours.
        EXPECT_EQ(set.runs(), (std::vector<ClusterRun>{{0, 15}}));
        EXPECT_FALSE(set.add({14, 2})); // Overlaps.
        EXPECT_TRUE(set.take({5, 2}));
        EXPECT_FALSE(set.take({4, 2}));
        EXPECT_EQ(set.holeAt(8), (ClusterRun{7, 8}));
        EXPECT_FALSE(set.holeAt(6));
        EXPECT_EQ(set.within({3, 10}), (std::vector<ClusterRun>{{3, 2}, {7, 6}}));
        EXPECT_EQ(set.nextFit(3, {3, 10}), (ClusterRun{7, 6}));
        EXPECT_EQ(set.prevFit(2, {0, 6}), (ClusterRun{0, 5}));
        EXPECT_EQ(set.prevFit(6, {0, 15}), (ClusterRun{7, 8}));
        EXPECT_FALSE(set.nextFit(9, {0, 15}));
        EXPECT_TRUE(set.contains({}));
        HoleSet empty;
        EXPECT_FALSE(empty.nextFit(1, {0, 100}));
        EXPECT_FALSE(empty.prevFit(1, {0, 100}));
        EXPECT_TRUE(empty.add({5, 5}));
        EXPECT_EQ(empty.total(), 5U);
    }

}} // namespace icd::core::planner
