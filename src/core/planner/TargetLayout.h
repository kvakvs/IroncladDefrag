#pragma once

#include "core/classify/Catalog.h"
#include "core/planner/ZoneAssignment.h"
#include "core/snapshot/Snapshot.h"
#include "core/strategy/ZoneScript.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace icd::core::planner {

    // The clusters zones can use, numbered 0, 1, ... in LCN order: the volume minus fixed obstacles ($MFT, the MFT
    // zone, unmovable and in-place files). Zones are laid out in these numbers, so they flow around obstacles
    // (plan.md 8.1) and a zone of N clusters always gets exactly N usable clusters.
    class AvailableAxis {
    public:
        // An axis over a whole volume without obstacles.
        AvailableAxis() = default;

        // An axis over a volume of totalClusters; obstacles may be unsorted, overlap and touch.
        AvailableAxis(std::vector<ClusterRun> obstacles, ClusterCount totalClusters);

        // Usable clusters.
        [[nodiscard]] ClusterCount size() const { return total_ - obstacleClusters_; }

        // Usable clusters before lcn (for an LCN inside an obstacle: before the obstacle).
        [[nodiscard]] ClusterCount indexOf(Lcn lcn) const;

        // LCN of the usable cluster with this number; index must be below size().
        [[nodiscard]] Lcn lcnAt(ClusterCount index) const;

        // The LCN runs of the usable clusters numbered [numbers.start, numbers.end()), in LCN order.
        [[nodiscard]] std::vector<ClusterRun> pieces(ClusterRun numbers) const;

        // The merged obstacles in LCN order.
        [[nodiscard]] const std::vector<ClusterRun>& obstacles() const { return obstacles_; }

        // Clusters on the volume.
        [[nodiscard]] ClusterCount totalClusters() const { return total_; }

    private:
        std::vector<ClusterRun> obstacles_; // Sorted, merged.
        std::vector<ClusterCount> before_; // Per obstacle: obstacle clusters before it.
        ClusterCount obstacleClusters_ = 0; // Sum of obstacle lengths.
        ClusterCount total_ = 0; // Volume size.
    };

    // Where one placed zone goes.
    struct ZoneTarget {
        ZoneIndex zone = noZone; // Index in the script.
        bool back = false; // Anchored at the back of the volume.
        ClusterCount need = 0; // Movable clusters of the zone's files.
        ClusterCount start = 0; // First usable cluster number.
        ClusterCount length = 0; // Usable clusters given: need, unless the volume is short.
        std::vector<ClusterRun> pieces{}; // The zone's LCN runs (usable clusters only).
        ClusterCount gapWanted = 0; // Gap the script asks for after the zone.
        ClusterCount gap = 0; // Gap granted (smaller when free space is short).
        std::vector<ClusterRun> gapPieces{}; // The gap's LCN runs.

        // First LCN of the zone, or nullopt for an empty zone.
        [[nodiscard]] std::optional<Lcn> firstLcn() const;

        // One past the last LCN of the zone (the first LCN for an empty zone).
        [[nodiscard]] Lcn endLcn() const;

        // Whether a run lies entirely inside the zone's pieces.
        [[nodiscard]] bool holds(ClusterRun run) const;
    };

    // Choices a layout leaves open, decided by the caller (the target fitting of plan.md 9.3.2).
    struct LayoutInputs {
        std::vector<ClusterRun> reserved{}; // Kept free when possible: the MFT zone (plan.md 3, fact 6).
        // Per forward placed zone, in script order: free clusters before it, counted from the front (non-decreasing,
        // capped to the slack). Empty: every zone packed to the front, all slack after the last forward zone.
        std::vector<ClusterCount> slackBefore{};
        // Per script zone: clusters beyond its need it may span, taken from the slack once gaps are granted (sorted
        // zones keep their anchors and avoid dependency cycles with it). Empty: every zone spans exactly its need.
        std::vector<ClusterCount> room{};
    };

    // The target state's geometry (plan.md 9.1): each placed zone resolved to LCN runs around the obstacles, with
    // its gap, and the free space left over (slack). Forward zones fill from the front in script order, back zones
    // end at the end of the volume.
    struct TargetLayout {
        AvailableAxis axis; // Usable clusters.
        std::vector<ZoneTarget> zones{}; // Placed zones in script order.
        std::vector<std::size_t> slotOf{}; // Per script zone: index into zones, or npos for unplaced zones.
        ClusterCount slack = 0; // Free usable clusters outside every zone and gap.
        std::vector<ClusterRun> slackPieces{}; // Their LCN runs.
        std::vector<std::string> notes{}; // Goals the volume cannot reach (gaps shrunk, MFT zone used).
        bool reservedUsed = false; // The reserved runs had to be given up: files may land in them.

        // The target of a script zone, or null for zones without a place.
        [[nodiscard]] const ZoneTarget* target(ZoneIndex zone) const;
    };

    // The extents no zone may use: unmovable extents, files that are not Placed, and the reserved runs. Sorted,
    // merged.
    [[nodiscard]] std::vector<ClusterRun> layoutObstacles(const Snapshot& snapshot, const ZoneAssignment& assignment,
                                                          std::span<const ClusterRun> reserved);

    // Clusters of a gap spec on a volume: the largest of its terms. `freeClusters` is the usable free space after
    // the plan; `classClusters` gives the clusters a catalog class holds.
    [[nodiscard]] ClusterCount gapClusters(const strategy::GapSpec& gap, const VolumeGeometry& geometry,
                                           ClusterCount freeClusters, const classify::Catalog& catalog,
                                           std::span<const ClusterCount> classClusters);

    // Resolves the placed zones of a script to LCN runs. Gaps shrink proportionally when free space is short; the
    // reserved runs are given up when the files do not fit without them. `classes` (per file) feeds gaps that are a
    // multiple of some classes' bytes.
    [[nodiscard]] TargetLayout resolveLayout(const Snapshot& snapshot, const strategy::ZoneScript& script,
                                             const ZoneAssignment& assignment,
                                             std::span<const classify::ClassId> classes,
                                             const classify::Catalog& catalog, const LayoutInputs& inputs);

} // namespace icd::core::planner
