#include "core/execute/HeadOrder.h"

#include "core/util/BitTree.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace icd::core::execute {

    namespace {

        // Where a move's first read starts (its first source in file order; a move without sources reads nothing,
        // so its first write stands in).
        Lcn firstRead(const ExecutionPlan& plan, const PlannedMove& move) {
            const auto sources = plan.sources(move);
            if (!sources.empty()) {
                return sources.front().start;
            }
            const auto destinations = plan.destinations(move);
            return destinations.empty() ? 0 : destinations.front().start;
        }

        // Where a move leaves the head: past its last write (in file order, the last piece is written last).
        Lcn lastWrite(const ExecutionPlan& plan, const PlannedMove& move) {
            const auto destinations = plan.destinations(move);
            if (!destinations.empty()) {
                return destinations.back().end();
            }
            const auto sources = plan.sources(move);
            return sources.empty() ? 0 : sources.back().end();
        }

        // Clusters between two positions.
        std::uint64_t distance(Lcn a, Lcn b) { return a > b ? a - b : b - a; }

        // One run of one move, for the overlap sweep (8 bytes: ten million moves stay cheap to sort).
        struct Access {
            std::uint32_t run = 0; // Index in ExecutionPlan::runs.
            std::uint32_t move = 0; // The move it belongs to.
        };

        // The moves that must keep their plan order: an edge from the earlier to the later of two moves whose runs
        // overlap, or that move the same file.
        class Hazards {
        public:
            // Finds every edge of a plan.
            Hazards(const ExecutionPlan& plan, Executor& executor) : plan_(plan), indegree_(plan.moves.size(), 0) {
                std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
                sweep(accessesOf(executor), edges);
                chainFiles(edges);
                build(edges);
            }

            // The moves that wait for v.
            [[nodiscard]] std::span<const std::uint32_t> after(std::uint32_t v) const {
                return std::span(items_).subspan(offsets_[v], offsets_[v + 1] - offsets_[v]);
            }

            // Per move: the edges into it.
            [[nodiscard]] const std::vector<std::uint32_t>& indegree() const { return indegree_; }

        private:
            // Every run of every move, sorted by start (then move and run, so the order has no ties).
            [[nodiscard]] std::vector<Access> accessesOf(Executor& executor) const {
                std::vector<Access> out;
                out.reserve(plan_.runs.size());
                for (std::uint32_t v = 0; v < plan_.moves.size(); ++v) {
                    addRuns(v, out);
                }
                parallelSort(executor, out, [&](const Access& a, const Access& b) {
                    const Lcn sa = plan_.runs[a.run].start;
                    const Lcn sb = plan_.runs[b.run].start;
                    return sa != sb ? sa < sb : (a.move != b.move ? a.move < b.move : a.run < b.run);
                });
                return out;
            }

            // Adds a move's source and destination runs that hold clusters.
            void addRuns(std::uint32_t move, std::vector<Access>& out) const {
                const PlannedMove& m = plan_.moves[move];
                const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> ranges{
                    {{m.firstSource, m.sourceCount}, {m.firstDestination, m.destinationCount}}};
                for (const auto& [first, count] : ranges) {
                    for (std::uint32_t r = first; r < first + count; ++r) {
                        if (plan_.runs[r].length != 0) {
                            out.push_back({.run = r, .move = move});
                        }
                    }
                }
            }

            // Pairs of overlapping runs of different moves (a move writes its destinations and frees its sources, so
            // every overlap orders the two); only a few runs cover any cluster, so the runs open at a start stay few.
            void sweep(const std::vector<Access>& accesses,
                       std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges) const {
                std::vector<Access> open;
                for (const Access& a : accesses) {
                    const Lcn start = plan_.runs[a.run].start;
                    std::erase_if(open, [&](const Access& b) { return plan_.runs[b.run].end() <= start; });
                    for (const Access& b : open) {
                        if (b.move != a.move) {
                            edges.emplace_back(std::min(a.move, b.move), std::max(a.move, b.move));
                        }
                    }
                    open.push_back(a);
                }
            }

            // Moves of the same file (a park and its return) keep their order even where their runs do not meet.
            void chainFiles(std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges) const {
                std::unordered_map<std::uint64_t, std::uint32_t> last;
                for (std::uint32_t v = 0; v < plan_.moves.size(); ++v) {
                    const auto [it, first] = last.try_emplace(plan_.moves[v].fileReference, v);
                    if (!first) {
                        edges.emplace_back(it->second, v);
                        it->second = v;
                    }
                }
            }

            // The edges as one list per move (repeated edges stay: each adds one to the indegree it is released from).
            void build(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& edges) {
                offsets_.assign(plan_.moves.size() + 1, 0);
                for (const auto& [from, to] : edges) {
                    ++offsets_[from + 1];
                    ++indegree_[to];
                }
                std::partial_sum(offsets_.begin(), offsets_.end(), offsets_.begin());
                items_.resize(edges.size());
                std::vector<std::size_t> next(offsets_.begin(), offsets_.end() - 1);
                for (const auto& [from, to] : edges) {
                    items_[next[from]++] = to;
                }
            }

            const ExecutionPlan& plan_; // The plan.
            std::vector<std::size_t> offsets_; // Per move: where its waiters start in items_; one past the end last.
            std::vector<std::uint32_t> items_; // Every move's waiters, move after move.
            std::vector<std::uint32_t> indegree_; // Per move: edges into it.
        };

        // The moves free to go, found by where their first read starts.
        class ReadyMoves {
        public:
            // An empty set over a plan's moves.
            ReadyMoves(const ExecutionPlan& plan, Executor& executor) :
                keys_(plan.moves.size()), byKey_(plan.moves.size()), rank_(plan.moves.size()),
                ready_(plan.moves.size()) {
                for (std::size_t v = 0; v < plan.moves.size(); ++v) {
                    keys_[v] = firstRead(plan, plan.moves[v]);
                }
                std::iota(byKey_.begin(), byKey_.end(), 0U);
                parallelSort(executor, byKey_, [&](std::uint32_t a, std::uint32_t b) {
                    return keys_[a] != keys_[b] ? keys_[a] < keys_[b] : a < b;
                });
                for (std::uint32_t r = 0; r < byKey_.size(); ++r) {
                    rank_[byKey_[r]] = r;
                }
            }

            // Adds a move.
            void insert(std::uint32_t v) { ready_.insert(rank_[v]); }

            // Whether no move is ready.
            [[nodiscard]] bool empty() const { return ready_.empty(); }

            // Takes the ready move whose first read is nearest `head`; on a tie the one ahead of the head.
            std::uint32_t take(Lcn head) {
                const auto split =
                    std::ranges::partition_point(byKey_, [&](std::uint32_t v) { return keys_[v] < head; });
                const auto r = static_cast<std::size_t>(split - byKey_.begin());
                const std::size_t ahead = ready_.next(r);
                const std::size_t behind = r == 0 ? BitTree::npos : ready_.prev(r - 1);
                const std::size_t chosen = closer(ahead, behind, head);
                ready_.erase(chosen);
                return byKey_[chosen];
            }

        private:
            // Of two ranks (either may be npos, not both), the one whose key is nearer the head.
            [[nodiscard]] std::size_t closer(std::size_t ahead, std::size_t behind, Lcn head) const {
                if (ahead == BitTree::npos || behind == BitTree::npos) {
                    return ahead == BitTree::npos ? behind : ahead;
                }
                return distance(keys_[byKey_[behind]], head) < distance(keys_[byKey_[ahead]], head) ? behind : ahead;
            }

            std::vector<Lcn> keys_; // Per move: where its first read starts.
            std::vector<std::uint32_t> byKey_; // Moves sorted by key, then index.
            std::vector<std::uint32_t> rank_; // Per move: its place in byKey_.
            BitTree ready_; // The ranks of the ready moves.
        };

        // Hands out the moves one at a time: the ready one nearest the head, then the moves that waited only for it
        // become ready.
        class HeadScheduler {
        public:
            // Every move without a predecessor is ready.
            HeadScheduler(const ExecutionPlan& plan, Executor& executor) :
                hazards_(plan, executor), waiting_(hazards_.indegree()), ready_(plan, executor) {
                for (std::uint32_t v = 0; v < waiting_.size(); ++v) {
                    if (waiting_[v] == 0) {
                        ready_.insert(v);
                    }
                }
            }

            // The next move for a head at `head`, or nullopt when every move was handed out (edges only point to
            // later moves, so every move becomes ready).
            std::optional<std::uint32_t> next(Lcn head) {
                if (ready_.empty()) {
                    return std::nullopt;
                }
                const std::uint32_t v = ready_.take(head);
                for (const std::uint32_t w : hazards_.after(v)) {
                    if (--waiting_[w] == 0) {
                        ready_.insert(w);
                    }
                }
                return v;
            }

        private:
            const Hazards hazards_; // Which moves wait for which.
            std::vector<std::uint32_t> waiting_; // Per move: predecessors not handed out yet.
            ReadyMoves ready_; // Moves free to go.
        };

    } // namespace

    std::uint64_t headTravel(const ExecutionPlan& plan) {
        std::uint64_t travel = 0;
        Lcn head = 0;
        for (const PlannedMove& m : plan.moves) {
            travel += distance(head, firstRead(plan, m));
            head = lastWrite(plan, m);
        }
        return travel;
    }

    ExecutionPlan orderForHead(const ExecutionPlan& plan, HeadOrderReport* report, Executor* executor) {
        InlineExecutor inlineExecutor;
        Executor& ex = executor != nullptr ? *executor : inlineExecutor;
        HeadScheduler scheduler(plan, ex);
        ExecutionPlan out;
        out.identity = plan.identity;
        out.reserved = plan.reserved;
        out.moves.reserve(plan.moves.size());
        out.runs.reserve(plan.runs.size());
        out.paths.reserve(plan.moves.size());
        Lcn head = 0;
        while (const std::optional<std::uint32_t> v = scheduler.next(head)) {
            const PlannedMove& m = plan.moves[*v];
            out.add(m, plan.sources(m), plan.destinations(m), *v < plan.paths.size() ? plan.paths[*v] : std::string());
            head = lastWrite(plan, m);
        }
        if (report != nullptr) {
            *report = {.travelBefore = headTravel(plan), .travelAfter = headTravel(out)};
        }
        return out;
    }

} // namespace icd::core::execute
