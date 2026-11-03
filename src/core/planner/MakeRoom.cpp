#include "core/planner/MakeRoom.h"

#include "core/planner/Plan.h"

#include <algorithm>

namespace icd::core::planner {

    namespace {

        // What one window would cost.
        struct WindowCost {
            std::vector<FileIndex> blockers; // Files to move out.
            ClusterCount evicted = 0; // Their movable clusters.
        };

        // The part of run inside window.
        ClusterRun clipTo(ClusterRun run, ClusterRun window) {
            const Lcn start = std::max(run.start, window.start);
            const Lcn end = std::min(run.end(), window.end());
            return {start, end > start ? end - start : 0};
        }

        // Whether a run overlaps any of the LCN-sorted runs.
        bool overlapsAny(ClusterRun run, std::span<const ClusterRun> sorted) {
            const auto it = std::ranges::upper_bound(sorted, run.start, {}, &ClusterRun::end);
            return it != sorted.end() && it->start < run.end();
        }

        // Adds a file to the blockers once.
        void addBlocker(const Snapshot& snapshot, FileIndex file, WindowCost& cost) {
            if (std::ranges::find(cost.blockers, file) == cost.blockers.end()) {
                cost.blockers.push_back(file);
                cost.evicted += movableClusters(snapshot, file);
            }
        }

        // Whether part lies inside one of the window's holes, walking past (and counting into `covered`) the holes
        // before it; parts come in LCN order.
        bool insideHoles(ClusterRun part, HoleSet::Walk& holes, ClusterCount& covered) {
            for (auto h = holes.hole(); h && h->end() <= part.start; h = holes.hole()) {
                covered += h->length;
                holes.next();
            }
            const auto h = holes.hole();
            return h && h->start <= part.start && part.end() <= h->end();
        }

        // What costing a window reads.
        struct WindowQuery {
            const Snapshot& snapshot; // The volume.
            const std::function<bool(FileIndex)>& mayEvict; // Which files may move.
            ClusterCount cap; // Most clusters the window may evict.
        };

        // Where costing a window stands.
        struct WindowScan {
            HoleSet::Walk holes; // The window's holes, walked as far as the extents reached.
            ClusterCount covered = 0; // Clusters of the holes passed so far, plus the blockers'.
            WindowCost cost{}; // The blockers so far.
        };

        // Accounts for one extent of the window: free after the plan (inside a hole), or a blocker that may move; false
        // when it may not move or the window now evicts more than the cap.
        bool accountExtent(const WindowQuery& q, const ExtentRecord& r, ClusterRun part, WindowScan& scan) {
            if (insideHoles(part, scan.holes, scan.covered)) {
                return true; // Vacated by a moving file: counted as free already.
            }
            if ((r.flags() & ExtentUnmovable) != 0 || !q.mayEvict(r.file())) {
                return false;
            }
            scan.covered += part.length;
            addBlocker(q.snapshot, r.file(), scan.cost);
            return scan.cost.evicted <= q.cap;
        }

        // The window's cost, or nullopt when something in it may not move or it would evict more than `cap` clusters
        // (the request's limit, or less than the best window so far: such a window can never be chosen).
        std::optional<WindowCost> costOf(const Snapshot& snapshot, const HoleSet& pool, ClusterRun window,
                                         const RoomRequest& request, const std::function<bool(FileIndex)>& mayEvict,
                                         ClusterCount cap) {
            if (overlapsAny(window, request.own)) {
                return std::nullopt;
            }
            const WindowQuery query{.snapshot = snapshot, .mayEvict = mayEvict, .cap = cap};
            WindowScan scan{.holes = HoleSet::Walk(pool, window)};
            for (const ExtentRecord& r : snapshot.extents.overlapping(window)) {
                if (!accountExtent(query, r, clipTo(r.run(), window), scan)) {
                    return std::nullopt;
                }
            }
            for (auto h = scan.holes.hole(); h; h = scan.holes.hole()) {
                scan.covered += h->length;
                scan.holes.next();
            }
            if (scan.covered != window.length) {
                return std::nullopt;
            }
            std::ranges::sort(scan.cost.blockers);
            return std::move(scan.cost);
        }

        // Window starts in a piece: hole starts and starts of files that may move, lowest first.
        std::vector<Lcn> startsIn(const HoleSet& pool, ClusterRun piece, const RoomRequest& request,
                                  std::size_t maxStarts) {
            const ClusterCount clusters = request.clusters;
            // Windows that start at a hole or end where one ends, walking holes from the piece's start (lazily: huge
            // pieces hold millions of holes).
            std::vector<Lcn> starts;
            ClusterRun window = piece;
            while (starts.size() < maxStarts) {
                const auto hole = pool.nextFit(1, window);
                if (!hole) {
                    break;
                }
                starts.push_back(hole->start);
                if (hole->end() >= piece.start + clusters) {
                    starts.push_back(hole->end() - clusters);
                }
                window = {hole->end(), piece.end() > hole->end() ? piece.end() - hole->end() : 0};
            }
            std::erase_if(starts, [&](Lcn s) { return s < piece.start; });
            std::ranges::sort(starts);
            starts.erase(std::ranges::unique(starts).begin(), starts.end());
            if (starts.size() > maxStarts) {
                starts.resize(maxStarts);
            }
            return starts;
        }

        // The search's fixed inputs.
        struct Search {
            const RoomRequest& request; // What is needed.
            const std::function<bool(FileIndex)>& mayEvict; // Which files may move.
            std::size_t maxStarts; // Starts tried per piece.
            Executor* executor; // Costs a piece's windows in parallel; null: inline.
        };

        // Most evicted clusters a window may cost and still beat best.
        ClusterCount capFor(const Search& search, const std::optional<Room>& best) {
            return best ? std::min(search.request.maxEvict, best->evicted - 1) : search.request.maxEvict;
        }

        // Makes a costed window the best when it evicts less than the best so far (the earliest of equals stays).
        void keepIfBetter(std::optional<WindowCost>& cost, ClusterRun window, std::optional<Room>& best) {
            if (!cost || (best && cost->evicted >= best->evicted)) {
                return;
            }
            best = Room{.window = window, .blockers = std::move(cost->blockers), .evicted = cost->evicted};
        }

        // Keeps the cheapest window of one region piece in best, trying the starts one after another.
        void bestInOrder(const Snapshot& snapshot, const HoleSet& pool, ClusterRun piece, const Search& search,
                         std::span<const Lcn> starts, std::optional<Room>& best) {
            for (const Lcn start : starts) {
                if (best && best->evicted == 0) {
                    return; // Nothing evicts less.
                }
                if (start + search.request.clusters > piece.end()) {
                    continue;
                }
                const ClusterRun window{start, search.request.clusters};
                auto cost = costOf(snapshot, pool, window, search.request, search.mayEvict, capFor(search, best));
                keepIfBetter(cost, window, best);
            }
        }

        // The same choice with every window costed in parallel first (against the best of earlier pieces): the
        // earliest window of least cost wins either way, the order only lets the serial walk skip losing windows.
        void bestInParallel(const Snapshot& snapshot, const HoleSet& pool, ClusterRun piece, const Search& search,
                            std::span<const Lcn> starts, std::optional<Room>& best) {
            const ClusterCount cap = capFor(search, best);
            std::vector<std::optional<WindowCost>> costs(starts.size());
            const auto costRange = [&](std::size_t begin, std::size_t end) {
                for (std::size_t i = begin; i < end; ++i) {
                    if (starts[i] + search.request.clusters <= piece.end()) {
                        const ClusterRun window{starts[i], search.request.clusters};
                        costs[i] = costOf(snapshot, pool, window, search.request, search.mayEvict, cap);
                    }
                }
            };
            search.executor->parallelFor({.count = starts.size(), .grain = 4}, costRange);
            for (std::size_t i = 0; i < starts.size() && !(best && best->evicted == 0); ++i) {
                keepIfBetter(costs[i], {starts[i], search.request.clusters}, best);
            }
        }

        // Keeps the cheapest window of one region piece in best.
        void bestInPiece(const Snapshot& snapshot, const HoleSet& pool, ClusterRun piece, const Search& search,
                         std::optional<Room>& best) {
            constexpr std::size_t parallelStarts = 16; // Fewer windows are not worth handing out.
            if (best && best->evicted == 0) {
                return;
            }
            const std::vector<Lcn> starts = startsIn(pool, piece, search.request, search.maxStarts);
            if (search.executor == nullptr || starts.size() < parallelStarts) {
                bestInOrder(snapshot, pool, piece, search, starts, best);
            }
            else {
                bestInParallel(snapshot, pool, piece, search, starts, best);
            }
        }

    } // namespace

    std::optional<Room> findRoom(const Snapshot& snapshot, const HoleSet& pool, const RoomRequest& request,
                                 const std::function<bool(FileIndex)>& mayEvict, std::size_t maxStarts,
                                 Executor* executor) {
        std::optional<Room> best;
        std::size_t budget = maxStarts;
        for (const ClusterRun& piece : request.region) {
            if (budget == 0) {
                break;
            }
            if (piece.length >= request.clusters) {
                bestInPiece(snapshot, pool, piece, {request, mayEvict, budget, executor}, best);
                budget -= std::min(budget, std::max<std::size_t>(1, budget / 4));
            }
        }
        return best;
    }

} // namespace icd::core::planner
