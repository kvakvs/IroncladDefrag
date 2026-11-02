#include "core/planner/Budget.h"

#include <algorithm>
#include <format>
#include <functional>
#include <numeric>
#include <queue>

namespace icd::core::planner {

    namespace {

        // Benefit of one fragment removed, against one for a file reaching its zone and a little for compaction.
        constexpr double fragmentWeight = 4.0;

        // Benefit of a file reaching its zone.
        constexpr double zoneWeight = 2.0;

        // Benefit of closing part of a hole (compaction, sorting).
        constexpr double compactWeight = 1.0;

        // Every move's sources sorted by LCN, with the move.
        std::vector<std::pair<ClusterRun, std::uint32_t>> sourceIndex(const Plan& plan) {
            std::vector<std::pair<ClusterRun, std::uint32_t>> index;
            for (std::uint32_t i = 0; i < plan.moves.size(); ++i) {
                for (const ClusterRun& r : plan.sources(plan.moves[i])) {
                    index.emplace_back(r, i);
                }
            }
            std::ranges::sort(index, {}, [](const auto& e) { return e.first.start; });
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
        std::vector<std::vector<std::uint32_t>> dependencies(const Plan& plan) {
            const auto index = sourceIndex(plan);
            std::vector<std::vector<std::uint32_t>> deps(plan.moves.size());
            for (std::uint32_t j = 0; j < plan.moves.size(); ++j) {
                deps[j] = earlierOverlaps(plan, index, j);
            }
            return deps;
        }

        // A move's benefit per cluster.
        double perCluster(const Move& m) {
            return m.benefit / static_cast<double>(std::max<ClusterCount>(m.clusters, 1));
        }

        // Work under a budget.
        struct Cost {
            ClusterCount clusters = 0; // Clusters moved.
            double seconds = 0.0; // Estimated time.
        };

        // The selection of moves under a budget.
        class Selector {
        public:
            // Prepares costs.
            Selector(const Plan& plan, std::uint32_t bytesPerCluster, const BudgetOptions& budget) :
                plan_(plan), bytesPerCluster_(bytesPerCluster), budget_(budget), deps_(dependencies(plan)),
                taken_(plan.moves.size(), 0) {}

            // Takes moves by benefit per byte with their dependencies while the budget lasts.
            std::vector<std::uint8_t> select() {
                std::vector<std::uint32_t> order(plan_.moves.size());
                std::iota(order.begin(), order.end(), 0U);
                std::ranges::stable_sort(order, [&](std::uint32_t a, std::uint32_t b) { return value(a) > value(b); });
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
                std::vector<std::uint8_t> seen(plan_.moves.size(), 0);
                Cost cost;
                while (!work.empty()) {
                    const std::uint32_t u = work.back();
                    work.pop_back();
                    if (taken_[u] != 0 || seen[u] != 0) {
                        continue;
                    }
                    seen[u] = 1;
                    closure.push_back(u);
                    cost.clusters += plan_.moves[u].clusters;
                    cost.seconds += budget_.time.moveSeconds(plan_.moves[u].clusters, bytesPerCluster_);
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
            std::vector<std::vector<std::uint32_t>> deps_; // Earlier moves each depends on.
            std::vector<std::uint8_t> taken_; // Kept moves.
            Cost used_; // Work kept so far.
        };

        // Dependency edges among kept moves.
        struct KeptGraph {
            std::vector<std::vector<std::uint32_t>> next; // Kept moves waiting for each move.
            std::vector<std::uint32_t> waiting; // Kept moves each still waits for.
        };

        // The graph of the kept moves.
        KeptGraph keptGraph(const Plan& plan, const std::vector<std::uint8_t>& taken) {
            const auto deps = dependencies(plan);
            KeptGraph g{.next = std::vector<std::vector<std::uint32_t>>(plan.moves.size()),
                        .waiting = std::vector<std::uint32_t>(plan.moves.size(), 0)};
            for (std::uint32_t j = 0; j < plan.moves.size(); ++j) {
                if (taken[j] == 0) {
                    continue;
                }
                for (const std::uint32_t i : deps[j]) {
                    g.next[i].push_back(j);
                    ++g.waiting[j];
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
        std::vector<std::uint32_t> valueOrder(const Plan& plan, const std::vector<std::uint8_t>& taken) {
            KeptGraph g = keptGraph(plan, taken);
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

        // The plan rebuilt from kept moves in a new order, with deferred clusters and extra recounted.
        void rebuild(Plan& plan, const std::vector<std::uint8_t>& taken, const std::vector<std::uint32_t>& order) {
            Plan out = plan;
            out.moves.clear();
            out.runs.clear();
            out.extra = 0;
            for (const std::uint32_t v : order) {
                const Move& m = plan.moves[v];
                out.add(m, plan.sources(m), plan.destinations(m));
                out.extra += m.extra ? m.clusters : 0;
            }
            std::uint64_t deferred = 0;
            for (std::uint32_t v = 0; v < plan.moves.size(); ++v) {
                if (taken[v] == 0 && !plan.moves[v].extra) {
                    out.deferredClusters += plan.moves[v].clusters;
                    ++deferred;
                }
            }
            if (deferred != 0) {
                out.notes.push_back(std::format("The budget leaves {} moves for a later run.", deferred));
            }
            plan = std::move(out);
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
        const std::uint64_t before = fragmentsOf(movableRuns(snapshot, move.file));
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

    void applyBudget(const Snapshot& snapshot, Plan& plan, const BudgetOptions& budget) {
        for (Move& m : plan.moves) {
            m.benefit = moveBenefit(snapshot, plan, m);
        }
        const bool parks = std::ranges::any_of(plan.moves, [](const Move& m) { return m.reason == MoveReason::Park; });
        if (parks || (!budget.maxClusters && !budget.maxSeconds)) {
            return;
        }
        Selector selector(plan, snapshot.geometry.bytesPerCluster, budget);
        const auto taken = selector.select();
        rebuild(plan, taken, valueOrder(plan, taken));
        finishTotals(plan, snapshot);
    }

} // namespace icd::core::planner
