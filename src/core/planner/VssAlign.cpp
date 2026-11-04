#include "core/planner/VssAlign.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <unordered_set>
#include <utility>

namespace icd::core::planner {

    namespace {

        // a - b modulo m, in [0, m).
        ClusterCount modDiff(Lcn a, Lcn b, ClusterCount m) { return a >= b ? (a - b) % m : (m - (b - a) % m) % m; }

        // Whether a run overlaps any of the LCN-sorted runs.
        bool hitsOwn(ClusterRun run, std::span<const ClusterRun> own) {
            const auto it = std::ranges::upper_bound(own, run.start, {}, &ClusterRun::end);
            return it != own.end() && it->start < run.end();
        }

        // What shifting one destination needs.
        struct Shift {
            ClusterRun moved; // The destination after the shift.
            ClusterRun freed; // Clusters it gives back.
            ClusterRun taken; // Clusters it takes from the pool.
        };

        // The shift forward or backward by `by` clusters.
        Shift shiftOf(ClusterRun run, ClusterCount by, bool forward) {
            if (forward) {
                return {{run.start + by, run.length}, {run.start, by}, {run.end(), by}};
            }
            return {{run.start - by, run.length}, {run.end() - by, by}, {run.start - by, by}};
        }

        // Whether a shift is possible: the space is free, the file stays in its zone and off its own clusters.
        bool shiftFits(const Shift& s, const HoleSet& pool, const Destination& d, std::span<const ClusterRun> own,
                       const TargetLayout* layout) {
            if (!pool.contains(s.taken) || hitsOwn(s.moved, own)) {
                return false;
            }
            const ZoneTarget* target = layout != nullptr && d.zone != noZone ? layout->target(d.zone) : nullptr;
            return target == nullptr || target->holds(s.moved);
        }

        // Applies the first shift that fits, smaller one first.
        bool tryShifts(Destination& d, HoleSet& pool, const TargetLayout* layout, std::span<const ClusterRun> own,
                       std::array<std::pair<ClusterCount, bool>, 2> options) {
            const ClusterRun run = d.runs.front();
            for (const auto& [by, forward] : options) {
                if (by == 0 || (!forward && by > run.start)) {
                    continue;
                }
                const Shift s = shiftOf(run, by, forward);
                if (shiftFits(s, pool, d, own, layout)) {
                    (void)pool.take(s.taken);
                    (void)pool.add(s.freed);
                    d.runs.front() = s.moved;
                    return true;
                }
            }
            return false;
        }

        // Aligns one destination if possible.
        bool alignOne(const Snapshot& snapshot, Destination& d, HoleSet& pool, const TargetLayout* layout,
                      ClusterCount unit) {
            const auto runs = movableRuns(snapshot, d.file);
            const VssGeometry geometry{.unit = unit, .bytesPerCluster = snapshot.geometry.bytesPerCluster};
            if (d.runs.size() != 1 || runs.empty() || movesAligned(runs, d.runs, geometry)) {
                return false;
            }
            const ClusterCount forward = modDiff(runs.front().start, d.runs.front().start, unit);
            const ClusterCount backward = modDiff(d.runs.front().start, runs.front().start, unit);
            const std::pair<ClusterCount, bool> ahead{forward, true};
            const std::pair<ClusterCount, bool> behind{backward, false};
            const auto order = forward <= backward ? std::array{ahead, behind} : std::array{behind, ahead};
            return tryShifts(d, pool, layout, sortedRuns(runs), order);
        }

        // Clusters taken out of play by dropped moves (their sources stay where they are), LCN-sorted and merged.
        class StayingRuns {
        public:
            // Adds the runs of a dropped move.
            void add(std::span<const ClusterRun> runs) {
                for (const ClusterRun& r : runs) {
                    ends_[r.start] = std::max(ends_[r.start], r.end());
                    maxLength_ = std::max(maxLength_, r.length);
                }
            }

            // Whether any of `runs` overlaps a staying run.
            [[nodiscard]] bool hits(std::span<const ClusterRun> runs) const {
                return std::ranges::any_of(runs, [&](const ClusterRun& r) {
                    auto it = ends_.lower_bound(r.end());
                    while (it != ends_.begin()) {
                        --it;
                        if (it->second > r.start) {
                            return true;
                        }
                        if (it->first + maxLength_ <= r.start) {
                            break;
                        }
                    }
                    return false;
                });
            }

        private:
            std::map<Lcn, Lcn> ends_; // Start -> furthest end of the runs starting there.
            ClusterCount maxLength_ = 0; // Longest run added: no run starting further back can reach a query.
        };

        // What dropping has decided so far.
        struct Dropping {
            std::vector<std::uint8_t> dropped; // Per move.
            std::vector<std::uint8_t> unaligned; // Per move: dropped for itself (else for a move it waited on).
            std::unordered_set<FileIndex> files; // Files with a dropped move: all their moves go.
        };

        // One pass in plan order: a move goes when its file lost a move or it lands on the sources of a dropped one.
        // Returns whether anything new was dropped (a parked file's earlier move may need another pass).
        bool dropForward(const Plan& plan, Dropping& d) {
            StayingRuns staying;
            bool changed = false;
            for (std::size_t i = 0; i < plan.moves.size(); ++i) {
                const Move& m = plan.moves[i];
                if (d.dropped[i] == 0 && (d.files.contains(m.file) || staying.hits(plan.destinations(m)))) {
                    d.dropped[i] = 1;
                    d.files.insert(m.file);
                    changed = true;
                }
                if (d.dropped[i] != 0) {
                    staying.add(plan.sources(m));
                }
            }
            return changed;
        }

        // The plan without the dropped moves; needed ones become skipped files.
        void removeDropped(Plan& plan, const Dropping& d) {
            const std::vector<Move> moves = std::exchange(plan.moves, {});
            const std::vector<ClusterRun> runs = std::exchange(plan.runs, {});
            plan.extra = 0;
            for (std::size_t i = 0; i < moves.size(); ++i) {
                const Move& m = moves[i];
                if (d.dropped[i] == 0) {
                    plan.add(m, std::span(runs).subspan(m.firstSource, m.sourceCount),
                             std::span(runs).subspan(m.firstDestination, m.destinationCount));
                    plan.extra += m.extra ? m.clusters : 0;
                }
                else if (!m.extra) {
                    const SkipReason reason = d.unaligned[i] != 0 ? SkipReason::Unaligned : SkipReason::Dependency;
                    plan.skipped.push_back({.file = m.file, .reason = reason, .clusters = m.clusters});
                    plan.skippedClusters += m.clusters;
                }
            }
        }

    } // namespace

    ClusterCount vssUnit(std::uint32_t bytesPerCluster) {
        return std::max<ClusterCount>(1, vssBlockBytes / std::max<std::uint32_t>(bytesPerCluster, 1));
    }

    bool movesAligned(std::span<const ClusterRun> sources, std::span<const ClusterRun> destinations,
                      const VssGeometry& geometry) {
        const ClusterCount unit = geometry.unit;
        if (clustersOf(sources) * geometry.bytesPerCluster < vssBlockBytes) {
            return false;
        }
        std::size_t si = 0;
        std::size_t di = 0;
        ClusterCount so = 0; // Offset into sources[si].
        ClusterCount dOff = 0; // Offset into destinations[di].
        while (si < sources.size() && di < destinations.size()) {
            if (modDiff(destinations[di].start + dOff, sources[si].start + so, unit) != 0) {
                return false;
            }
            const ClusterCount step = std::min(sources[si].length - so, destinations[di].length - dOff);
            so += step;
            dOff += step;
            if (so == sources[si].length) {
                ++si;
                so = 0;
            }
            if (dOff == destinations[di].length) {
                ++di;
                dOff = 0;
            }
        }
        return true;
    }

    VssReport vssReport(const Plan& plan, std::uint32_t bytesPerCluster) {
        VssReport out;
        const ClusterCount unit = vssUnit(bytesPerCluster);
        for (const Move& m : plan.moves) {
            ++out.moves;
            if (!movesAligned(plan.sources(m), plan.destinations(m),
                              {.unit = unit, .bytesPerCluster = bytesPerCluster})) {
                ++out.unaligned;
                out.growthBytes += m.clusters * bytesPerCluster;
            }
        }
        return out;
    }

    std::size_t dropUnaligned(Plan& plan, std::uint32_t bytesPerCluster) {
        const VssGeometry geometry{.unit = vssUnit(bytesPerCluster), .bytesPerCluster = bytesPerCluster};
        Dropping d{.dropped = std::vector<std::uint8_t>(plan.moves.size(), 0),
                   .unaligned = std::vector<std::uint8_t>(plan.moves.size(), 0),
                   .files = {}};
        for (std::size_t i = 0; i < plan.moves.size(); ++i) {
            const Move& m = plan.moves[i];
            if (!movesAligned(plan.sources(m), plan.destinations(m), geometry)) {
                d.dropped[i] = 1;
                d.unaligned[i] = 1;
                d.files.insert(m.file);
            }
        }
        while (dropForward(plan, d)) {}
        const auto count = static_cast<std::size_t>(std::ranges::count(d.dropped, 1));
        if (count != 0) {
            removeDropped(plan, d);
        }
        return count;
    }

    void blockUnalignedMoves(Plan& plan, std::uint32_t bytesPerCluster) {
        if (const std::size_t dropped = dropUnaligned(plan, bytesPerCluster); dropped != 0) {
            plan.notes.push_back(std::format(
                "{} moves are left out because they are not aligned to shadow-copy blocks (or wait on one).", dropped));
        }
    }

    std::size_t alignDestinations(const Snapshot& snapshot, std::vector<Destination>& destinations, HoleSet& pool,
                                  const TargetLayout* layout) {
        const ClusterCount unit = vssUnit(snapshot.geometry.bytesPerCluster);
        if (unit == 1) {
            return 0;
        }
        std::size_t aligned = 0;
        for (Destination& d : destinations) {
            aligned += alignOne(snapshot, d, pool, layout, unit) ? 1 : 0;
        }
        return aligned;
    }

} // namespace icd::core::planner
