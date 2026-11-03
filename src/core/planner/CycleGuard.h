#pragma once

#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace icd::core::planner {

    // Keeps placement from closing dependency cycles, each of which costs a parking move at Maximum and a redirect or a
    // drop below it (plan.md 9.3.7). It records which placed files wait for which movers (a file waits for every file
    // whose current clusters its destination covers) and tells whether a new destination would make a file wait,
    // through others, for itself. The files keep a topological order that is repaired as waits are added (Pearce and
    // Kelly), so a destination that fits the order needs no search and a search only visits the files between the two.
    class CycleGuard {
    public:
        // A guard over the snapshot's current owners of each cluster.
        explicit CycleGuard(const Snapshot& snapshot) : snapshot_(snapshot) {}

        // Files a search may visit by default: beyond it the guard lets the destination through (and keeps no wait it
        // could not order); ordering breaks what is left.
        static constexpr std::size_t defaultLimit = 4096;

        // Searches share a budget of visits: this many to start with (small volumes are searched in full) plus
        // perRecord for every destination recorded, so planning stays near-linear on huge volumes.
        static constexpr std::uint64_t startCredit = 1ULL << 22U;

        // Visits each recorded destination adds to the budget. At 30 TB (10 M destinations a level) the searches spend
        // all of it, nine tenths in searches that give up: 64 cost about 45 s a level, 4 costs a few seconds for plans
        // within 0.03% of the moves (T093c).
        static constexpr std::uint64_t perRecord = 4;

        // Visits a search may still make when the budget is spent.
        static constexpr std::size_t minLimit = 16;

        // Whether placing the file on the runs would close a cycle with the destinations recorded so far; searches
        // visit at most `limit` files (callers that try many runs per file pass less).
        [[nodiscard]] bool closesCycle(FileIndex file, std::span<const ClusterRun> runs,
                                       std::size_t limit = defaultLimit);

        // Where a destination that closes a cycle stops doing so when it starts later: one past the first clusters
        // (in LCN order) whose owner already waits, through others, for the file; nullopt when it closes none.
        [[nodiscard]] std::optional<Lcn> cycleEnd(FileIndex file, ClusterRun run);

        // Records a file's destination: the file now waits for the owners of the clusters it covers. A wait that
        // closes a cycle (a destination taken anyway) is left out, so the order stays valid.
        void record(FileIndex file, std::span<const ClusterRun> runs);

    private:
        // One file in the wait graph.
        struct Node {
            std::int64_t order = 0; // Topological position: a file comes after everything it waits for.
            std::uint32_t seen = 0; // The search that last visited it.
            std::vector<std::uint32_t> waiters{}; // Nodes that wait for this one.
            std::vector<std::uint32_t> waitsFor{}; // Nodes this one waits for.
        };

        // How a search ended.
        enum class Search : std::uint8_t {
            Done, // Every file in range was visited.
            Found, // The file searched for was reached.
            GaveUp, // More files than a search may visit: the answer is unknown.
        };

        // The node of a file, or "none".
        [[nodiscard]] std::uint32_t find(FileIndex file) const;

        // Whether the last search visited the file.
        [[nodiscard]] bool visited(FileIndex file) const;

        // Files other than `file` that own clusters of the runs now (they must move before it lands there).
        void blockersOf(FileIndex file, std::span<const ClusterRun> runs);

        // Whether some target already waits, through others, for the file (a search among waiters ordered before the
        // last target).
        [[nodiscard]] bool reaches(FileIndex file, std::span<const FileIndex> targets, std::size_t limit);

        // The node of a file, created at the front (blockers) or the back (placed files) of the order.
        std::uint32_t nodeOf(FileIndex file, bool front);

        // Adds "waiter waits for owner", repairing the order when owner comes after waiter; false when that would
        // close a cycle or the repair is too large (the wait is then left out).
        bool addWait(std::uint32_t owner, std::uint32_t waiter);

        // Moves the owner and what it waits for before the waiter and what waits for it, reusing their positions
        // (Pearce and Kelly); false when the owner already waits for the waiter or a search gives up.
        bool repairOrder(std::uint32_t owner, std::uint32_t waiter, std::int64_t low, std::int64_t high);

        // What one search looks for.
        struct Query {
            std::uint32_t start = 0; // Where it starts.
            std::uint32_t stop = 0; // The node whose discovery ends it ("none" for a full search).
            bool forward = true; // Along waiters (forward) or waitsFor (backward).
            std::int64_t low = 0; // Orders strictly above this are searched...
            std::int64_t high = 0; // ...and strictly below this.
            std::size_t limit = defaultLimit; // Most nodes it may visit (the shared budget may allow fewer).
        };

        // The nodes a query reaches (`out`, the start included); stops early when its stop node is found or too many
        // nodes were visited.
        Search collect(const Query& q, std::vector<std::uint32_t>& out);

        // Pushes the unvisited neighbours of v inside the query's order range; true when one of them is the stop node.
        bool expand(std::uint32_t v, const Query& q);

        const Snapshot& snapshot_; // Current cluster owners.
        std::vector<std::uint32_t> slot_; // Per file: its node, or "none"; sized on first use.
        std::vector<Node> nodes_; // The wait graph.
        std::int64_t front_ = -1; // Next order at the front.
        std::int64_t back_ = 0; // Next order at the back.
        std::vector<FileIndex> blockers_; // Scratch: blockers of the current query.
        std::vector<std::uint32_t> work_; // Scratch: a search's stack.
        std::vector<std::uint32_t> found_; // Scratch: a search's result (the forward half of a repair).
        std::vector<std::uint32_t> behind_; // Scratch: the backward half of an order repair.
        std::vector<std::int64_t> orders_; // Scratch: the positions an order repair reuses.
        std::uint32_t search_ = 0; // The current search.
        std::uint64_t credit_ = startCredit; // Visits searches may still make.
    };

} // namespace icd::core::planner
