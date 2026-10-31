#include "core/snapshot/FreeRunList.h"

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>

namespace icd::core {

    namespace {

        // Sorted runs with overlapping and touching ones joined.
        std::vector<ClusterRun> merged(std::vector<ClusterRun> runs) {
            std::erase_if(runs, [](const ClusterRun& run) { return run.length == 0; });
            std::ranges::sort(runs, {}, &ClusterRun::start);
            std::vector<ClusterRun> out;
            out.reserve(runs.size());
            for (const ClusterRun& run : runs) {
                if (!out.empty() && run.start <= out.back().end()) {
                    out.back().length = std::max(out.back().end(), run.end()) - out.back().start;
                }
                else {
                    out.push_back(run);
                }
            }
            return out;
        }

    } // namespace

    FreeRunList::FreeRunList(std::vector<ClusterRun> runs, ClusterCount totalClusters) :
        runs_(merged(std::move(runs))) {
        for (const ClusterRun& run : runs_) {
            if (run.end() > totalClusters || run.end() < run.start) {
                throw SnapshotError(std::format("free run at LCN {} with {} clusters is outside the volume ({} "
                                                "clusters)",
                                                run.start, run.length, totalClusters));
            }
            freeClusters_ += run.length;
        }
    }

    std::optional<ClusterRun> FreeRunList::largest() const {
        const auto best = std::ranges::max_element(
            runs_, [](const ClusterRun& a, const ClusterRun& b) { return a.length < b.length; });
        return best == runs_.end() ? std::nullopt : std::optional(*best);
    }

    std::optional<ClusterRun> FreeRunList::find(Lcn lcn) const {
        const auto after = std::ranges::upper_bound(runs_, lcn, std::less<>{}, &ClusterRun::start);
        if (after == runs_.begin() || !std::prev(after)->contains(lcn)) {
            return std::nullopt;
        }
        return *std::prev(after);
    }

    std::span<const ClusterRun> FreeRunList::overlapping(ClusterRun range) const {
        if (range.length == 0) {
            return {};
        }
        const auto first = std::ranges::lower_bound(runs_, range.start, std::less<>{},
                                                    [](const ClusterRun& r) { return r.end() - 1; });
        const auto last = std::ranges::lower_bound(runs_, range.end(), std::less<>{}, &ClusterRun::start);
        return first >= last ? std::span<const ClusterRun>() : std::span<const ClusterRun>(first, last);
    }

} // namespace icd::core
