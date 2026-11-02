#include "core/planner/MoveOrdering.h"

#include <algorithm>
#include <functional>
#include <queue>

namespace icd::core::planner {

    namespace {

        // Where a move stands while ordering.
        enum class State : std::uint8_t {
            Waiting, // Not emitted.
            Parked, // Its file sits in scratch space; the return move is still waiting.
            Done, // Emitted (for a parked file: returned).
            Dropped, // Left out.
        };

        // "No component".
        constexpr std::uint32_t none = static_cast<std::uint32_t>(-1);

        // Strongly connected components of the waiting moves (Tarjan, iterative so deep chains cannot overflow the
        // stack).
        class Components {
        public:
            // Finds the components among moves whose state is Waiting or Parked.
            Components(const std::vector<std::vector<std::uint32_t>>& out, const std::vector<State>& state) :
                out_(out), state_(state), index_(out.size(), none), low_(out.size(), 0), onStack_(out.size(), 0),
                component_(out.size(), none) {
                for (std::uint32_t v = 0; v < out.size(); ++v) {
                    if (live(v) && index_[v] == none) {
                        visit(v);
                    }
                }
            }

            // Component of a move (none for moves that are not waiting).
            [[nodiscard]] std::uint32_t of(std::uint32_t v) const { return component_[v]; }

            // Number of components.
            [[nodiscard]] std::uint32_t count() const { return count_; }

        private:
            // Whether a move takes part.
            [[nodiscard]] bool live(std::uint32_t v) const {
                return state_[v] == State::Waiting || state_[v] == State::Parked;
            }

            // One depth-first walk from v.
            void visit(std::uint32_t root) {
                std::vector<std::pair<std::uint32_t, std::size_t>> frames{{root, 0}};
                open(root);
                while (!frames.empty()) {
                    auto& [v, edge] = frames.back();
                    if (edge < out_[v].size()) {
                        const std::uint32_t w = out_[v][edge++];
                        step(v, w, frames);
                        continue;
                    }
                    const std::uint32_t done = v;
                    frames.pop_back();
                    if (!frames.empty()) {
                        low_[frames.back().first] = std::min(low_[frames.back().first], low_[done]);
                    }
                    close(done);
                }
            }

            // Follows edge v -> w.
            void step(std::uint32_t v, std::uint32_t w, std::vector<std::pair<std::uint32_t, std::size_t>>& frames) {
                if (!live(w) || state_[v] == State::Parked) {
                    return; // A parked file's sources are free already: its edges no longer hold anything up.
                }
                if (index_[w] == none) {
                    open(w);
                    frames.emplace_back(w, 0);
                }
                else if (onStack_[w] != 0) {
                    low_[v] = std::min(low_[v], index_[w]);
                }
            }

            // Numbers a move and pushes it.
            void open(std::uint32_t v) {
                index_[v] = low_[v] = next_++;
                stack_.push_back(v);
                onStack_[v] = 1;
            }

            // Pops a component when v is its root.
            void close(std::uint32_t v) {
                if (low_[v] != index_[v]) {
                    return;
                }
                std::uint32_t w = none;
                do {
                    w = stack_.back();
                    stack_.pop_back();
                    onStack_[w] = 0;
                    component_[w] = count_;
                }
                while (w != v);
                ++count_;
            }

            const std::vector<std::vector<std::uint32_t>>& out_; // Edges.
            const std::vector<State>& state_; // Move states.
            std::vector<std::uint32_t> index_; // Visit order.
            std::vector<std::uint32_t> low_; // Lowest reachable visit order.
            std::vector<std::uint8_t> onStack_; // On the Tarjan stack.
            std::vector<std::uint32_t> component_; // Result.
            std::vector<std::uint32_t> stack_; // Tarjan stack.
            std::uint32_t next_ = 0; // Next visit number.
            std::uint32_t count_ = 0; // Components found.
        };

        // The ordering of one plan.
        class Orderer {
        public:
            // Builds the dependency graph.
            Orderer(const Snapshot& snapshot, std::span<const Destination> destinations, const OrderOptions& options,
                    Plan& plan) :
                snapshot_(snapshot), destinations_(destinations), options_(options), plan_(plan),
                state_(destinations.size(), State::Waiting), out_(destinations.size()),
                indegree_(destinations.size(), 0), scratchRuns_(destinations.size()) {
                sources_.reserve(destinations.size());
                for (const Destination& d : destinations) {
                    sources_.push_back(movableRuns(snapshot, d.file));
                }
                buildEdges();
                buildScratch();
            }

            // Emits every move, breaking cycles as needed.
            OrderReport run() {
                for (std::uint32_t v = 0; v < indegree_.size(); ++v) {
                    if (indegree_[v] == 0) {
                        ready_.push(v);
                    }
                }
                while (true) {
                    drain();
                    if (!breakCycles()) {
                        return report_;
                    }
                }
            }

        private:
            // Edges: move i -> move j when j's destination overlaps i's sources (i must go first).
            void buildEdges() {
                std::vector<std::pair<ClusterRun, std::uint32_t>> index;
                for (std::uint32_t i = 0; i < sources_.size(); ++i) {
                    for (const ClusterRun& r : sources_[i]) {
                        index.emplace_back(r, i);
                    }
                }
                std::ranges::sort(index, {}, [](const auto& e) { return e.first.start; });
                for (std::uint32_t j = 0; j < destinations_.size(); ++j) {
                    for (const std::uint32_t i : predecessors(index, j)) {
                        out_[i].push_back(j);
                        ++indegree_[j];
                    }
                }
            }

            // The moves whose sources move j's destination overlaps, once each, without j itself.
            [[nodiscard]] std::vector<std::uint32_t>
            predecessors(const std::vector<std::pair<ClusterRun, std::uint32_t>>& index, std::uint32_t j) const {
                std::vector<std::uint32_t> preds;
                for (const ClusterRun& d : destinations_[j].runs) {
                    auto it = std::ranges::upper_bound(index, d.start, {}, [](const auto& e) { return e.first.end(); });
                    for (; it != index.end() && it->first.start < d.end(); ++it) {
                        preds.push_back(it->second);
                    }
                }
                std::ranges::sort(preds);
                preds.erase(std::ranges::unique(preds).begin(), preds.end());
                std::erase(preds, j);
                return preds;
            }

            // Scratch space: free runs no destination touches.
            void buildScratch() {
                scratch_ = options_.scratch.empty() ? HoleSet(snapshot_.freeSpace.runs()) : HoleSet(options_.scratch);
                for (const Destination& d : destinations_) {
                    for (const ClusterRun& r : d.runs) {
                        for (const ClusterRun& piece : scratch_.within(r)) {
                            (void)scratch_.take(piece);
                        }
                    }
                }
            }

            // Emits ready moves in destination order until none is ready.
            void drain() {
                while (!ready_.empty()) {
                    const std::uint32_t v = ready_.top();
                    ready_.pop();
                    if (state_[v] == State::Dropped || state_[v] == State::Done) {
                        continue;
                    }
                    const bool wasParked = state_[v] == State::Parked;
                    emit(v);
                    if (!wasParked) {
                        release(v);
                    }
                }
            }

            // Emits the move (or a parked file's return).
            void emit(std::uint32_t v) {
                const Destination& d = destinations_[v];
                const Move move{.file = d.file, .reason = d.reason, .zone = d.zone, .extra = d.extra};
                if (state_[v] == State::Parked) {
                    Move back = move;
                    back.fromParking = true;
                    plan_.add(back, scratchRuns_[v], d.runs);
                    releaseRuns(scratch_, scratchRuns_[v]);
                }
                else {
                    plan_.add(move, sources_[v], d.runs);
                }
                state_[v] = State::Done;
            }

            // The move's sources are free now: its dependents lose a dependency.
            void release(std::uint32_t v) {
                for (const std::uint32_t w : out_[v]) {
                    if (--indegree_[w] == 0) {
                        ready_.push(w);
                    }
                }
            }

            // Breaks every cycle that waits on nothing else; false when nothing is waiting.
            bool breakCycles() {
                const Components components(out_, state_);
                if (components.count() == 0) {
                    return false;
                }
                std::vector<std::uint8_t> entered(components.count(), 0);
                std::vector<std::vector<std::uint32_t>> members(components.count());
                for (std::uint32_t v = 0; v < out_.size(); ++v) {
                    if (components.of(v) == none) {
                        continue;
                    }
                    members[components.of(v)].push_back(v);
                    markEntered(v, components, entered);
                }
                bool any = false;
                for (std::uint32_t c = 0; c < components.count(); ++c) {
                    if (entered[c] == 0 && state_[members[c].front()] == State::Waiting) {
                        ++report_.cycles;
                        breakCycle(members[c]);
                        any = true;
                    }
                }
                return any;
            }

            // Marks the components that v's live edges enter from outside.
            void markEntered(std::uint32_t v, const Components& components, std::vector<std::uint8_t>& entered) const {
                if (state_[v] == State::Parked) {
                    return;
                }
                for (const std::uint32_t w : out_[v]) {
                    if (components.of(w) != none && components.of(w) != components.of(v)) {
                        entered[components.of(w)] = 1;
                    }
                }
            }

            // Parks the smallest file of a cycle, or drops the cycle.
            void breakCycle(const std::vector<std::uint32_t>& cycle) {
                const auto smallest = std::ranges::min_element(cycle, [&](std::uint32_t a, std::uint32_t b) {
                    const ClusterCount ca = clustersOf(sources_[a]);
                    const ClusterCount cb = clustersOf(sources_[b]);
                    return ca != cb ? ca < cb : a < b;
                });
                if (options_.allowParking && park(*smallest)) {
                    return;
                }
                drop(cycle);
            }

            // Moves a file to scratch space now; its return waits for the move's own dependencies.
            bool park(std::uint32_t v) {
                const ClusterCount clusters = clustersOf(sources_[v]);
                const SpaceRequest request{
                    .clusters = clusters, .own = {}, .window = {0, snapshot_.geometry.totalClusters}};
                std::vector<ClusterRun> runs;
                if (const auto run = findFirst(scratch_, request)) {
                    runs.push_back(*run);
                }
                else if (auto pieces = findPieces(scratch_, request, 1)) {
                    runs = std::move(*pieces);
                }
                if (runs.empty()) {
                    return false;
                }
                takeRuns(scratch_, runs);
                const Destination& d = destinations_[v];
                plan_.add({.file = d.file, .reason = MoveReason::Park, .zone = noZone, .extra = true}, sources_[v],
                          runs);
                plan_.extra += clusters;
                scratchRuns_[v] = std::move(runs);
                state_[v] = State::Parked;
                ++report_.parked;
                release(v);
                return true;
            }

            // Drops a cycle and every waiting move that depends on it.
            void drop(const std::vector<std::uint32_t>& cycle) {
                std::vector<std::uint32_t> work = cycle;
                std::vector<std::uint8_t> inCycle(out_.size(), 0);
                for (const std::uint32_t v : cycle) {
                    inCycle[v] = 1;
                }
                while (!work.empty()) {
                    const std::uint32_t v = work.back();
                    work.pop_back();
                    if (state_[v] != State::Waiting) {
                        continue;
                    }
                    state_[v] = State::Dropped;
                    skip(v, inCycle[v] != 0 ? SkipReason::Cycle : SkipReason::Dependency);
                    work.insert(work.end(), out_[v].begin(), out_[v].end());
                }
            }

            // Records a dropped move.
            void skip(std::uint32_t v, SkipReason reason) {
                ++report_.dropped;
                const Destination& d = destinations_[v];
                if (d.extra) {
                    return; // Not part of the lower bound: leaving it out costs nothing.
                }
                const ClusterCount clusters = clustersOf(sources_[v]);
                plan_.skipped.push_back({.file = d.file, .reason = reason, .clusters = clusters});
                plan_.skippedClusters += clusters;
            }

            const Snapshot& snapshot_; // The volume.
            std::span<const Destination> destinations_; // What to order.
            const OrderOptions& options_; // Parking or not.
            Plan& plan_; // Output.
            std::vector<State> state_; // Per move.
            std::vector<std::vector<ClusterRun>> sources_; // Per move: the file's movable runs.
            std::vector<std::vector<std::uint32_t>> out_; // Per move: the moves waiting for it.
            std::vector<std::uint32_t> indegree_; // Per move: moves it still waits for.
            std::vector<std::vector<ClusterRun>> scratchRuns_; // Per parked move: where its file sits.
            std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready_; // Ready moves.
            HoleSet scratch_; // Free runs no destination touches.
            OrderReport report_; // Counts.
        };

    } // namespace

    OrderReport orderMoves(const Snapshot& snapshot, std::span<const Destination> destinations,
                           const OrderOptions& options, Plan& plan) {
        return Orderer(snapshot, destinations, options, plan).run();
    }

} // namespace icd::core::planner
