#include "core/planner/LowerBound.h"

#include "core/planner/Plan.h"

#include <algorithm>
#include <limits>

namespace icd::core::planner {

    namespace {

        // Files per parallel chunk.
        constexpr std::size_t fileGrain = 8192;

        // Where a file may sit for the slack before its zone: it stays for every slack in [lo, hi].
        struct StayInterval {
            std::size_t slot = 0; // Position of the zone among the forward zones.
            std::int64_t lo = 0; // Least slack that keeps the file inside its zone.
            std::int64_t hi = 0; // Most slack that keeps it inside.
            ClusterCount weight = 0; // Its movable clusters.
        };

        // A forward zone's place with no slack before it.
        struct ForwardZone {
            ZoneIndex zone = noZone; // Script index.
            std::int64_t start = 0; // First usable cluster number with no slack.
            std::int64_t length = 0; // Usable clusters.
            bool front = false; // Anchored at the front: never shifted.
        };

        // The forward zones of a layout made without slack, and the slot of each script zone among them.
        std::vector<ForwardZone> forwardZones(const TargetLayout& layout, const strategy::ZoneScript& script,
                                              std::vector<std::size_t>& slotOfZone) {
            std::vector<ForwardZone> out;
            slotOfZone.assign(script.zones.size(), static_cast<std::size_t>(-1));
            for (const ZoneTarget& t : layout.zones) {
                if (t.back) {
                    continue;
                }
                slotOfZone[t.zone] = out.size();
                out.push_back({.zone = t.zone,
                               .start = static_cast<std::int64_t>(t.start),
                               .length = static_cast<std::int64_t>(t.length),
                               .front = script.zones[t.zone].anchor == strategy::Anchor::Front});
            }
            return out;
        }

        // What fitting reads while collecting intervals.
        struct FitContext {
            const Snapshot& snapshot; // The volume.
            const strategy::ZoneScript& script; // Zones.
            const ZoneAssignment& assignment; // Zones and roles.
            const TargetLayout& layout; // The layout without slack.
            const std::vector<ForwardZone>& forward; // Forward zones.
            const std::vector<std::size_t>& slotOfZone; // Script zone -> forward slot.
            const GoodEnough& rules; // Strictness.
        };

        // The stay interval of one file, if it can stay at all.
        std::optional<StayInterval> intervalOf(const FitContext& c, FileIndex file) {
            const ZoneIndex zone = c.assignment.zoneOf[file];
            if (c.assignment.roles[file] != FileRole::Placed || c.slotOfZone[zone] >= c.forward.size()) {
                return std::nullopt;
            }
            const auto runs = movableRuns(c.snapshot, file);
            if (!fragmentsOk(runs, fragmentOkFor(c.script, zone, c.rules), c.snapshot.geometry.bytesPerCluster)) {
                return std::nullopt;
            }
            const auto span = usableSpan(c.snapshot, file, c.layout.axis);
            if (!span) {
                return std::nullopt;
            }
            const std::size_t slot = c.slotOfZone[zone];
            const ForwardZone& z = c.forward[slot];
            const auto slack = static_cast<std::int64_t>(c.layout.slack);
            const std::int64_t lo = std::max<std::int64_t>(0, span->second - z.start - z.length);
            const std::int64_t hi = std::min<std::int64_t>(z.front ? 0 : slack, span->first - z.start);
            if (lo > hi) {
                return std::nullopt;
            }
            return StayInterval{.slot = slot, .lo = lo, .hi = hi, .weight = c.assignment.movable[file]};
        }

        // Every stay interval, in file order (chunks combined in order, so any executor gives the same list).
        std::vector<StayInterval> collectIntervals(const FitContext& c, Executor& executor) {
            const std::size_t files = c.snapshot.files.size();
            const ChunkedRange range{.count = files, .grain = fileGrain};
            std::vector<std::vector<StayInterval>> parts(range.chunks());
            executor.parallelFor(range, [&](std::size_t begin, std::size_t end) {
                auto& part = parts[begin / fileGrain];
                for (std::size_t f = begin; f < end; ++f) {
                    if (auto interval = intervalOf(c, static_cast<FileIndex>(f))) {
                        part.push_back(*interval);
                    }
                }
            });
            std::vector<StayInterval> out;
            for (auto& part : parts) {
                out.insert(out.end(), part.begin(), part.end());
            }
            return out;
        }

        // Candidate slack values: 0 and every interval's low end (the optimum needs no others), thinned when many.
        std::vector<std::int64_t> candidateGrid(const std::vector<StayInterval>& intervals, std::size_t maxCandidates) {
            std::vector<std::int64_t> grid{0};
            grid.reserve(intervals.size() + 1);
            for (const StayInterval& i : intervals) {
                grid.push_back(i.lo);
            }
            std::ranges::sort(grid);
            grid.erase(std::ranges::unique(grid).begin(), grid.end());
            if (grid.size() <= maxCandidates || maxCandidates < 2) {
                return grid;
            }
            std::vector<std::int64_t> thin;
            thin.reserve(maxCandidates);
            for (std::size_t k = 0; k < maxCandidates; ++k) {
                thin.push_back(grid[k * (grid.size() - 1) / (maxCandidates - 1)]);
            }
            thin.erase(std::ranges::unique(thin).begin(), thin.end());
            return thin;
        }

        // Clusters that stay per forward zone and candidate (difference arrays, then prefix sums).
        std::vector<std::vector<ClusterCount>> stayingPerCandidate(const std::vector<StayInterval>& intervals,
                                                                   const std::vector<std::int64_t>& grid,
                                                                   std::size_t zones) {
            std::vector<std::vector<std::int64_t>> diff(zones, std::vector<std::int64_t>(grid.size() + 1, 0));
            for (const StayInterval& i : intervals) {
                const auto first = std::ranges::lower_bound(grid, i.lo) - grid.begin();
                const auto last = std::ranges::upper_bound(grid, i.hi) - grid.begin();
                diff[i.slot][static_cast<std::size_t>(first)] += static_cast<std::int64_t>(i.weight);
                diff[i.slot][static_cast<std::size_t>(last)] -= static_cast<std::int64_t>(i.weight);
            }
            std::vector<std::vector<ClusterCount>> out(zones, std::vector<ClusterCount>(grid.size(), 0));
            for (std::size_t z = 0; z < zones; ++z) {
                std::int64_t sum = 0;
                for (std::size_t j = 0; j < grid.size(); ++j) {
                    sum += diff[z][j];
                    out[z][j] = static_cast<ClusterCount>(sum);
                }
            }
            return out;
        }

        // The tables of the slack choice: best[z][j] = clusters kept by zones 0..z with zone z at candidate j;
        // from[z][j] = zone z-1's candidate in that best.
        struct ChoiceTables {
            std::vector<std::vector<ClusterCount>> best; // Kept clusters.
            std::vector<std::vector<std::size_t>> from; // Back pointers.
        };

        // Fills row z of the tables from row z-1 (a running maximum, so candidates never go back).
        void fillRow(const std::vector<std::vector<ClusterCount>>& staying, const std::vector<ForwardZone>& forward,
                     std::size_t z, ChoiceTables& t) {
            ClusterCount prefixBest = 0;
            std::size_t prefixAt = 0;
            for (std::size_t j = 0; j < staying[z].size(); ++j) {
                if (z > 0 && t.best[z - 1][j] > prefixBest) {
                    prefixBest = t.best[z - 1][j];
                    prefixAt = j;
                }
                t.best[z][j] = !forward[z].front || j == 0 ? staying[z][j] + prefixBest : 0;
                t.from[z][j] = prefixAt;
            }
        }

        // The non-decreasing choice of candidates (one per zone) that keeps the most clusters in place.
        std::vector<std::size_t> bestChoice(const std::vector<std::vector<ClusterCount>>& staying,
                                            const std::vector<ForwardZone>& forward, std::size_t candidates) {
            const std::size_t zones = forward.size();
            ChoiceTables t{.best = std::vector<std::vector<ClusterCount>>(zones, std::vector<ClusterCount>(candidates)),
                           .from = std::vector<std::vector<std::size_t>>(zones, std::vector<std::size_t>(candidates))};
            for (std::size_t z = 0; z < zones; ++z) {
                fillRow(staying, forward, z, t);
            }
            std::vector<std::size_t> choice(zones, 0);
            const auto& last = t.best[zones - 1];
            choice[zones - 1] = static_cast<std::size_t>(std::ranges::max_element(last) - last.begin());
            for (std::size_t z = zones - 1; z > 0; --z) {
                choice[z - 1] = t.from[z][choice[z]];
            }
            return choice;
        }

        // What deciding one file's lower bound reads.
        struct BoundContext {
            const Snapshot& snapshot; // The volume.
            const strategy::ZoneScript& script; // Zones.
            const ZoneAssignment& assignment; // Zones and roles.
            const TargetLayout& layout; // The target.
            const GoodEnough& rules; // Strictness.
        };

        // Whether a file must move to reach the target.
        bool mustMoveFile(const BoundContext& c, FileIndex file) {
            const FileRole role = c.assignment.roles[file];
            if (role == FileRole::Fixed || role == FileRole::InPlace) {
                return false;
            }
            const ZoneIndex zone = c.assignment.zoneOf[file];
            const auto runs = movableRuns(c.snapshot, file);
            if (!fragmentsOk(runs, fragmentOkFor(c.script, zone, c.rules), c.snapshot.geometry.bytesPerCluster)) {
                return true;
            }
            if (role != FileRole::Placed) {
                return false;
            }
            const ZoneTarget* target = c.layout.target(zone);
            return target == nullptr ||
                !std::ranges::all_of(runs, [&](const ClusterRun& r) { return target->holds(r); });
        }

        // Sums the must-move clusters, per zone and in total.
        void sumBound(const ZoneAssignment& assignment, std::size_t zones, LowerBound& out) {
            for (std::size_t f = 0; f < out.mustMove.size(); ++f) {
                if (out.mustMove[f] == 0) {
                    continue;
                }
                const ZoneIndex zone = assignment.zoneOf[f];
                out.clusters += assignment.movable[f];
                out.perZone[zone < zones ? zone : zones] += assignment.movable[f];
            }
        }

    } // namespace

    bool fragmentsOk(std::span<const ClusterRun> runs, std::optional<std::uint64_t> fragmentOk,
                     std::uint32_t bytesPerCluster) {
        if (fragmentsOf(runs) <= 1) {
            return true;
        }
        if (!fragmentOk) {
            return false;
        }
        ClusterCount piece = 0;
        for (std::size_t i = 0; i < runs.size(); ++i) {
            piece += runs[i].length;
            const bool ends = i + 1 == runs.size() || runs[i + 1].start != runs[i].end();
            if (ends && piece * bytesPerCluster < *fragmentOk) {
                return false;
            }
            piece = ends ? 0 : piece;
        }
        return true;
    }

    std::optional<std::uint64_t> fragmentOkFor(const strategy::ZoneScript& script, ZoneIndex zone,
                                               const GoodEnough& rules) {
        if (rules.wholeFiles) {
            return std::nullopt;
        }
        return zone < script.zones.size() ? script.zones[zone].fragmentOk
                                          : std::optional<std::uint64_t>(strategy::Zone::defaultFragmentOk);
    }

    std::optional<std::pair<std::int64_t, std::int64_t>> usableSpan(const Snapshot& snapshot, FileIndex file,
                                                                    const AvailableAxis& axis) {
        std::int64_t lo = std::numeric_limits<std::int64_t>::max();
        std::int64_t hi = 0;
        const auto records = snapshot.extents.records();
        for (const std::uint32_t position : snapshot.extents.extentsOf(file)) {
            const ExtentRecord& r = records[position];
            if ((r.flags() & ExtentUnmovable) != 0) {
                continue;
            }
            const ClusterCount first = axis.indexOf(r.lcn());
            if (axis.indexOf(r.end()) - first != r.length() || axis.lcnAt(first) != r.lcn()) {
                return std::nullopt;
            }
            lo = std::min(lo, static_cast<std::int64_t>(first));
            hi = std::max(hi, static_cast<std::int64_t>(first + r.length()));
        }
        return std::pair(lo, hi);
    }

    LowerBound lowerBound(const Snapshot& snapshot, const strategy::ZoneScript& script,
                          const ZoneAssignment& assignment, const TargetLayout& layout, const GoodEnough& rules,
                          Executor& executor, const CancelToken& token) {
        const std::size_t files = snapshot.files.size();
        LowerBound out{.mustMove = std::vector<std::uint8_t>(files, 0),
                       .clusters = 0,
                       .perZone = std::vector<ClusterCount>(script.zones.size() + 1, 0)};
        const BoundContext context{snapshot, script, assignment, layout, rules};
        executor.parallelFor(
            {.count = files, .grain = fileGrain},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t f = begin; f < end; ++f) {
                    out.mustMove[f] = mustMoveFile(context, static_cast<FileIndex>(f)) ? 1 : 0;
                }
            },
            token);
        sumBound(assignment, script.zones.size(), out);
        return out;
    }

    LayoutInputs fitTarget(const Snapshot& snapshot, const strategy::ZoneScript& script,
                           const ZoneAssignment& assignment, std::span<const classify::ClassId> classes,
                           const classify::Catalog& catalog, LayoutInputs base, const GoodEnough& rules,
                           Executor& executor, std::size_t maxCandidates) {
        base.slackBefore.clear();
        const TargetLayout layout = resolveLayout(snapshot, script, assignment, classes, catalog, base);
        std::vector<std::size_t> slotOfZone;
        const std::vector<ForwardZone> forward = forwardZones(layout, script, slotOfZone);
        if (layout.slack == 0 || forward.empty()) {
            return base;
        }
        const FitContext context{snapshot, script, assignment, layout, forward, slotOfZone, rules};
        const auto intervals = collectIntervals(context, executor);
        const auto grid = candidateGrid(intervals, maxCandidates);
        const auto staying = stayingPerCandidate(intervals, grid, forward.size());
        const auto choice = bestChoice(staying, forward, grid.size());
        for (const std::size_t j : choice) {
            base.slackBefore.push_back(static_cast<ClusterCount>(grid[j]));
        }
        return base;
    }

} // namespace icd::core::planner
