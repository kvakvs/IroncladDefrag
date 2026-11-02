#include "core/execute/ExecutionPlan.h"

namespace icd::core::execute {

    namespace {

        // The span a zone's target covers, or the whole volume for moves without one (or zones without pieces).
        ClusterRun regionOf(const planner::TargetLayout& layout, planner::ZoneIndex zone, VolumeGeometry geometry) {
            const planner::ZoneTarget* target = zone == planner::noZone ? nullptr : layout.target(zone);
            if (target == nullptr || target->pieces.empty()) {
                return {0, geometry.totalClusters};
            }
            const Lcn start = target->pieces.front().start;
            return {start, target->pieces.back().end() - start};
        }

    } // namespace

    std::span<const ClusterRun> ExecutionPlan::sources(const PlannedMove& move) const {
        return std::span(runs).subspan(move.firstSource, move.sourceCount);
    }

    std::span<const ClusterRun> ExecutionPlan::destinations(const PlannedMove& move) const {
        return std::span(runs).subspan(move.firstDestination, move.destinationCount);
    }

    void ExecutionPlan::add(PlannedMove move, std::span<const ClusterRun> from, std::span<const ClusterRun> to,
                            std::string path) {
        move.firstSource = static_cast<std::uint32_t>(runs.size());
        move.sourceCount = static_cast<std::uint32_t>(from.size());
        runs.insert(runs.end(), from.begin(), from.end());
        move.firstDestination = static_cast<std::uint32_t>(runs.size());
        move.destinationCount = static_cast<std::uint32_t>(to.size());
        runs.insert(runs.end(), to.begin(), to.end());
        move.clusters = planner::clustersOf(from);
        moves.push_back(move);
        paths.push_back(std::move(path));
    }

    ExecutionPlan executionPlanOf(const planner::PlanResult& result, const Snapshot& snapshot, PlanIdentity identity,
                                  std::vector<ClusterRun> reserved) {
        ExecutionPlan out;
        out.identity = std::move(identity);
        out.reserved = std::move(reserved);
        const planner::Plan& plan = result.plan;
        out.moves.reserve(plan.moves.size());
        for (const planner::Move& m : plan.moves) {
            PlannedMove move;
            move.fileReference = snapshot.files.fileReference(m.file);
            move.reason = m.reason;
            move.fromParking = m.fromParking;
            move.extra = m.extra;
            move.region = regionOf(result.layout, m.zone, snapshot.geometry);
            move.fileFlags = snapshot.files.flags(m.file);
            out.add(move, plan.sources(m), plan.destinations(m), snapshot.path(m.file));
        }
        return out;
    }

    ClusterCount plannedClusters(const ExecutionPlan& plan) {
        ClusterCount sum = 0;
        for (const PlannedMove& m : plan.moves) {
            sum += m.clusters;
        }
        return sum;
    }

} // namespace icd::core::execute
