#include "core/planner/MoveOrdering.h"

#include "core/strategy/ZoneScript.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>

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

        // Components up to this many waiting moves are broken a move at a time (LocalCycles); larger ones a batch
        // per round, so huge plans need few rounds over the whole graph.
        constexpr std::size_t greedyLimit = 2'000;

        // A large component with at most this many back-edge moves parks them all in one round (that removes every
        // cycle in it); with more, the smallest quarter parks per round.
        constexpr std::size_t allTails = 16;

        // The moves of every component in one array, grouped by component (in move order inside each), so a round over
        // millions of one-move components allocates nothing per component.
        struct Members {
            std::vector<std::uint32_t> offsets; // Per component: where its moves start; one past the last at the end.
            std::vector<std::uint32_t> moves; // The moves.

            // Number of components.
            [[nodiscard]] std::size_t count() const { return offsets.size() - 1; }

            // The moves of a component.
            [[nodiscard]] std::span<const std::uint32_t> of(std::size_t c) const {
                return std::span(moves).subspan(offsets[c], offsets[c + 1] - offsets[c]);
            }
        };

        // A list per move in one array (offsets into `items`): ten million moves need two allocations, not ten
        // million (building and freeing those took seconds).
        template <typename T>
        struct Flat {
            std::vector<std::size_t> offsets{0}; // Per move: where its items start; one past the last at the end.
            std::vector<T> items{}; // Every move's items, move after move.

            // Number of lists.
            [[nodiscard]] std::size_t size() const { return offsets.size() - 1; }

            // One move's list.
            [[nodiscard]] std::span<const T> operator[](std::size_t v) const {
                return std::span(items).subspan(offsets[v], offsets[v + 1] - offsets[v]);
            }
        };

        // The arrays of a component search, kept across rounds: a round resets only the entries of the moves it could
        // visit, instead of allocating them for every move again.
        struct ComponentArrays {
            std::vector<std::uint32_t> index; // Visit order, none when unvisited.
            std::vector<std::uint32_t> low; // Lowest reachable visit order.
            std::vector<std::uint8_t> onStack; // On the Tarjan stack.
            std::vector<std::uint8_t> tail; // Moves with an edge into the stack.
            std::vector<std::uint32_t> component; // Result, none outside every component.

            // Arrays for `moves` moves, all unvisited.
            explicit ComponentArrays(std::size_t moves) :
                index(moves, none), low(moves, 0), onStack(moves, 0), tail(moves, 0), component(moves, none) {}
        };

        // Strongly connected components of the waiting moves (Tarjan, iterative so deep chains cannot overflow the
        // stack).
        class Components {
        public:
            // Finds the components among moves whose state is Waiting or Parked; `live` lists at least those moves, in
            // ascending order (walks start from them in that order, as from every move). `arrays` come unvisited and
            // are left so.
            Components(const Flat<std::uint32_t>& out, const std::vector<State>& state,
                       std::span<const std::uint32_t> live, ComponentArrays& arrays) :
                out_(out), state_(state), visited_(live), index_(arrays.index), low_(arrays.low),
                onStack_(arrays.onStack), tail_(arrays.tail), component_(arrays.component) {
                for (const std::uint32_t v : live) {
                    if (this->live(v) && index_[v] == none) {
                        visit(v);
                    }
                }
            }

            // Leaves the arrays unvisited for the next round (only live moves were touched).
            ~Components() {
                for (const std::uint32_t v : visited_) {
                    index_[v] = none;
                    low_[v] = 0;
                    onStack_[v] = 0;
                    tail_[v] = 0;
                    component_[v] = none;
                }
            }

            Components(const Components&) = delete;
            Components& operator=(const Components&) = delete;
            Components(Components&&) = delete;
            Components& operator=(Components&&) = delete;

            // Component of a move (none for moves that are not waiting).
            [[nodiscard]] std::uint32_t of(std::uint32_t v) const { return component_[v]; }

            // Number of components.
            [[nodiscard]] std::uint32_t count() const { return count_; }

            // Whether v has an edge back into the walk's stack: breaking every such move removes every cycle.
            [[nodiscard]] bool tail(std::uint32_t v) const { return tail_[v] != 0; }

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
                    tail_[v] = 1; // Without v's edges into the stack only tree edges are left: no cycle.
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

            const Flat<std::uint32_t>& out_; // Edges.
            const std::vector<State>& state_; // Move states.
            std::span<const std::uint32_t> visited_; // The moves a walk may have touched.
            std::vector<std::uint32_t>& index_; // Visit order.
            std::vector<std::uint32_t>& low_; // Lowest reachable visit order.
            std::vector<std::uint8_t>& onStack_; // On the Tarjan stack.
            std::vector<std::uint8_t>& tail_; // Moves with an edge into the stack.
            std::vector<std::uint32_t>& component_; // Result.
            std::vector<std::uint32_t> stack_; // Tarjan stack.
            std::uint32_t next_ = 0; // Next visit number.
            std::uint32_t count_ = 0; // Components found.
        };

        // The cycles left among some moves: their strongly connected components of two or more moves (Tarjan over
        // local numbers, iterative), so one component can be broken a move at a time without walking the whole graph.
        class LocalCycles {
        public:
            // Finds the components among the members, following only edges between live members. `localOf` is a
            // per-move scratch array of `none`, left as it was found.
            LocalCycles(const Flat<std::uint32_t>& out, const std::vector<std::uint32_t>& members,
                        std::vector<std::uint32_t>& localOf, const std::function<bool(std::uint32_t)>& live) :
                out_(out), members_(members), localOf_(localOf), live_(live), index_(members.size(), none),
                low_(members.size(), 0), onStack_(members.size(), 0) {
                for (std::uint32_t i = 0; i < members.size(); ++i) {
                    localOf_[members[i]] = i;
                }
                for (std::uint32_t i = 0; i < members.size(); ++i) {
                    if (index_[i] == none && live_(members[i])) {
                        visit(i);
                    }
                }
                for (const std::uint32_t v : members) {
                    localOf_[v] = none;
                }
            }

            // The components with two or more moves (global move numbers).
            [[nodiscard]] std::vector<std::vector<std::uint32_t>>& cycles() { return cycles_; }

        private:
            // The local number of a live member reached by an edge, or none.
            [[nodiscard]] std::uint32_t target(std::uint32_t global) const {
                const std::uint32_t i = localOf_[global];
                return i != none && live_(global) ? i : none;
            }

            // One depth-first walk from a root.
            void visit(std::uint32_t root) {
                std::vector<std::pair<std::uint32_t, std::size_t>> frames{{root, 0}};
                open(root);
                while (!frames.empty()) {
                    auto& [v, edge] = frames.back();
                    const auto edges = out_[members_[v]];
                    if (edge < edges.size()) {
                        const std::uint32_t from = v;
                        const std::uint32_t w = target(edges[edge++]);
                        if (step(from, w)) {
                            frames.emplace_back(w, 0);
                        }
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

            // Follows edge v -> w (w may be none); true when w is new and the walk descends into it.
            bool step(std::uint32_t v, std::uint32_t w) {
                if (w == none) {
                    return false;
                }
                if (index_[w] == none) {
                    open(w);
                    return true;
                }
                if (onStack_[w] != 0) {
                    low_[v] = std::min(low_[v], index_[w]);
                }
                return false;
            }

            // Numbers a member and pushes it.
            void open(std::uint32_t v) {
                index_[v] = low_[v] = next_++;
                stack_.push_back(v);
                onStack_[v] = 1;
            }

            // Pops a component when v is its root; keeps it when it holds a cycle.
            void close(std::uint32_t v) {
                if (low_[v] != index_[v]) {
                    return;
                }
                std::vector<std::uint32_t> component;
                std::uint32_t w = none;
                do {
                    w = stack_.back();
                    stack_.pop_back();
                    onStack_[w] = 0;
                    component.push_back(members_[w]);
                }
                while (w != v);
                if (component.size() > 1) {
                    cycles_.push_back(std::move(component));
                }
            }

            const Flat<std::uint32_t>& out_; // Edges.
            const std::vector<std::uint32_t>& members_; // Local number -> move.
            std::vector<std::uint32_t>& localOf_; // Move -> local number while finding.
            const std::function<bool(std::uint32_t)>& live_; // Whether a move takes part.
            std::vector<std::uint32_t> index_; // Visit order.
            std::vector<std::uint32_t> low_; // Lowest reachable visit order.
            std::vector<std::uint8_t> onStack_; // On the Tarjan stack.
            std::vector<std::uint32_t> stack_; // Tarjan stack.
            std::uint32_t next_ = 0; // Next visit number.
            std::vector<std::vector<std::uint32_t>> cycles_; // Result.
        };

        // The ordering of one plan.
        class Orderer {
        public:
            // Builds the dependency graph.
            Orderer(const Snapshot& snapshot, std::span<const Destination> destinations, const OrderOptions& options,
                    Plan& plan) :
                snapshot_(snapshot), destinations_(destinations), options_(options), plan_(plan),
                state_(destinations.size(), State::Waiting), arrays_(destinations.size()),
                localOf_(destinations.size(), none), indegree_(destinations.size(), 0),
                scratchRuns_(destinations.size()) {
                buildSources();
                index_ = sourceIndex();
                buildEdges();
                buildScratch();
            }

            // Emits every move, breaking cycles as needed.
            OrderReport run() {
                reservePlan();
                dropUnbacked();
                live_.resize(destinations_.size());
                std::iota(live_.begin(), live_.end(), 0U);
                for (std::uint32_t v = 0; v < indegree_.size(); ++v) {
                    if (state_[v] != State::Waiting) {
                        continue;
                    }
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
            // The executor of the options, or an inline one.
            [[nodiscard]] Executor& executor() const {
                return options_.executor != nullptr ? *options_.executor : inline_;
            }

            // Every move's file's movable runs, in one array: counted, then copied in place, in parallel.
            void buildSources() {
                const std::size_t n = destinations_.size();
                const auto records = snapshot_.extents.records();
                const auto movable = [&](std::uint32_t position) {
                    return (records[position].flags() & ExtentUnmovable) == 0;
                };
                std::vector<std::size_t> counts(n, 0);
                forEach(n, [&](std::size_t i) {
                    counts[i] = static_cast<std::size_t>(
                        std::ranges::count_if(snapshot_.extents.extentsOf(destinations_[i].file), movable));
                });
                sources_.offsets.assign(n + 1, 0);
                for (std::size_t i = 0; i < n; ++i) {
                    sources_.offsets[i + 1] = sources_.offsets[i] + counts[i];
                }
                sources_.items.resize(sources_.offsets.back());
                clusters_.resize(n);
                forEach(n, [&](std::size_t i) {
                    std::size_t at = sources_.offsets[i];
                    for (const std::uint32_t position : snapshot_.extents.extentsOf(destinations_[i].file)) {
                        if (movable(position)) {
                            sources_.items[at++] = records[position].run();
                        }
                    }
                    clusters_[i] = clustersOf(sources_[i]);
                });
            }

            // Every move's sources sorted by LCN, with the move (the files' runs are disjoint: no ties).
            [[nodiscard]] std::vector<std::pair<ClusterRun, std::uint32_t>> sourceIndex() const {
                std::vector<std::pair<ClusterRun, std::uint32_t>> index;
                for (std::uint32_t i = 0; i < sources_.size(); ++i) {
                    for (const ClusterRun& r : sources_[i]) {
                        index.emplace_back(r, i);
                    }
                }
                parallelSort(executor(), index,
                             [](const auto& a, const auto& b) { return a.first.start < b.first.start; });
                return index;
            }

            // Every move's predecessors, found per chunk of moves into one flat list each (no vector per move).
            struct Predecessors {
                ChunkedRange range; // The chunks.
                std::vector<Flat<std::uint32_t>> parts; // Per chunk: its moves' predecessors, move after move.

                // The predecessors of move j.
                [[nodiscard]] std::span<const std::uint32_t> of(std::size_t j) const {
                    return parts[j / range.chunkSize()][j % range.chunkSize()];
                }
            };

            // Finds every move's predecessors in parallel.
            [[nodiscard]] Predecessors predecessorLists() const {
                Predecessors p{.range = {.count = destinations_.size(), .grain = 4096}, .parts = {}};
                p.parts.resize(p.range.chunks());
                executor().parallelFor(p.range, [&](std::size_t begin, std::size_t end) {
                    Flat<std::uint32_t>& part = p.parts[begin / p.range.chunkSize()];
                    std::vector<std::uint32_t> preds;
                    for (std::size_t j = begin; j < end; ++j) {
                        predecessors(index_, static_cast<std::uint32_t>(j), preds);
                        part.items.insert(part.items.end(), preds.begin(), preds.end());
                        part.offsets.push_back(part.items.size());
                    }
                });
                return p;
            }

            // Edges: move i -> move j when j's destination overlaps i's sources (i must go first).
            void buildEdges() {
                const Predecessors preds = predecessorLists();
                out_.offsets.assign(destinations_.size() + 1, 0);
                for (std::uint32_t j = 0; j < destinations_.size(); ++j) {
                    for (const std::uint32_t i : preds.of(j)) {
                        ++out_.offsets[i + 1];
                        ++indegree_[j];
                    }
                }
                for (std::size_t i = 0; i < destinations_.size(); ++i) {
                    out_.offsets[i + 1] += out_.offsets[i];
                }
                out_.items.resize(out_.offsets.back());
                std::vector<std::size_t> next(out_.offsets.begin(), out_.offsets.end() - 1);
                for (std::uint32_t j = 0; j < destinations_.size(); ++j) { // Each move's waiters in ascending order.
                    for (const std::uint32_t i : preds.of(j)) {
                        out_.items[next[i]++] = j;
                    }
                }
            }

            // Runs work(i) for every index, in parallel chunks when an executor is set.
            void forEach(std::size_t count, const std::function<void(std::size_t)>& work) const {
                executor().parallelFor({.count = count, .grain = 4096}, [&](std::size_t begin, std::size_t end) {
                    for (std::size_t i = begin; i < end; ++i) {
                        work(i);
                    }
                });
            }

            // The moves whose sources move j's destination overlaps, once each, without j itself (into `preds`, which
            // is reused across moves).
            void predecessors(const std::vector<std::pair<ClusterRun, std::uint32_t>>& index, std::uint32_t j,
                              std::vector<std::uint32_t>& preds) const {
                preds.clear();
                for (const ClusterRun& d : destinations_[j].runs) {
                    auto it = std::ranges::upper_bound(index, d.start, {}, [](const auto& e) { return e.first.end(); });
                    for (; it != index.end() && it->first.start < d.end(); ++it) {
                        preds.push_back(it->second);
                    }
                }
                std::ranges::sort(preds);
                preds.erase(std::ranges::unique(preds).begin(), preds.end());
                std::erase(preds, j);
            }

            // Whether every cluster of a destination is free now or vacated by some move.
            [[nodiscard]] bool backed(const Destination& d,
                                      const std::vector<std::pair<ClusterRun, std::uint32_t>>& index) const {
                for (const ClusterRun& r : d.runs) {
                    ClusterCount covered = 0;
                    for (const ClusterRun& f : snapshot_.freeSpace.overlapping(r)) {
                        covered += overlap(f, r);
                    }
                    auto it = std::ranges::upper_bound(index, r.start, {}, [](const auto& e) { return e.first.end(); });
                    for (; it != index.end() && it->first.start < r.end(); ++it) {
                        covered += overlap(it->first, r);
                    }
                    if (covered != r.length) {
                        return false;
                    }
                }
                return true;
            }

            // Clusters two runs share.
            static ClusterCount overlap(ClusterRun a, ClusterRun b) {
                const Lcn start = std::max(a.start, b.start);
                const Lcn end = std::min(a.end(), b.end());
                return end > start ? end - start : 0;
            }

            // Drops moves whose destination lands on a file that does not move (a planner left it in place), with
            // everything that waits on them.
            void dropUnbacked() {
                std::vector<std::uint8_t> unbacked(destinations_.size(), 0);
                forEach(destinations_.size(),
                        [&](std::size_t v) { unbacked[v] = backed(destinations_[v], index_) ? 0 : 1; });
                std::vector<std::uint32_t> bad;
                for (std::uint32_t v = 0; v < destinations_.size(); ++v) {
                    if (unbacked[v] != 0) {
                        bad.push_back(v);
                    }
                }
                drop(bad, SkipReason::Dependency);
            }

            // Scratch space: free runs no destination touches (one sorted difference instead of millions of takes).
            void buildScratch() {
                std::vector<ClusterRun> taken;
                for (const Destination& d : destinations_) {
                    taken.insert(taken.end(), d.runs.begin(), d.runs.end());
                }
                parallelSort(executor(), taken,
                             [](const ClusterRun& a, const ClusterRun& b) { return a.start < b.start; });
                const std::vector<ClusterRun> base = options_.scratch.empty()
                    ? std::vector<ClusterRun>(snapshot_.freeSpace.runs().begin(), snapshot_.freeSpace.runs().end())
                    : HoleSet(options_.scratch).runs();
                scratch_ = HoleSet(subtractRuns({.from = base, .minus = taken}));
            }

            // Runs to keep and runs to take out of them.
            struct RunDifference {
                std::span<const ClusterRun> from; // Sorted, disjoint runs.
                std::span<const ClusterRun>
                    minus; // Runs sorted by start (they may overlap each other and reach outside).
            };

            // The `from` runs without the `minus` runs.
            static std::vector<ClusterRun> subtractRuns(const RunDifference& d) {
                std::vector<ClusterRun> out;
                std::size_t j = 0;
                for (const ClusterRun& r : d.from) {
                    while (j < d.minus.size() && d.minus[j].end() <= r.start) {
                        ++j;
                    }
                    subtractFrom(r, d.minus.subspan(j), out);
                }
                return out;
            }

            // Appends what is left of one run without the runs (sorted by start) that reach into it.
            static void subtractFrom(ClusterRun r, std::span<const ClusterRun> minus, std::vector<ClusterRun>& out) {
                Lcn cursor = r.start;
                for (std::size_t k = 0; k < minus.size() && minus[k].start < r.end(); ++k) {
                    if (minus[k].start > cursor) {
                        out.push_back({cursor, minus[k].start - cursor});
                    }
                    cursor = std::max(cursor, minus[k].end());
                }
                if (cursor < r.end()) {
                    out.push_back({cursor, r.end() - cursor});
                }
            }

            // Makes room in the plan for every move once (parks come on top), so emitting does not regrow it.
            void reservePlan() {
                std::size_t runs = sources_.items.size();
                for (const Destination& d : destinations_) {
                    runs += d.runs.size();
                }
                plan_.moves.reserve(plan_.moves.size() + destinations_.size());
                plan_.runs.reserve(plan_.runs.size() + runs);
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
                std::erase_if(live_, [&](std::uint32_t v) {
                    return state_[v] != State::Waiting && state_[v] != State::Parked; // No state turns live again.
                });
                const Components components(out_, state_, live_, arrays_);
                if (components.count() == 0) {
                    return false;
                }
                std::vector<std::uint8_t> entered(components.count(), 0);
                const Members members = membersOf(components, entered);
                // Parking and redirecting are valid at any time, so every cycle is broken in this one pass; only when
                // none is left (a waiting move nothing feeds) are the components without inbound edges broken.
                bool any = false;
                for (std::size_t c = 0; c < members.count(); ++c) {
                    const auto component = members.of(c);
                    // One move never holds a cycle (breakIfCycle() needs two waiting ones), whatever happened since.
                    any = (component.size() > 1 && breakIfCycle(component, components)) || any;
                }
                return any || breakSources(members, entered);
            }

            // The moves of each component, and which components a live edge enters from outside.
            [[nodiscard]] Members membersOf(const Components& components, std::vector<std::uint8_t>& entered) const {
                Members m{.offsets = std::vector<std::uint32_t>(components.count() + 1, 0), .moves = {}};
                for (const std::uint32_t v : live_) { // Moves outside it belong to no component.
                    if (components.of(v) != none) {
                        ++m.offsets[components.of(v) + 1];
                    }
                }
                for (std::size_t c = 1; c < m.offsets.size(); ++c) {
                    m.offsets[c] += m.offsets[c - 1];
                }
                m.moves.resize(m.offsets.back());
                std::vector<std::uint32_t> next(m.offsets.begin(), m.offsets.end() - 1);
                for (const std::uint32_t v : live_) {
                    if (components.of(v) != none) {
                        m.moves[next[components.of(v)]++] = v;
                        markEntered(v, components, entered);
                    }
                }
                return m;
            }

            // Breaks the components nothing outside feeds; false when there is none.
            bool breakSources(const Members& members, const std::vector<std::uint8_t>& entered) {
                bool broken = false;
                for (std::size_t c = 0; c < members.count(); ++c) {
                    if (entered[c] == 0 && state_[members.of(c).front()] == State::Waiting) {
                        ++report_.cycles;
                        breakCycle(members.of(c));
                        broken = true;
                    }
                }
                return broken;
            }

            // The waiting moves of a component, and those with an edge back into the walk, smallest first.
            struct CycleMoves {
                std::vector<std::uint32_t> waiting; // Moves still waiting.
                std::vector<std::uint32_t> tails; // Of those, the ones whose breaking removes every cycle.
            };

            // Splits a component into its waiting moves and tails.
            [[nodiscard]] CycleMoves cycleMoves(std::span<const std::uint32_t> component,
                                                const Components& components) const {
                CycleMoves out;
                for (const std::uint32_t v : component) {
                    if (state_[v] != State::Waiting) {
                        continue;
                    }
                    out.waiting.push_back(v);
                    if (components.tail(v)) {
                        out.tails.push_back(v);
                    }
                }
                std::ranges::sort(out.tails, [&](std::uint32_t a, std::uint32_t b) { return smaller(a, b); });
                return out;
            }

            // Breaks a component when two or more of its moves still wait (it holds cycles): of the moves with an edge
            // back into the walk (breaking all of them would remove every cycle) the smallest quarter is parked per
            // round (later rounds find which cycles are left, so few large files park), or all are redirected (that
            // costs no extra move). When scratch space runs out the round ends (returns of parked files free it
            // again); below Maximum, or when not even one fits, the component is dropped (plan.md 9.3.7).
            bool breakIfCycle(std::span<const std::uint32_t> component, const Components& components) {
                CycleMoves moves = cycleMoves(component, components);
                if (moves.waiting.size() < 2) {
                    return false;
                }
                ++report_.cycles;
                if (options_.allowParking && moves.waiting.size() <= greedyLimit) {
                    breakGreedily(moves);
                    return true;
                }
                if (options_.allowParking && moves.tails.size() > allTails) {
                    moves.tails.resize((moves.tails.size() + 3) / 4);
                }
                breakTails(moves);
                return true;
            }

            // Parks the smallest move of every cycle left in a component, finds the cycles left among the rest, and
            // so on until none is left: a parked file breaks every cycle through it, so later rounds see only the
            // cycles it did not break and few large files park.
            void breakGreedily(const CycleMoves& moves) {
                std::vector<std::uint32_t> chosen = chooseBreaks(moves.waiting);
                keepNeeded(moves.waiting, chosen);
                bool broken = false;
                for (const std::uint32_t v : chosen) {
                    if (!breakOne(v)) {
                        outOfScratch(moves, broken);
                        return;
                    }
                    broken = true;
                }
            }

            // Moves to break until no cycle is left among the others: per round, the cheapest break of every
            // component (nothing is parked yet; `marked_` holds the choice).
            std::vector<std::uint32_t> chooseBreaks(const std::vector<std::uint32_t>& waiting) {
                marked_.resize(destinations_.size(), 0);
                const std::function<bool(std::uint32_t)> live = [&](std::uint32_t v) {
                    return state_[v] == State::Waiting && marked_[v] == 0;
                };
                std::vector<std::uint32_t> chosen;
                std::vector<std::uint32_t> members = waiting;
                while (!members.empty()) {
                    LocalCycles local(out_, members, localOf_, live);
                    members.clear();
                    for (auto& cycle : local.cycles()) {
                        const std::uint32_t v = cheapestBreak(cycle);
                        marked_[v] = 1;
                        chosen.push_back(v);
                        members.insert(members.end(), cycle.begin(), cycle.end());
                    }
                }
                return chosen;
            }

            // Drops breaks that are not needed: largest first, a chosen move whose return closes no cycle among the
            // moves not chosen stays where the plan puts it (greedy choices often leave such redundant ones).
            void keepNeeded(const std::vector<std::uint32_t>& waiting, std::vector<std::uint32_t>& chosen) {
                std::vector<std::uint32_t> bySize = chosen;
                std::ranges::sort(bySize, [&](std::uint32_t a, std::uint32_t b) { return smaller(b, a); });
                for (const std::uint32_t v : waiting) {
                    localOf_[v] = 0; // Membership of the component, for the cycle walks.
                }
                for (const std::uint32_t v : bySize) {
                    marked_[v] = 0;
                    if (onCycle(v)) {
                        marked_[v] = 1;
                    }
                }
                for (const std::uint32_t v : waiting) {
                    localOf_[v] = none;
                }
                std::erase_if(chosen, [&](std::uint32_t v) { return marked_[v] == 0; });
                for (const std::uint32_t v : chosen) {
                    marked_[v] = 0;
                }
            }

            // Whether a walk from v along waits among unchosen waiting moves of the component comes back to v.
            [[nodiscard]] bool onCycle(std::uint32_t v) {
                ++walk_;
                seen_.resize(destinations_.size(), 0);
                std::vector<std::uint32_t> stack{v};
                while (!stack.empty()) {
                    const std::uint32_t u = stack.back();
                    stack.pop_back();
                    for (const std::uint32_t w : out_[u]) {
                        if (w == v) {
                            return true;
                        }
                        if (localOf_[w] != none && state_[w] == State::Waiting && marked_[w] == 0 &&
                            seen_[w] != walk_) {
                            seen_[w] = walk_;
                            stack.push_back(w);
                        }
                    }
                }
                return false;
            }

            // Waits into and out of each move of a component, counting only waits between its waiting moves.
            struct Degrees {
                std::vector<std::uint64_t> in; // Per member: members it waits for.
                std::vector<std::uint64_t> out; // Per member: members waiting for it.
            };

            // The degrees of a component's members inside it (localOf_ is left as it was found).
            [[nodiscard]] Degrees degreesIn(const std::vector<std::uint32_t>& component) {
                Degrees d{.in = std::vector<std::uint64_t>(component.size(), 0),
                          .out = std::vector<std::uint64_t>(component.size(), 0)};
                for (std::uint32_t i = 0; i < component.size(); ++i) {
                    localOf_[component[i]] = i;
                }
                for (std::uint32_t i = 0; i < component.size(); ++i) {
                    for (const std::uint32_t w : out_[component[i]]) {
                        const std::uint32_t j = localOf_[w];
                        if (j != none && state_[w] == State::Waiting) {
                            ++d.out[i];
                            ++d.in[j];
                        }
                    }
                }
                for (const std::uint32_t v : component) {
                    localOf_[v] = none;
                }
                return d;
            }

            // The move of a component whose parking breaks the most cycles per cluster: the fewest clusters for the
            // most waits through it (waits in times waits out; every member lies on a cycle), the smaller file among
            // equals.
            [[nodiscard]] std::uint32_t cheapestBreak(const std::vector<std::uint32_t>& component) {
                const Degrees d = degreesIn(component);
                std::size_t best = 0;
                for (std::size_t i = 1; i < component.size(); ++i) {
                    const std::uint64_t power = d.in[i] * d.out[i];
                    const std::uint64_t bestPower = d.in[best] * d.out[best];
                    const std::uint64_t cost = clusters_[component[i]] * bestPower;
                    const std::uint64_t bestCost = clusters_[component[best]] * power;
                    if (cost < bestCost || (cost == bestCost && smaller(component[i], component[best]))) {
                        best = i;
                    }
                }
                return component[best];
            }

            // Parks (Maximum) or redirects one move; false without scratch space for it.
            bool breakOne(std::uint32_t v) {
                // A move without a zone only needs its file in one piece somewhere: free space reaches that in one
                // move, so it is redirected even when parking is allowed (and parked when no free run holds it).
                const bool anywhere = destinations_[v].zone == noZone;
                if (options_.allowParking && !anywhere) {
                    return park(v);
                }
                return redirect(v) || (options_.allowParking && park(v));
            }

            // What happens when scratch space runs out while breaking a component: wait for parked files to return
            // (when this round parked any, or one smaller member still fits), else drop the component.
            void outOfScratch(const CycleMoves& moves, bool broken) {
                if (!options_.allowParking || !(broken || breakAny(moves.waiting))) {
                    drop(moves.waiting, SkipReason::Cycle);
                }
            }

            // Parks or redirects the tails in order; see breakIfCycle().
            void breakTails(const CycleMoves& moves) {
                bool broken = false;
                for (const std::uint32_t v : moves.tails) {
                    if (state_[v] != State::Waiting) {
                        continue;
                    }
                    if (!breakOne(v)) {
                        outOfScratch(moves, broken);
                        return;
                    }
                    broken = true;
                }
            }

            // Parks (or redirects) the smallest waiting move of a component that fits the scratch space; false when
            // none does.
            bool breakAny(std::vector<std::uint32_t> waiting) {
                std::ranges::sort(waiting, [&](std::uint32_t a, std::uint32_t b) { return smaller(a, b); });
                const ClusterCount room = scratch_.total();
                for (const std::uint32_t v : waiting) {
                    if (clusters_[v] > room) {
                        return false; // Sorted by size: nothing later fits either.
                    }
                    if (breakOne(v)) {
                        return true;
                    }
                }
                return false;
            }

            // Whether move a's file is smaller than b's (index breaks ties).
            [[nodiscard]] bool smaller(std::uint32_t a, std::uint32_t b) const {
                const ClusterCount ca = clusters_[a];
                const ClusterCount cb = clusters_[b];
                return ca != cb ? ca < cb : a < b;
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
            void breakCycle(std::span<const std::uint32_t> cycle) {
                const auto smallest =
                    std::ranges::min_element(cycle, [&](std::uint32_t a, std::uint32_t b) { return smaller(a, b); });
                if (breakOne(*smallest)) {
                    return;
                }
                drop(cycle, SkipReason::Cycle);
            }

            // Scratch space for a move's file: one run, else pieces; taken out of the scratch pool. Empty when none.
            std::vector<ClusterRun> takeScratch(std::uint32_t v, bool onePiece) {
                const ClusterCount clusters = clusters_[v];
                const SpaceRequest request{
                    .clusters = clusters, .own = {}, .window = {0, snapshot_.geometry.totalClusters}};
                std::vector<ClusterRun> runs;
                if (const auto run = findFirst(scratch_, request)) {
                    runs.push_back(*run);
                }
                else if (auto pieces = onePiece ? std::nullopt : findPieces(scratch_, request, 1)) {
                    runs = std::move(*pieces);
                }
                takeRuns(scratch_, runs);
                return runs;
            }

            // Scratch pieces of at least 64 MB each (never worse than "good enough"), or nothing.
            std::vector<ClusterRun> takeLargePieces(std::uint32_t v) {
                const ClusterCount cluster = std::max<std::uint32_t>(snapshot_.geometry.bytesPerCluster, 1);
                const ClusterCount minPiece = (strategy::Zone::defaultFragmentOk + cluster - 1) / cluster;
                const SpaceRequest request{
                    .clusters = clusters_[v], .own = {}, .window = {0, snapshot_.geometry.totalClusters}};
                auto pieces = findPieces(scratch_, request, minPiece);
                if (!pieces) {
                    return {};
                }
                takeRuns(scratch_, *pieces);
                return std::move(*pieces);
            }

            // Levels that may not park break a cycle by sending its smallest file to free space nothing else uses: it
            // still moves once (no extra cost) and its fragments are joined, but it ends outside its target.
            bool redirect(std::uint32_t v) {
                std::vector<ClusterRun> runs = takeScratch(v, true);
                if (runs.empty()) {
                    runs = takeLargePieces(v);
                }
                if (runs.empty()) {
                    return false;
                }
                const Destination& d = destinations_[v];
                const MoveReason reason =
                    fragmentsOf(sources_[v]) > fragmentsOf(runs) ? MoveReason::Defragment : MoveReason::Relocate;
                plan_.add({.file = d.file, .reason = reason, .zone = noZone, .extra = d.extra}, sources_[v], runs);
                state_[v] = State::Done;
                ++report_.redirected;
                release(v);
                return true;
            }

            // Moves a file to scratch space now; its return waits for the move's own dependencies.
            bool park(std::uint32_t v) {
                const ClusterCount clusters = clusters_[v];
                std::vector<ClusterRun> runs = takeScratch(v, false);
                if (runs.empty()) {
                    return false;
                }
                const Destination& d = destinations_[v];
                parkMove_[v] = plan_.moves.size();
                plan_.add({.file = d.file, .reason = MoveReason::Park, .zone = noZone, .extra = true}, sources_[v],
                          runs);
                plan_.extra += clusters;
                scratchRuns_[v] = std::move(runs);
                state_[v] = State::Parked;
                ++report_.parked;
                release(v);
                return true;
            }

            // Drops moves (skipped with `reason`) and every waiting move that depends on them (Dependency).
            void drop(std::span<const std::uint32_t> cycle, SkipReason reason) {
                std::vector<std::uint32_t> work(cycle.begin(), cycle.end());
                const std::unordered_set<std::uint32_t> inCycle(cycle.begin(), cycle.end());
                while (!work.empty()) {
                    const std::uint32_t v = work.back();
                    work.pop_back();
                    if (state_[v] == State::Parked) {
                        settle(v);
                    }
                    if (state_[v] != State::Waiting) {
                        continue;
                    }
                    state_[v] = State::Dropped;
                    skip(v, inCycle.contains(v) ? reason : SkipReason::Dependency);
                    work.insert(work.end(), out_[v].begin(), out_[v].end());
                }
            }

            // A parked file whose return can no longer happen stays in its scratch space: its park becomes its one
            // real move (outside its target), so it moves once and nothing is extra about it.
            void settle(std::uint32_t v) {
                Move& park = plan_.moves[parkMove_[v]];
                const MoveReason reason = fragmentsOf(sources_[v]) > fragmentsOf(plan_.destinations(park))
                    ? MoveReason::Defragment
                    : MoveReason::Relocate;
                park.reason = reason;
                park.extra = destinations_[v].extra;
                plan_.extra -= park.clusters;
                plan_.extra += park.extra ? park.clusters : 0;
                state_[v] = State::Done;
                --report_.parked;
                ++report_.redirected;
            }

            // Records a dropped move.
            void skip(std::uint32_t v, SkipReason reason) {
                ++report_.dropped;
                const Destination& d = destinations_[v];
                if (d.extra) {
                    return; // Not part of the lower bound: leaving it out costs nothing.
                }
                const ClusterCount clusters = clusters_[v];
                plan_.skipped.push_back({.file = d.file, .reason = reason, .clusters = clusters});
                plan_.skippedClusters += clusters;
            }

            const Snapshot& snapshot_; // The volume.
            std::span<const Destination> destinations_; // What to order.
            const OrderOptions& options_; // Parking or not.
            mutable InlineExecutor inline_; // Runs the parallel steps when the options bring no executor.
            std::vector<std::pair<ClusterRun, std::uint32_t>> index_; // Every move's sources by LCN.
            Plan& plan_; // Output.
            std::vector<State> state_; // Per move.
            Flat<ClusterRun> sources_; // Per move: the file's movable runs.
            std::vector<ClusterCount> clusters_; // Per move: its file's movable clusters (sizes rank cycle breaking).
            std::vector<std::uint32_t> live_; // Moves that may still be waiting or parked, ascending.
            std::vector<std::uint8_t> marked_; // Per move: chosen to break cycles (while choosing).
            std::vector<std::uint32_t> seen_; // Per move: the cycle walk that last reached it.
            std::uint32_t walk_ = 0; // The current cycle walk.
            Flat<std::uint32_t> out_; // Per move: the moves waiting for it.
            ComponentArrays arrays_; // The component search's arrays, kept across rounds.
            std::vector<std::uint32_t> localOf_; // Scratch for LocalCycles: per move, its local number or none.
            std::vector<std::uint32_t> indegree_; // Per move: moves it still waits for.
            std::vector<std::vector<ClusterRun>> scratchRuns_; // Per parked move: where its file sits.
            std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready_; // Ready moves.
            HoleSet scratch_; // Free runs no destination touches.
            std::unordered_map<std::uint32_t, std::size_t> parkMove_; // Per parked move: its park move in the plan.
            OrderReport report_; // Counts.
        };

    } // namespace

    OrderReport orderMoves(const Snapshot& snapshot, std::span<const Destination> destinations,
                           const OrderOptions& options, Plan& plan) {
        return Orderer(snapshot, destinations, options, plan).run();
    }

} // namespace icd::core::planner
