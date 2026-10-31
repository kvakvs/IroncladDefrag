#include "core/snapshot/Stripes.h"

#include <algorithm>
#include <functional>
#include <span>

namespace icd::core {

    StripeLayout::StripeLayout(ClusterCount totalClusters, StripeTarget target) : totalClusters_(totalClusters) {
        const ClusterCount wanted = std::max<std::size_t>(target.stripes, 1);
        const ClusterCount raw = (totalClusters + wanted - 1) / wanted;
        stripeClusters_ = std::max<ClusterCount>(alignment, (raw + alignment - 1) / alignment * alignment);
        count_ = static_cast<std::size_t>(
            std::max<ClusterCount>(1, (totalClusters + stripeClusters_ - 1) / stripeClusters_));
    }

    ClusterRun StripeLayout::range(std::size_t stripe) const {
        const Lcn start = std::min<Lcn>(stripe * stripeClusters_, totalClusters_);
        return {start, std::min(stripeClusters_, totalClusters_ - start)};
    }

    std::size_t StripeLayout::stripeOf(Lcn lcn) const {
        return std::min<std::size_t>(static_cast<std::size_t>(lcn / stripeClusters_), count_ - 1);
    }

    ClusterRun clip(ClusterRun run, ClusterRun range) {
        const Lcn start = std::max(run.start, range.start);
        const Lcn end = std::min(run.end(), range.end());
        return start < end ? ClusterRun{start, end - start} : ClusterRun{start, 0};
    }

    namespace {

        // For each stripe, [first, last) of the items of a sorted, disjoint sequence that overlap it.
        template <typename Item, typename Start, typename End>
        std::vector<std::pair<std::size_t, std::size_t>> bounds(std::span<const Item> items, const StripeLayout& layout,
                                                                Start start, End end) {
            std::vector<std::pair<std::size_t, std::size_t>> out(layout.count());
            for (std::size_t stripe = 0; stripe < layout.count(); ++stripe) {
                const ClusterRun range = layout.range(stripe);
                // Items are disjoint and sorted, so both their starts and their ends are sorted.
                const auto first =
                    std::ranges::lower_bound(items, range.start + 1, std::less<>{}, end); // end > range.start
                const auto last = std::ranges::lower_bound(items, range.end(), std::less<>{}, start);
                const auto firstIndex = static_cast<std::size_t>(first - items.begin());
                const auto lastIndex = static_cast<std::size_t>(last - items.begin());
                out[stripe] = {firstIndex, std::max(firstIndex, lastIndex)};
            }
            return out;
        }

    } // namespace

    Stripes::Stripes(const Snapshot& snapshot, StripeLayout layout) :
        snapshot_(&snapshot), layout_(layout),
        extentBounds_(bounds(
            snapshot.extents.records(), layout_, [](const ExtentRecord& r) { return r.lcn(); },
            [](const ExtentRecord& r) { return r.end(); })),
        freeBounds_(bounds(snapshot.freeSpace.runs(), layout_, &ClusterRun::start,
                           [](const ClusterRun& r) { return r.end(); })) {}

    StripeView Stripes::view(std::size_t stripe) const {
        const auto [extentFirst, extentLast] = extentBounds_.at(stripe);
        const auto [freeFirst, freeLast] = freeBounds_.at(stripe);
        return {.index = stripe,
                .range = layout_.range(stripe),
                .extents = snapshot_->extents.records().subspan(extentFirst, extentLast - extentFirst),
                .freeRuns = snapshot_->freeSpace.runs().subspan(freeFirst, freeLast - freeFirst)};
    }

} // namespace icd::core
