#include "core/map/ZoneFitColorMode.h"

#include <algorithm>

namespace icd::core {

    namespace {

        // The movable runs of placed files that lie outside their zone's target.
        std::vector<ClusterRun> misplacedRuns(const Snapshot& snapshot, const planner::ZoneAssignment& assignment,
                                              const planner::TargetLayout& layout) {
            std::vector<ClusterRun> misplaced;
            for (std::size_t f = 0; f < assignment.roles.size(); ++f) {
                const planner::ZoneTarget* t =
                    assignment.roles[f] == planner::FileRole::Placed ? layout.target(assignment.zoneOf[f]) : nullptr;
                if (t == nullptr) {
                    continue;
                }
                for (const ClusterRun& r : planner::movableRuns(snapshot, static_cast<FileIndex>(f))) {
                    if (!t->holds(r)) {
                        misplaced.push_back(r);
                    }
                }
            }
            return misplaced;
        }

    } // namespace

    std::shared_ptr<const MapOverlay> zoneFitOverlay(const Snapshot& snapshot,
                                                     const planner::ZoneAssignment& assignment,
                                                     const planner::TargetLayout& layout) {
        std::vector<ClusterRun> targets;
        for (const planner::ZoneTarget& t : layout.zones) {
            targets.insert(targets.end(), t.pieces.begin(), t.pieces.end());
        }
        std::vector<ClusterRun> misplaced = misplacedRuns(snapshot, assignment, layout);
        auto overlay = std::make_shared<MapOverlay>();
        overlay->layers.push_back({.runs = mergedRuns(std::move(targets)), .color = zoneTargetColor});
        overlay->layers.push_back({.runs = mergedRuns(std::move(misplaced)), .color = misplacedColor});
        return overlay;
    }

} // namespace icd::core
