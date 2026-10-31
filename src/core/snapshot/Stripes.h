#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace icd::core {

    // How many stripes to aim for, as its own type so it cannot be mixed up with a cluster count.
    struct StripeTarget {
        std::size_t stripes = 512; // Wanted stripe count (plan.md suggests 256-1024); small volumes get fewer.
    };

    // How a volume is cut into fixed LCN stripes (plan.md 6.2 principle 2): equal-sized ranges (the last one
    // may be shorter) that cover every cluster exactly once. Most per-volume work runs per stripe in parallel;
    // the split depends only on the volume size, never on the thread count, so results stay deterministic.
    class StripeLayout {
    public:
        // Stripes wanted on a large volume.
        static constexpr std::size_t defaultStripes = StripeTarget{}.stripes;

        // Stripe sizes are multiples of this many clusters (16 MB at 4 KB), so small volumes get fewer stripes
        // instead of tiny ones.
        static constexpr ClusterCount alignment = 4096;

        // A layout for a volume; at least one stripe, even for an empty volume.
        explicit StripeLayout(ClusterCount totalClusters, StripeTarget target = {});

        // Number of stripes.
        [[nodiscard]] std::size_t count() const { return count_; }

        // Clusters per stripe (all but possibly the last).
        [[nodiscard]] ClusterCount stripeClusters() const { return stripeClusters_; }

        // Clusters on the volume.
        [[nodiscard]] ClusterCount totalClusters() const { return totalClusters_; }

        // The clusters of a stripe.
        [[nodiscard]] ClusterRun range(std::size_t stripe) const;

        // The stripe holding lcn (the last one for LCNs past the end).
        [[nodiscard]] std::size_t stripeOf(Lcn lcn) const;

    private:
        ClusterCount totalClusters_ = 0; // Volume size.
        ClusterCount stripeClusters_ = 1; // Stripe size.
        std::size_t count_ = 1; // Stripe count.
    };

    // What one stripe sees of a snapshot. Records and free runs that cross a stripe edge appear in both
    // neighbours' views; clip() them to the range when counting clusters, so every cluster counts once.
    struct StripeView {
        std::size_t index = 0; // Stripe number, in LCN order.
        ClusterRun range; // The stripe's clusters.
        std::span<const ExtentRecord> extents; // Records overlapping range, in LCN order.
        std::span<const ClusterRun> freeRuns; // Free runs overlapping range, in LCN order.
    };

    // The part of run inside range (length 0 when they do not overlap).
    [[nodiscard]] ClusterRun clip(ClusterRun run, ClusterRun range);

    // Stripe views over a snapshot, with the record and free-run boundaries of every stripe found once up
    // front, so a view costs nothing to make. The snapshot must outlive this object.
    class Stripes {
    public:
        // Cuts snapshot's volume by layout.
        Stripes(const Snapshot& snapshot, StripeLayout layout);

        // The layout.
        [[nodiscard]] const StripeLayout& layout() const { return layout_; }

        // Number of stripes.
        [[nodiscard]] std::size_t count() const { return layout_.count(); }

        // The view of one stripe.
        [[nodiscard]] StripeView view(std::size_t stripe) const;

    private:
        const Snapshot* snapshot_; // Source; outlives this object.
        StripeLayout layout_; // How the volume is cut.
        std::vector<std::pair<std::size_t, std::size_t>> extentBounds_; // Per stripe: [first, last) record.
        std::vector<std::pair<std::size_t, std::size_t>> freeBounds_; // Per stripe: [first, last) free run.
    };

    // Runs map on every stripe in parallel and returns the results in stripe (LCN) order, the deterministic
    // merge of plan.md 6.2 principle 8: the output is the same for any executor.
    template <typename Map>
    auto mapStripes(Executor& executor, const Stripes& stripes, Map map, const CancelToken& token = {})
        -> std::vector<decltype(map(std::declval<const StripeView&>()))> {
        std::vector<decltype(map(std::declval<const StripeView&>()))> results(stripes.count());
        executor.parallelFor(
            {.count = stripes.count(), .grain = 1},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t stripe = begin; stripe < end; ++stripe) {
                    results[stripe] = map(stripes.view(stripe));
                }
            },
            token);
        return results;
    }

    // Maps every stripe in parallel and combines the results in stripe order, so even floating-point totals
    // are identical for any thread count.
    template <typename T, typename Map, typename Combine>
    T reduceStripes(Executor& executor, const Stripes& stripes, T init, Map map, Combine combine,
                    const CancelToken& token = {}) {
        return parallelReduce(
            executor, {.count = stripes.count(), .grain = 1}, std::move(init),
            [&](std::size_t begin, std::size_t /*end*/) { return map(stripes.view(begin)); }, combine, token);
    }

} // namespace icd::core
