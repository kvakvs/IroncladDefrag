#include "core/planner/Plan.h"

#include <array>
#include <unordered_map>

namespace icd::core::planner {

    std::string_view reasonKey(MoveReason reason) {
        constexpr std::array<std::string_view, 6> keys{"defragment", "relocate", "compact", "sort", "park", "evict"};
        return keys.at(static_cast<std::size_t>(reason));
    }

    std::string_view skipKey(SkipReason reason) {
        constexpr std::array<std::string_view, 4> keys{"no-space", "cycle", "dependency", "unaligned"};
        return keys.at(static_cast<std::size_t>(reason));
    }

    std::span<const ClusterRun> Plan::sources(const Move& move) const {
        return std::span(runs).subspan(move.firstSource, move.sourceCount);
    }

    std::span<const ClusterRun> Plan::destinations(const Move& move) const {
        return std::span(runs).subspan(move.firstDestination, move.destinationCount);
    }

    void Plan::add(Move move, std::span<const ClusterRun> from, std::span<const ClusterRun> to) {
        move.firstSource = static_cast<std::uint32_t>(runs.size());
        move.sourceCount = static_cast<std::uint32_t>(from.size());
        runs.insert(runs.end(), from.begin(), from.end());
        move.firstDestination = static_cast<std::uint32_t>(runs.size());
        move.destinationCount = static_cast<std::uint32_t>(to.size());
        runs.insert(runs.end(), to.begin(), to.end());
        move.clusters = 0;
        for (const ClusterRun& r : from) {
            move.clusters += r.length;
        }
        moves.push_back(move);
    }

    std::vector<ClusterRun> movableRuns(const Snapshot& snapshot, FileIndex file) {
        std::vector<ClusterRun> out;
        const auto records = snapshot.extents.records();
        for (const std::uint32_t position : snapshot.extents.extentsOf(file)) {
            const ExtentRecord& r = records[position];
            if ((r.flags() & ExtentUnmovable) == 0) {
                out.push_back(r.run());
            }
        }
        return out;
    }

    ClusterCount clustersOf(std::span<const ClusterRun> runs) {
        ClusterCount sum = 0;
        for (const ClusterRun& r : runs) {
            sum += r.length;
        }
        return sum;
    }

    std::uint64_t fragmentsOf(std::span<const ClusterRun> runs) {
        std::uint64_t fragments = 0;
        for (std::size_t i = 0; i < runs.size(); ++i) {
            fragments += i == 0 || runs[i].start != runs[i - 1].end() ? 1 : 0;
        }
        return fragments;
    }

    void finishTotals(Plan& plan, const Snapshot& snapshot) {
        PlanTotals t;
        std::unordered_map<FileIndex, std::size_t> last; // Per file: its last move.
        for (std::size_t i = 0; i < plan.moves.size(); ++i) {
            const Move& m = plan.moves[i];
            t.clustersMoved += m.clusters;
            last[m.file] = i;
        }
        t.moves = plan.moves.size();
        t.files = last.size();
        for (const auto& [file, index] : last) {
            t.fragmentsBefore += fragmentsOf(movableRuns(snapshot, file));
            t.fragmentsAfter += fragmentsOf(plan.destinations(plan.moves[index]));
        }
        plan.totals = t;
    }

} // namespace icd::core::planner
