#include "core/planner/Budget.h"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <queue>
#include <span>
#include <utility>

namespace icd::core::planner {

    namespace {

        // Benefit of one fragment removed, against one for a file reaching its zone and a little for compaction.
        constexpr double fragmentWeight = 4.0;

        // Benefit of a file reaching its zone.
        constexpr double zoneWeight = 2.0;

        // Benefit of closing part of a hole (compaction, sorting).
        constexpr double compactWeight = 1.0;

        // Every move's sources sorted by LCN, with the move (ties in any order: users sort what they find).
        std::vector<std::pair<ClusterRun, std::uint32_t>> sourceIndex(const Plan& plan, Executor& executor) {
            std::vector<std::pair<ClusterRun, std::uint32_t>> index;
            index.reserve(plan.runs.size() / 2);
            for (std::uint32_t i = 0; i < plan.moves.size(); ++i) {
                for (const ClusterRun& r : plan.sources(plan.moves[i])) {
                    index.emplace_back(r, i);
                }
            }
            parallelSort(executor, index, [](const auto& a, const auto& b) { return a.first.start < b.first.start; });
            return index;
        }

        // The earlier moves whose sources move j's destination overlaps, once each.
        std::vector<std::uint32_t> earlierOverlaps(const Plan& plan,
                                                   const std::vector<std::pair<ClusterRun, std::uint32_t>>& index,
                                                   std::uint32_t j) {
            std::vector<std::uint32_t> out;
            for (const ClusterRun& d : plan.destinations(plan.moves[j])) {
                auto it = std::ranges::upper_bound(index, d.start, {}, [](const auto& e) { return e.first.end(); });
                for (; it != index.end() && it->first.start < d.end(); ++it) {
                    out.push_back(it->second);
                }
            }
            std::erase_if(out, [&](std::uint32_t i) { return i >= j; });
            std::ranges::sort(out);
            out.erase(std::ranges::unique(out).begin(), out.end());
            return out;
        }

        // For each move, the earlier moves whose sources its destination overlaps.
        std::vector<std::vector<std::uint32_t>> dependencies(const Plan& plan, Executor& executor) {
            const auto index = sourceIndex(plan, executor);
            std::vector<std::vector<std::uint32_t>> deps(plan.moves.size());
            executor.parallelFor({.count = plan.moves.size(), .grain = 4096}, [&](std::size_t begin, std::size_t end) {
                for (std::size_t j = begin; j < end; ++j) {
                    deps[j] = earlierOverlaps(plan, index, static_cast<std::uint32_t>(j));
                }
            });
            return deps;
        }

        // A move's benefit per cluster.
        double perCluster(const Move& m) {
            return m.benefit / static_cast<double>(std::max<ClusterCount>(m.clusters, 1));
        }

        // A list per move in one array (offsets into `items`), so millions of moves need two allocations.
        struct MoveLists {
            std::vector<std::size_t> offsets{0}; // Per move: where its items start; one past the last at the end.
            std::vector<std::uint32_t> items{}; // Every move's items, move after move.

            // One move's list.
            [[nodiscard]] std::span<const std::uint32_t> operator[](std::size_t v) const {
                return std::span(items).subspan(offsets[v], offsets[v + 1] - offsets[v]);
            }
        };

        // Work under a budget.
        struct Cost {
            ClusterCount clusters = 0; // Clusters moved.
            double seconds = 0.0; // Estimated time.
        };

        // The selection of moves under a budget.
        class Selector {
        public:
            // Prepares costs; deps holds every move's dependencies.
            Selector(const Plan& plan, std::uint32_t bytesPerCluster, const BudgetOptions& budget,
                     const std::vector<std::vector<std::uint32_t>>& deps) :
                plan_(plan), bytesPerCluster_(bytesPerCluster), budget_(budget), deps_(deps),
                taken_(plan.moves.size(), 0), seen_(plan.moves.size(), 0) {}

            // Takes moves by benefit per byte with their dependencies while the budget lasts. Ties go to the earlier
            // move, so the order is total and any sort gives it.
            std::vector<std::uint8_t> select(Executor& executor) {
                std::vector<std::uint32_t> order(plan_.moves.size());
                std::iota(order.begin(), order.end(), 0U);
                parallelSort(executor, order, [&](std::uint32_t a, std::uint32_t b) {
                    return value(a) != value(b) ? value(a) > value(b) : a < b;
                });
                for (const std::uint32_t v : order) {
                    tryTake(v);
                }
                return taken_;
            }

        private:
            // Benefit per cluster.
            [[nodiscard]] double value(std::uint32_t v) const { return perCluster(plan_.moves[v]); }

            // Takes a move with every dependency not taken yet, if they fit the budget together.
            void tryTake(std::uint32_t v) {
                if (taken_[v] != 0 || plan_.moves[v].extra) {
                    return; // Extra moves come only with the moves that need them.
                }
                std::vector<std::uint32_t> closure;
                std::vector<std::uint32_t> work{v};
                ++stamp_;
                Cost cost;
                while (!work.empty()) {
                    const std::uint32_t u = work.back();
                    work.pop_back();
                    if (taken_[u] != 0 || seen_[u] == stamp_) {
                        continue;
                    }
                    seen_[u] = stamp_;
                    closure.push_back(u);
                    cost.clusters += plan_.moves[u].clusters;
                    cost.seconds += budget_.time.moveSeconds(plan_.moves[u].clusters, bytesPerCluster_);
                    if (!fits(cost)) {
                        return; // Already over: no need to walk the rest of the closure.
                    }
                    work.insert(work.end(), deps_[u].begin(), deps_[u].end());
                }
                if (!fits(cost)) {
                    return;
                }
                used_.clusters += cost.clusters;
                used_.seconds += cost.seconds;
                for (const std::uint32_t u : closure) {
                    taken_[u] = 1;
                }
            }

            // Whether more work fits the budget.
            [[nodiscard]] bool fits(Cost more) const {
                const bool clustersFit = !budget_.maxClusters || used_.clusters + more.clusters <= *budget_.maxClusters;
                const bool timeFits = !budget_.maxSeconds || used_.seconds + more.seconds <= *budget_.maxSeconds;
                return clustersFit && timeFits;
            }

            const Plan& plan_; // The ordered plan.
            std::uint32_t bytesPerCluster_ = 4096; // Cluster size.
            const BudgetOptions& budget_; // Limits.
            const std::vector<std::vector<std::uint32_t>>& deps_; // Earlier moves each depends on.
            std::vector<std::uint8_t> taken_; // Kept moves.
            Cost used_; // Work kept so far.
            std::vector<std::uint32_t> seen_; // Per move: the closure walk that last visited it.
            std::uint32_t stamp_ = 0; // The current closure walk.
        };

        // Dependency edges among kept moves.
        struct KeptGraph {
            MoveLists next; // Kept moves waiting for each move, in move order.
            std::vector<std::uint32_t> waiting; // Kept moves each still waits for.
        };

        // Move j's dependencies when it is kept, else none.
        std::span<const std::uint32_t> keptDeps(const std::vector<std::uint8_t>& taken,
                                                const std::vector<std::vector<std::uint32_t>>& deps, std::uint32_t j) {
            return taken[j] != 0 ? std::span(deps[j]) : std::span<const std::uint32_t>();
        }

        // The graph of the kept moves.
        KeptGraph keptGraph(const Plan& plan, const std::vector<std::uint8_t>& taken,
                            const std::vector<std::vector<std::uint32_t>>& deps) {
            const std::size_t n = plan.moves.size();
            KeptGraph g{.next = {}, .waiting = std::vector<std::uint32_t>(n, 0)};
            g.next.offsets.assign(n + 1, 0);
            for (std::uint32_t j = 0; j < n; ++j) {
                for (const std::uint32_t i : keptDeps(taken, deps, j)) {
                    ++g.next.offsets[i + 1];
                    ++g.waiting[j];
                }
            }
            for (std::size_t i = 0; i < n; ++i) {
                g.next.offsets[i + 1] += g.next.offsets[i];
            }
            g.next.items.resize(g.next.offsets.back());
            std::vector<std::size_t> at(g.next.offsets.begin(), g.next.offsets.end() - 1);
            for (std::uint32_t j = 0; j < n; ++j) {
                for (const std::uint32_t i : keptDeps(taken, deps, j)) {
                    g.next.items[at[i]++] = j;
                }
            }
            return g;
        }

        // Max-heap order of ready moves: the higher value per cluster, then the earlier move.
        struct ByValue {
            const Plan* plan = nullptr; // The plan.

            // Whether a ranks below b.
            bool operator()(std::uint32_t a, std::uint32_t b) const {
                const double va = perCluster(plan->moves[a]);
                const double vb = perCluster(plan->moves[b]);
                return va != vb ? va < vb : a > b;
            }
        };

        // The kept moves ordered with the most valuable ready move first (a topological order over the kept set).
        std::vector<std::uint32_t> valueOrder(const Plan& plan, const std::vector<std::uint8_t>& taken,
                                              const std::vector<std::vector<std::uint32_t>>& deps) {
            KeptGraph g = keptGraph(plan, taken, deps);
            std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, ByValue> ready(ByValue{&plan});
            for (std::uint32_t v = 0; v < plan.moves.size(); ++v) {
                if (taken[v] != 0 && g.waiting[v] == 0) {
                    ready.push(v);
                }
            }
            std::vector<std::uint32_t> out;
            while (!ready.empty()) {
                const std::uint32_t v = ready.top();
                ready.pop();
                out.push_back(v);
                for (const std::uint32_t w : g.next[v]) {
                    if (--g.waiting[w] == 0) {
                        ready.push(w);
                    }
                }
            }
            return out;
        }

        // The plan rebuilt from kept moves in a new order, with deferred clusters and extra recounted (its moves and
        // runs are taken out first, so the rest of the plan is not copied).
        void rebuild(Plan& plan, const std::vector<std::uint8_t>& taken, const std::vector<std::uint32_t>& order) {
            const std::vector<Move> moves = std::exchange(plan.moves, {});
            const std::vector<ClusterRun> runs = std::exchange(plan.runs, {});
            plan.moves.reserve(order.size());
            plan.runs.reserve(runs.size());
            plan.extra = 0;
            for (const std::uint32_t v : order) {
                const Move& m = moves[v];
                plan.add(m, std::span(runs).subspan(m.firstSource, m.sourceCount),
                         std::span(runs).subspan(m.firstDestination, m.destinationCount));
                plan.extra += m.extra ? m.clusters : 0;
            }
            std::uint64_t deferred = 0;
            for (std::uint32_t v = 0; v < moves.size(); ++v) {
                if (taken[v] == 0 && !moves[v].extra) {
                    plan.deferredClusters += moves[v].clusters;
                    ++deferred;
                }
            }
            if (deferred != 0) {
                plan.notes.push_back(std::format("The budget leaves {} moves for a later run.", deferred));
            }
        }

    } // namespace

    double TimeModel::moveSeconds(ClusterCount clusters, std::uint32_t bytesPerCluster) const {
        const double bytes = static_cast<double>(clusters) * bytesPerCluster;
        return 2.0 * bytes / std::max(bytesPerSecond, 1.0) + seekSeconds;
    }

    double moveBenefit(const Snapshot& snapshot, const Plan& plan, const Move& move) {
        if (move.extra || move.reason == MoveReason::Park) {
            return 0.0;
        }
        const std::uint64_t before = movableFragments(snapshot, move.file);
        const std::uint64_t after = fragmentsOf(plan.destinations(move));
        const double removed = before > after ? static_cast<double>(before - after) : 0.0;
        const double zone = move.reason == MoveReason::Relocate ? zoneWeight : 0.0;
        return fragmentWeight * removed + zone + compactWeight;
    }

    double planSeconds(const Plan& plan, std::uint32_t bytesPerCluster, const TimeModel& time) {
        double seconds = 0.0;
        for (const Move& m : plan.moves) {
            seconds += time.moveSeconds(m.clusters, bytesPerCluster);
        }
        return seconds;
    }

    void applyBudget(const Snapshot& snapshot, Plan& plan, const BudgetOptions& budget, Executor* executor) {
        InlineExecutor inline_;
        Executor& run = executor != nullptr ? *executor : inline_;
        run.parallelFor({.count = plan.moves.size(), .grain = 1U << 14U}, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                plan.moves[i].benefit = moveBenefit(snapshot, plan, plan.moves[i]);
            }
        });
        const bool parks = std::ranges::any_of(plan.moves, [](const Move& m) { return m.reason == MoveReason::Park; });
        if (parks || (!budget.maxClusters && !budget.maxSeconds)) {
            return;
        }
        const auto deps = dependencies(plan, run);
        Selector selector(plan, snapshot.geometry.bytesPerCluster, budget, deps);
        const auto taken = selector.select(run);
        rebuild(plan, taken, valueOrder(plan, taken, deps));
        finishTotals(plan, snapshot);
    }

} // namespace icd::core::planner
