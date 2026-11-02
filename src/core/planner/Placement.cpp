#include "core/planner/Placement.h"

#include <algorithm>

namespace icd::core::planner {

    namespace {

        // The parts of a hole not covered by the file's own runs, in LCN order.
        std::vector<ClusterRun> piecesOf(ClusterRun hole, std::span<const ClusterRun> own) {
            std::vector<ClusterRun> out;
            Lcn cursor = hole.start;
            auto it = std::ranges::upper_bound(own, hole.start, {}, &ClusterRun::end);
            for (; it != own.end() && it->start < hole.end(); ++it) {
                if (it->start > cursor) {
                    out.push_back({cursor, it->start - cursor});
                }
                cursor = std::max(cursor, it->end());
            }
            if (cursor < hole.end()) {
                out.push_back({cursor, hole.end() - cursor});
            }
            return out;
        }

        // The window [from, window.end()).
        ClusterRun tailOf(ClusterRun window, Lcn from) {
            from = std::clamp(from, window.start, window.end());
            return {from, window.end() - from};
        }

        // A placement inside the hole holding `near` that covers near itself (distance 0), when the hole is big enough.
        std::optional<ClusterRun> aroundPoint(const HoleSet& pool, const SpaceRequest& request, Lcn near) {
            const auto hole = pool.holeAt(near);
            if (!hole) {
                return std::nullopt;
            }
            const Lcn start = std::max(hole->start, request.window.start);
            const Lcn end = std::min(hole->end(), request.window.end());
            for (const ClusterRun& piece : piecesOf({start, end > start ? end - start : 0}, request.own)) {
                if (piece.length >= request.clusters && piece.contains(near)) {
                    return ClusterRun{std::min(near, piece.end() - request.clusters), request.clusters};
                }
            }
            return std::nullopt;
        }

        // Takes the start of a free piece for a file in large pieces and returns the clusters taken. It never leaves a
        // remainder smaller than a piece may be: it takes less now so the last piece is big enough.
        ClusterCount takePiece(ClusterRun piece, ClusterCount left, ClusterCount minPiece,
                               std::vector<ClusterRun>& out) {
            ClusterCount take = std::min(piece.length, left);
            if (left - take != 0 && left - take < minPiece) {
                take = left > minPiece ? left - minPiece : 0;
            }
            if (take < minPiece) {
                return 0;
            }
            out.push_back({piece.start, take});
            return take;
        }

    } // namespace

    std::optional<ClusterRun> findFirst(const HoleSet& pool, const SpaceRequest& request) {
        ClusterRun window = request.window;
        while (const auto hole = pool.nextFit(request.clusters, window)) {
            for (const ClusterRun& piece : piecesOf(*hole, request.own)) {
                if (piece.length >= request.clusters) {
                    return ClusterRun{piece.start, request.clusters};
                }
            }
            window = tailOf(window, hole->end());
        }
        return std::nullopt;
    }

    std::optional<ClusterRun> findLast(const HoleSet& pool, const SpaceRequest& request) {
        ClusterRun window = request.window;
        while (const auto hole = pool.prevFit(request.clusters, window)) {
            const auto pieces = piecesOf(*hole, request.own);
            for (auto it = pieces.rbegin(); it != pieces.rend(); ++it) {
                if (it->length >= request.clusters) {
                    return ClusterRun{it->end() - request.clusters, request.clusters};
                }
            }
            window = {window.start, hole->start - window.start};
        }
        return std::nullopt;
    }

    std::optional<ClusterRun> findNear(const HoleSet& pool, const SpaceRequest& request, Lcn near) {
        near = std::clamp(near, request.window.start, request.window.end());
        if (const auto around = aroundPoint(pool, request, near)) {
            return around;
        }
        const ClusterRun after = tailOf(request.window, near);
        const ClusterRun before{request.window.start, near - request.window.start};
        const auto forward = findFirst(pool, {request.clusters, request.own, after});
        const auto backward = findLast(pool, {request.clusters, request.own, before});
        if (!backward) {
            return forward;
        }
        if (!forward) {
            return backward;
        }
        return forward->start - near <= near - backward->end() ? forward : backward;
    }

    std::optional<std::vector<ClusterRun>> findPieces(const HoleSet& pool, const SpaceRequest& request,
                                                      ClusterCount minPiece) {
        minPiece = std::max<ClusterCount>(minPiece, 1);
        std::vector<ClusterRun> out;
        ClusterCount left = request.clusters;
        ClusterRun window = request.window;
        while (left > 0) {
            const auto hole = pool.nextFit(minPiece, window);
            if (!hole) {
                return std::nullopt;
            }
            for (const ClusterRun& piece : piecesOf(*hole, request.own)) {
                left -= takePiece(piece, left, minPiece, out);
            }
            window = tailOf(window, hole->end());
        }
        return out;
    }

    void takeRuns(HoleSet& pool, std::span<const ClusterRun> runs) {
        for (const ClusterRun& r : runs) {
            (void)pool.take(r);
        }
    }

    void releaseRuns(HoleSet& pool, std::span<const ClusterRun> runs) {
        for (const ClusterRun& r : runs) {
            (void)pool.add(r);
        }
    }

    void appendMoves(const Snapshot& snapshot, std::span<const Destination> destinations, Plan& plan) {
        for (const Destination& d : destinations) {
            plan.add({.file = d.file, .reason = d.reason, .zone = d.zone}, movableRuns(snapshot, d.file), d.runs);
        }
    }

    std::vector<ClusterRun> sortedRuns(std::span<const ClusterRun> runs) {
        std::vector<ClusterRun> out(runs.begin(), runs.end());
        std::ranges::sort(out, {}, &ClusterRun::start);
        return out;
    }

} // namespace icd::core::planner
