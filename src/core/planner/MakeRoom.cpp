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
                cost.evicted += clustersOf(movableRuns(snapshot, file));
            }
        }

        // The window's cost, or nullopt when something in it may not move.
        std::optional<WindowCost> costOf(const Snapshot& snapshot, const HoleSet& pool, ClusterRun window,
                                         const RoomRequest& request, const std::function<bool(FileIndex)>& mayEvict) {
            if (overlapsAny(window, request.own)) {
                return std::nullopt;
            }
            ClusterCount covered = clustersOf(pool.within(window));
            WindowCost cost;
            for (const ExtentRecord& r : snapshot.extents.overlapping(window)) {
                const ClusterRun part = clipTo(r.run(), window);
                if (pool.contains(part)) {
                    continue; // Vacated by a moving file: counted as free already.
                }
                if ((r.flags() & ExtentUnmovable) != 0 || !mayEvict(r.file())) {
                    return std::nullopt;
                }
                covered += part.length;
                addBlocker(snapshot, r.file(), cost);
            }
            if (covered != window.length || cost.evicted > request.maxEvict) {
                return std::nullopt;
            }
            std::ranges::sort(cost.blockers);
            return cost;
        }

        // Window starts in a piece: hole starts and starts of files that may move, lowest first.
        std::vector<Lcn> startsIn(const Snapshot& snapshot, const HoleSet& pool, ClusterRun piece,
                                  const std::function<bool(FileIndex)>& mayEvict, std::size_t maxStarts) {
            std::vector<Lcn> starts;
            for (const ClusterRun& hole : pool.within(piece)) {
                starts.push_back(hole.start);
            }
            for (const ExtentRecord& r : snapshot.extents.overlapping(piece)) {
                if (r.lcn() >= piece.start && mayEvict(r.file())) {
                    starts.push_back(r.lcn());
                }
            }
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
        };

        // Keeps the cheapest window of one region piece in best.
        void bestInPiece(const Snapshot& snapshot, const HoleSet& pool, ClusterRun piece, const Search& search,
                         std::optional<Room>& best) {
            for (const Lcn start : startsIn(snapshot, pool, piece, search.mayEvict, search.maxStarts)) {
                if (start + search.request.clusters > piece.end()) {
                    return;
                }
                const ClusterRun window{start, search.request.clusters};
                const auto cost = costOf(snapshot, pool, window, search.request, search.mayEvict);
                if (cost && (!best || cost->evicted < best->evicted)) {
                    best = Room{.window = window, .blockers = cost->blockers, .evicted = cost->evicted};
                }
            }
        }

    } // namespace

    std::optional<Room> findRoom(const Snapshot& snapshot, const HoleSet& pool, const RoomRequest& request,
                                 const std::function<bool(FileIndex)>& mayEvict, std::size_t maxStarts) {
        std::optional<Room> best;
        for (const ClusterRun& piece : request.region) {
            if (piece.length >= request.clusters) {
                bestInPiece(snapshot, pool, piece, {request, mayEvict, maxStarts}, best);
            }
        }
        return best;
    }

} // namespace icd::core::planner
