#include "core/execute/MovePieces.h"

#include "core/planner/Plan.h"

#include <algorithm>
#include <utility>

namespace icd::core::execute {

    namespace {

        // A stretch where one source run and one destination run line up.
        struct Segment {
            Lcn source = 0; // First source cluster.
            Lcn destination = 0; // First destination cluster.
            ClusterCount length = 0; // Clusters.
        };

        // A position inside a run list: run index and clusters of that run already passed.
        struct RunCursor {
            std::span<const ClusterRun> runs; // Walked.
            std::size_t index = 0; // Current run.
            ClusterCount offset = 0; // Clusters of it passed.

            // Whether every run has been passed.
            [[nodiscard]] bool atEnd() const { return index >= runs.size(); }

            // The current cluster.
            [[nodiscard]] Lcn here() const { return runs[index].start + offset; }

            // Clusters left in the current run.
            [[nodiscard]] ClusterCount left() const { return runs[index].length - offset; }

            // Passes n clusters of the current run (at most left()).
            void advance(ClusterCount n) {
                offset += n;
                if (offset == runs[index].length) {
                    ++index;
                    offset = 0;
                }
            }
        };

        // The move cut where either list changes run; nullopt when the lists hold different cluster counts.
        std::optional<std::vector<Segment>> segmentsOf(MoveRuns move) {
            if (planner::clustersOf(move.sources) != planner::clustersOf(move.destinations)) {
                return std::nullopt;
            }
            std::vector<Segment> out;
            RunCursor s{move.sources};
            RunCursor d{move.destinations};
            while (!s.atEnd() && !d.atEnd()) {
                const ClusterCount take = std::min(s.left(), d.left());
                out.push_back({s.here(), d.here(), take});
                s.advance(take);
                d.advance(take);
            }
            std::erase_if(out, [](const Segment& x) { return x.length == 0; });
            return out;
        }

        // Walks a file's runs along the segments of a move and records where each stretch lies.
        class Matcher {
        public:
            // Matches against these segments.
            explicit Matcher(std::vector<Segment> segments) : segments_(std::move(segments)) {}

            // Takes the next run of the file; false when it lies at neither place or runs past the move.
            bool add(const StreamRun& run) {
                for (ClusterCount pos = 0; pos < run.length;) {
                    if (segment_ == segments_.size()) {
                        return false;
                    }
                    const Segment& s = segments_[segment_];
                    const ClusterCount take = std::min(run.length - pos, s.length - offset_);
                    if (!place(run, pos, take, s)) {
                        return false;
                    }
                    pos += take;
                    advance(take);
                }
                return true;
            }

            // The pieces, or nullopt when the file ended before the move did.
            [[nodiscard]] std::optional<std::vector<MovePiece>> finish() && {
                if (segment_ != segments_.size()) {
                    return std::nullopt;
                }
                return std::move(pieces_);
            }

        private:
            // Records `take` clusters of `run` from `pos`; false when they are neither at the source nor done.
            bool place(const StreamRun& run, ClusterCount pos, ClusterCount take, const Segment& s) {
                const Lcn at = run.lcn + pos;
                const Lcn from = s.source + offset_;
                const Lcn to = s.destination + offset_;
                if (at != from && at != to) {
                    return false;
                }
                append({run.stream, run.vcn + pos, from, to, take, at == to});
                return true;
            }

            // Appends a piece, joined to the last one when it continues it in every respect.
            void append(const MovePiece& p) {
                if (!pieces_.empty() && continues(pieces_.back(), p)) {
                    pieces_.back().length += p.length;
                    return;
                }
                pieces_.push_back(p);
            }

            // Whether b continues a in the stream, at the source, at the destination and in state.
            static bool continues(const MovePiece& a, const MovePiece& b) {
                return a.stream == b.stream && a.done == b.done && a.vcn + a.length == b.vcn &&
                    a.from + a.length == b.from && a.to + a.length == b.to;
            }

            // Moves the segment cursor on by n clusters.
            void advance(ClusterCount n) {
                offset_ += n;
                if (offset_ == segments_[segment_].length) {
                    ++segment_;
                    offset_ = 0;
                }
            }

            std::vector<Segment> segments_; // The move.
            std::size_t segment_ = 0; // Current segment.
            ClusterCount offset_ = 0; // Clusters of it already matched.
            std::vector<MovePiece> pieces_; // Output.
        };

    } // namespace

    std::optional<std::vector<MovePiece>> matchMove(std::span<const StreamRun> now, MoveRuns move) {
        auto segments = segmentsOf(move);
        if (!segments) {
            return std::nullopt;
        }
        Matcher matcher(std::move(*segments));
        for (const StreamRun& run : now) {
            if (!matcher.add(run)) {
                return std::nullopt;
            }
        }
        return std::move(matcher).finish();
    }

    std::optional<std::vector<MovePiece>> remapMove(std::span<const StreamRun> now,
                                                    std::span<const ClusterRun> destinations) {
        std::vector<ClusterRun> here;
        here.reserve(now.size());
        for (const StreamRun& r : now) {
            here.push_back({r.lcn, r.length});
        }
        return matchMove(now, {here, destinations});
    }

    std::vector<ClusterRun> lcnRunsOf(std::span<const StreamRun> runs) {
        std::vector<ClusterRun> out;
        for (const StreamRun& r : runs) {
            if (r.length == 0) {
                continue;
            }
            if (!out.empty() && out.back().end() == r.lcn) {
                out.back().length += r.length;
                continue;
            }
            out.push_back({r.lcn, r.length});
        }
        return out;
    }

    std::vector<StreamRun> asStreamRuns(std::span<const ClusterRun> runs) {
        std::vector<StreamRun> out;
        out.reserve(runs.size());
        std::uint64_t vcn = 0;
        for (const ClusterRun& r : runs) {
            out.push_back({0, vcn, r.start, r.length});
            vcn += r.length;
        }
        return out;
    }

    bool allDone(std::span<const MovePiece> pieces) { return std::ranges::all_of(pieces, &MovePiece::done); }

    ClusterCount pendingClusters(std::span<const MovePiece> pieces) {
        ClusterCount sum = 0;
        for (const MovePiece& p : pieces) {
            sum += p.done ? 0 : p.length;
        }
        return sum;
    }

} // namespace icd::core::execute
