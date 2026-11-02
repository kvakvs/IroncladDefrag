#include "core/execute/MoveCalls.h"

#include <algorithm>
#include <utility>

namespace icd::core::execute {

    namespace {

        // Calls of at most `chunk` clusters for one piece of a plain file.
        void plainCalls(const MovePiece& p, ClusterCount chunk, std::vector<MoveCall>& out) {
            for (ClusterCount k = 0; k < p.length; k += chunk) {
                const ClusterCount n = std::min(chunk, p.length - k);
                out.push_back({p.stream, p.vcn + k, n, p.to + k, n});
            }
        }

        // The end of a compressed call: the end of its last piece rounded up to a unit boundary, so it moves whole
        // units, holes included.
        std::uint64_t unitEnd(std::uint64_t vcnEnd) {
            return (vcnEnd + compressionUnitClusters - 1) / compressionUnitClusters * compressionUnitClusters;
        }

        // Whether a piece of a compressed file extends the open call: same stream, after it, its destination
        // continuing the call's, and no cut wanted (a cut needs a unit boundary and a call already at its size).
        bool joins(const MoveCall& call, const MovePiece& p, ClusterCount chunk) {
            const bool continues =
                p.stream == call.stream && p.vcn >= call.vcn + call.span && p.to == call.to + call.allocated;
            const bool cut = p.vcn % compressionUnitClusters == 0 && p.vcn + p.length - call.vcn > chunk;
            return continues && !cut;
        }

        // Rounds each compressed call up to its last unit's end, but never into the next piece of the same stream
        // (`last` gives each call's last piece).
        void roundToUnits(std::vector<MoveCall>& calls, std::span<const std::size_t> last,
                          std::span<const MovePiece> pieces) {
            for (std::size_t i = 0; i < calls.size(); ++i) {
                const std::size_t next = last[i] + 1;
                const bool bounded = next < pieces.size() && pieces[next].stream == calls[i].stream;
                const std::uint64_t end = unitEnd(calls[i].vcn + calls[i].span);
                calls[i].span = (bounded ? std::min(end, pieces[next].vcn) : end) - calls[i].vcn;
            }
        }

        // Calls of a compressed file. Pieces next to each other in the list join over the holes between them; each
        // call then reaches up to its last unit's end, but never into the next piece of the same stream (which may be
        // done already, or go elsewhere).
        std::vector<MoveCall> compressedCalls(std::span<const MovePiece> pieces, ClusterCount chunk) {
            std::vector<MoveCall> out;
            std::vector<std::size_t> last; // Per call: index of its last piece.
            for (std::size_t k = 0; k < pieces.size(); ++k) {
                const MovePiece& p = pieces[k];
                if (p.done || p.length == 0) {
                    continue;
                }
                if (!out.empty() && last.back() + 1 == k && joins(out.back(), p, chunk)) {
                    out.back().span = p.vcn + p.length - out.back().vcn;
                    out.back().allocated += p.length;
                    last.back() = k;
                    continue;
                }
                out.push_back({p.stream, p.vcn, p.length, p.to, p.length});
                last.push_back(k);
            }
            roundToUnits(out, last, pieces);
            return out;
        }

        // Whether a run lies before a stretch in (stream, VCN) order (it ends before the stretch starts).
        bool endsBefore(const StreamRun& r, const Landing& l) {
            return r.stream < l.stream || (r.stream == l.stream && r.vcn + r.length <= l.vcn);
        }

        // The runs of `after` inside a landing's window, clipped to it, joined where they continue on disk.
        std::vector<ClusterRun> clipped(std::span<const StreamRun> after, const Landing& l) {
            const auto first =
                std::ranges::partition_point(after, [&](const StreamRun& r) { return endsBefore(r, l); });
            std::vector<ClusterRun> out;
            for (auto r = first; r != after.end() && r->stream == l.stream && r->vcn < l.vcn + l.span; ++r) {
                const std::uint64_t from = std::max(r->vcn, l.vcn);
                const std::uint64_t to = std::min(r->vcn + r->length, l.vcn + l.span);
                const ClusterRun piece{r->lcn + (from - r->vcn), to - from};
                if (!out.empty() && out.back().end() == piece.start) {
                    out.back().length += piece.length;
                }
                else {
                    out.push_back(piece);
                }
            }
            return out;
        }

    } // namespace

    std::vector<MoveCall> moveCalls(std::span<const MovePiece> pieces, CallShape shape) {
        const ClusterCount chunk = std::max<ClusterCount>(shape.chunkClusters, 1);
        if (shape.compressed) {
            return compressedCalls(pieces, chunk);
        }
        std::vector<MoveCall> out;
        for (const MovePiece& p : pieces) {
            if (!p.done) {
                plainCalls(p, chunk, out);
            }
        }
        return out;
    }

    std::vector<ClusterRun> retarget(std::span<MoveCall> calls, Lcn start) {
        std::vector<ClusterRun> out;
        Lcn at = start;
        for (MoveCall& c : calls) {
            c.to = at;
            at += c.allocated;
        }
        if (at > start) {
            out.push_back({start, at - start});
        }
        return out;
    }

    Landing landingOf(const MoveCall& call) { return {call.stream, call.vcn, call.span, call.to, call.allocated}; }

    Landing landingOf(const MovePiece& piece) {
        return {piece.stream, piece.vcn, piece.length, piece.to, piece.length};
    }

    bool landed(std::span<const StreamRun> after, std::span<const Landing> landings) {
        return std::ranges::all_of(landings, [&](const Landing& l) {
            const std::vector<ClusterRun> runs = clipped(after, l);
            return l.allocated == 0 ? runs.empty() : runs == std::vector<ClusterRun>{{l.to, l.allocated}};
        });
    }

    LandedPrefix landedPrefix(std::span<const StreamRun> now, const MoveCall& call) {
        LandedPrefix out{0, call.vcn};
        const Landing window{call.stream, call.vcn, call.span, call.to, call.allocated};
        const auto first = std::ranges::partition_point(now, [&](const StreamRun& r) { return endsBefore(r, window); });
        for (auto r = first; r != now.end() && r->stream == call.stream && r->vcn < call.vcn + call.span; ++r) {
            const std::uint64_t from = std::max(r->vcn, call.vcn);
            const std::uint64_t to = std::min(r->vcn + r->length, call.vcn + call.span);
            if (r->lcn + (from - r->vcn) != call.to + out.allocated) {
                break; // The rest still lies elsewhere.
            }
            out.allocated += to - from;
            out.vcnEnd = to;
        }
        return out;
    }

    MoveCall remainderOf(const MoveCall& call, LandedPrefix landed) {
        return {call.stream, landed.vcnEnd, call.vcn + call.span - landed.vcnEnd, call.to + landed.allocated,
                call.allocated - landed.allocated};
    }

    ClusterCount allocatedOf(std::span<const MoveCall> calls) {
        ClusterCount sum = 0;
        for (const MoveCall& c : calls) {
            sum += c.allocated;
        }
        return sum;
    }

} // namespace icd::core::execute
