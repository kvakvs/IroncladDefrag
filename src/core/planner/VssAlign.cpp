#include "core/planner/VssAlign.h"

#include <algorithm>
#include <array>
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
