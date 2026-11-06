// Ordering moves by disk head position (T133): the nearest read goes next, moves that share clusters or a file keep
// their order, and on random plans every move still finds its file and free destinations and the volume ends the same.

#include "core/execute/HeadOrder.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace icd::core::execute { namespace {

    // A move of file `file` from `from` to `to`.
    struct TestMove {
        std::uint64_t file = 0; // File reference.
        std::vector<ClusterRun> from; // Sources.
        std::vector<ClusterRun> to; // Destinations.
    };

    // A plan of the moves, in the order given; each path names its move's place in that order.
    ExecutionPlan planOf(const std::vector<TestMove>& moves) {
        ExecutionPlan plan;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            plan.add({.fileReference = moves[i].file}, moves[i].from, moves[i].to, std::to_string(i));
        }
        return plan;
    }

    // The original index of each move of an ordered plan (from its path).
    std::vector<int> originalOrder(const ExecutionPlan& plan) {
        std::vector<int> out;
        for (const std::string& path : plan.paths) {
            out.push_back(std::stoi(path));
        }
        return out;
    }

    // Owner of each cluster (0: free).
    using Volume = std::vector<std::uint64_t>;

    // Replays a plan on a volume; nullopt when a move does not find its file at its sources or free destinations.
    std::optional<Volume> replay(const ExecutionPlan& plan, Volume volume) {
        for (const PlannedMove& m : plan.moves) {
            for (const ClusterRun& r : plan.sources(m)) {
                for (Lcn c = r.start; c < r.end(); ++c) {
                    if (volume[c] != m.fileReference) {
                        return std::nullopt;
                    }
                    volume[c] = 0;
                }
            }
            for (const ClusterRun& r : plan.destinations(m)) {
                for (Lcn c = r.start; c < r.end(); ++c) {
                    if (volume[c] != 0) {
                        return std::nullopt;
                    }
                    volume[c] = m.fileReference;
                }
            }
        }
        return volume;
    }

    TEST(HeadOrder, NearestReadGoesNext) {
        // Independent moves planned in destination order; from cluster 0 the read at 10 is nearest, then the one at
        // 500, then 900.
        const ExecutionPlan plan =
            planOf({{1, {{900, 1}}, {{100, 1}}}, {2, {{10, 1}}, {{101, 1}}}, {3, {{500, 1}}, {{102, 1}}}});
        HeadOrderReport report;
        const ExecutionPlan ordered = orderForHead(plan, &report);
        EXPECT_EQ(originalOrder(ordered), (std::vector<int>{1, 2, 0}));
        EXPECT_EQ(report.travelBefore, 900U + 91U + 398U);
        EXPECT_EQ(report.travelAfter, 10U + 398U + 797U);
        EXPECT_EQ(headTravel(ordered), report.travelAfter);
    }

    TEST(HeadOrder, MovesIntoVacatedClustersWaitForThem) {
        // Y goes where X is: X must leave first, however near Y's read is. Z at 5 is free to go first.
        const ExecutionPlan plan =
            planOf({{1, {{10, 1}}, {{50, 1}}}, {2, {{6, 1}}, {{10, 1}}}, {3, {{5, 1}}, {{70, 1}}}});
        const ExecutionPlan ordered = orderForHead(plan);
        const std::vector<int> order = originalOrder(ordered);
        EXPECT_EQ(order, (std::vector<int>{2, 0, 1}));
        Volume volume(100, 0);
        volume[10] = 1;
        volume[6] = 2;
        volume[5] = 3;
        EXPECT_EQ(replay(ordered, volume), replay(plan, volume));
    }

    TEST(HeadOrder, RunsAndPathsFollowTheirMoves) {
        const ExecutionPlan plan = planOf({{1, {{900, 2}, {950, 1}}, {{100, 3}}}, {2, {{10, 1}}, {{200, 1}}}});
        const ExecutionPlan ordered = orderForHead(plan);
        ASSERT_EQ(ordered.moves.size(), 2U);
        EXPECT_EQ(ordered.paths[0], "1");
        EXPECT_EQ(ordered.moves[1].fileReference, 1U);
        EXPECT_EQ(ordered.moves[1].clusters, 3U);
        EXPECT_EQ(std::vector(ordered.sources(ordered.moves[1]).begin(), ordered.sources(ordered.moves[1]).end()),
                  (std::vector<ClusterRun>{{900, 2}, {950, 1}}));
        EXPECT_EQ(
            std::vector(ordered.destinations(ordered.moves[1]).begin(), ordered.destinations(ordered.moves[1]).end()),
            (std::vector<ClusterRun>{{100, 3}}));
        EXPECT_TRUE(orderForHead(ExecutionPlan{}).moves.empty());
    }

    // A random volume and a valid plan on it: files of 1-4 clusters, each move takes a file to free clusters (in up
    // to two pieces), some files move twice (like a park and its return).
    struct RandomPlan {
        Volume volume; // Before the plan.
        ExecutionPlan plan; // The moves, valid in this order.
    };

    // Free runs of a volume, in LCN order.
    std::vector<ClusterRun> freeRuns(const Volume& volume) {
        std::vector<ClusterRun> out;
        for (Lcn c = 0; c < volume.size(); ++c) {
            if (volume[c] != 0) {
                continue;
            }
            if (!out.empty() && out.back().end() == c) {
                ++out.back().length;
            }
            else {
                out.push_back({c, 1});
            }
        }
        return out;
    }

    // Free clusters for `count` clusters, starting from a random free run; empty when there are too few.
    std::vector<ClusterRun> takeFree(const Volume& volume, ClusterCount count, std::mt19937& random) {
        const std::vector<ClusterRun> runs = freeRuns(volume);
        if (runs.empty()) {
            return {};
        }
        std::vector<ClusterRun> out;
        for (std::size_t k = 0, i = random() % runs.size(); k < runs.size() && count > 0;
             ++k, i = (i + 1) % runs.size()) {
            const ClusterCount take = std::min(count, runs[i].length);
            out.push_back({runs[i].start, take});
            count -= take;
        }
        return count == 0 ? out : std::vector<ClusterRun>{};
    }

    // The runs a file holds now, in LCN order.
    std::vector<ClusterRun> runsOf(const Volume& volume, std::uint64_t file) {
        std::vector<ClusterRun> out;
        for (Lcn c = 0; c < volume.size(); ++c) {
            if (volume[c] != file) {
                continue;
            }
            if (!out.empty() && out.back().end() == c) {
                ++out.back().length;
            }
            else {
                out.push_back({c, 1});
            }
        }
        return out;
    }

    // Builds a random plan by moving random files to random free space, one move at a time.
    RandomPlan randomPlan(unsigned seed) {
        std::mt19937 random(seed);
        RandomPlan out{.volume = Volume(400, 0), .plan = {}};
        std::uint64_t files = 0;
        for (Lcn c = 0; c + 4 < out.volume.size(); c += 1 + random() % 5) {
            if (random() % 3 != 0) {
                const ClusterCount size = 1 + random() % 4;
                ++files;
                std::fill_n(out.volume.begin() + static_cast<std::ptrdiff_t>(c), size, files);
                c += size;
            }
        }
        Volume now = out.volume;
        std::vector<TestMove> moves;
        for (int k = 0; k < 120; ++k) {
            const std::uint64_t file = 1 + random() % files;
            const std::vector<ClusterRun> from = runsOf(now, file);
            const std::vector<ClusterRun> to = takeFree(now, planner::clustersOf(from), random);
            if (to.empty()) {
                continue;
            }
            moves.push_back({file, from, to});
            const ExecutionPlan one = planOf({moves.back()});
            now = *replay(one, now);
        }
        out.plan = planOf(moves);
        return out;
    }

    TEST(HeadOrder, RandomPlansStayValidAndEndTheSame) {
        int reordered = 0; // Plans whose order changed: the check above is not met by keeping plan order.
        std::uint64_t before = 0; // Head travel over all plans in plan order.
        std::uint64_t after = 0; // The same ordered by head position.
        for (unsigned seed = 1; seed <= 40; ++seed) {
            const RandomPlan r = randomPlan(seed);
            const std::optional<Volume> planned = replay(r.plan, r.volume);
            ASSERT_TRUE(planned.has_value()) << "seed " << seed;
            HeadOrderReport report;
            const ExecutionPlan ordered = orderForHead(r.plan, &report);
            ASSERT_EQ(ordered.moves.size(), r.plan.moves.size());
            std::vector<int> order = originalOrder(ordered);
            reordered += std::ranges::is_sorted(order) ? 0 : 1;
            before += report.travelBefore;
            after += report.travelAfter;
            std::ranges::sort(order);
            for (int i = 0; i < static_cast<int>(order.size()); ++i) {
                ASSERT_EQ(order[static_cast<std::size_t>(i)], i) << "seed " << seed; // Each move once.
            }
            EXPECT_EQ(replay(ordered, r.volume), planned) << "seed " << seed;
            EXPECT_EQ(report.travelBefore, headTravel(r.plan));
        }
        EXPECT_GT(reordered, 30);
        EXPECT_LT(after, before / 2); // Random plans jump all over; nearest-first cuts that by more than half.
    }

}} // namespace icd::core::execute
