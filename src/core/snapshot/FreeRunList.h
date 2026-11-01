#pragma once

#include "core/snapshot/Types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace icd::core {

    // Saves and loads the private parts of snapshot classes (SnapshotIO.cpp).
    struct SnapshotAccess;

    // Free space of a volume as sorted, disjoint, merged runs. Built from the volume bitmap in chunks
    // (plan.md 6.2 principle 5), never as a full bitmap; the planner takes destinations from it.
    class FreeRunList {
    public:
        // An empty list (no free space).
        FreeRunList() = default;

        // Sorts and merges runs (adjacent and overlapping ones become one); empty runs are dropped. Throws
        // SnapshotError when a run reaches past totalClusters.
        FreeRunList(std::vector<ClusterRun> runs, ClusterCount totalClusters);

        // The runs in LCN order.
        [[nodiscard]] std::span<const ClusterRun> runs() const { return runs_; }

        // Number of runs.
        [[nodiscard]] std::size_t size() const { return runs_.size(); }

        // Free clusters in total.
        [[nodiscard]] ClusterCount freeClusters() const { return freeClusters_; }

        // The longest run (the first of equal ones), or nullopt without free space.
        [[nodiscard]] std::optional<ClusterRun> largest() const;

        // The run containing lcn, or nullopt when the cluster is in use.
        [[nodiscard]] std::optional<ClusterRun> find(Lcn lcn) const;

        // Whether lcn is free.
        [[nodiscard]] bool isFree(Lcn lcn) const { return find(lcn).has_value(); }

        // Runs overlapping [range.start, range.end()), in LCN order (not clipped to the range).
        [[nodiscard]] std::span<const ClusterRun> overlapping(ClusterRun range) const;

    private:
        friend struct SnapshotAccess; // Saves and loads the private parts (SnapshotIO).

        std::vector<ClusterRun> runs_; // Sorted, disjoint, merged.
        ClusterCount freeClusters_ = 0; // Sum of run lengths.
    };

} // namespace icd::core
