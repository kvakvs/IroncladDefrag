#include "core/snapshot/ScanReconcile.h"

#include "core/snapshot/ExtentIndex.h"
#include "core/snapshot/FreeRunList.h"

#include <algorithm>

namespace icd::core {

    namespace {

        // Drops empty runs and clips runs to the volume, so a bad read can never reach past its end.
        void clipToVolume(std::vector<ExtentEntry>& extents, ClusterCount total) {
            std::erase_if(extents, [&](const ExtentEntry& e) { return e.run.length == 0 || e.run.start >= total; });
            for (ExtentEntry& e : extents) {
                e.run.length = std::min(e.run.length, total - e.run.start);
            }
        }

        // LCN order; at equal starts the newer read (higher file index) first, so it is the one kept.
        void sortExtents(std::vector<ExtentEntry>& extents) {
            std::ranges::sort(extents, [](const ExtentEntry& a, const ExtentEntry& b) {
                return a.run.start != b.run.start ? a.run.start < b.run.start : a.file > b.file;
            });
        }

        // Keeps disjoint extents, preferring the newer read where two overlap; returns how many were dropped.
        std::uint64_t dropOverlaps(std::vector<ExtentEntry>& extents) {
            std::vector<ExtentEntry> kept;
            kept.reserve(extents.size());
            std::uint64_t dropped = 0;
            for (const ExtentEntry& e : extents) {
                // Only the last kept extent can overlap: the kept ones are sorted and disjoint.
                while (!kept.empty() && e.run.start < kept.back().run.end() && e.file > kept.back().file) {
                    kept.pop_back();
                    ++dropped;
                }
                if (!kept.empty() && e.run.start < kept.back().run.end()) {
                    ++dropped;
                    continue;
                }
                kept.push_back(e);
            }
            extents = std::move(kept);
            return dropped;
        }

        // Cuts the extents (sorted, disjoint) out of one free run; returns the clusters removed.
        ClusterCount cutRun(ClusterRun run, const std::vector<ExtentEntry>& extents, std::size_t first,
                            std::vector<ClusterRun>& out) {
            ClusterCount taken = 0;
            Lcn cursor = run.start;
            for (std::size_t k = first; k < extents.size() && extents[k].run.start < run.end(); ++k) {
                const ClusterRun& e = extents[k].run;
                if (e.start > cursor) {
                    out.push_back({cursor, e.start - cursor});
                }
                const Lcn from = std::max(cursor, e.start);
                const Lcn to = std::min(run.end(), e.end());
                taken += to > from ? to - from : 0;
                cursor = std::max(cursor, e.end());
            }
            if (cursor < run.end()) {
                out.push_back({cursor, run.end() - cursor});
            }
            return taken;
        }

        // Removes extent clusters from the free runs; returns the clusters removed.
        ClusterCount subtractExtents(std::vector<ClusterRun>& freeRuns, const std::vector<ExtentEntry>& extents,
                                     ClusterCount total) {
            const FreeRunList merged(std::move(freeRuns), total);
            std::vector<ClusterRun> out;
            out.reserve(merged.size());
            ClusterCount taken = 0;
            std::size_t first = 0;
            for (const ClusterRun& run : merged.runs()) {
                while (first < extents.size() && extents[first].run.end() <= run.start) {
                    ++first;
                }
                taken += cutRun(run, extents, first, out);
            }
            freeRuns = std::move(out);
            return taken;
        }

        // Adds unmovable extents of `file` for every cluster neither in an extent nor free.
        void fillGaps(std::vector<ExtentEntry>& extents, const std::vector<ClusterRun>& freeRuns, ClusterCount total,
                      ReconcileStats& stats, FileIndex file) {
            std::vector<ExtentEntry> gaps;
            Lcn pos = 0;
            const auto gapTo = [&](Lcn end) {
                if (end > pos) {
                    gaps.push_back({{pos, end - pos}, stats.unattributed, file, 0, ExtentUnmovable});
                    stats.unattributed += end - pos;
                    ++stats.unattributedRuns;
                }
            };
            std::size_t i = 0;
            std::size_t k = 0;
            while (i < extents.size() || k < freeRuns.size()) {
                const bool takeExtent =
                    k == freeRuns.size() || (i < extents.size() && extents[i].run.start < freeRuns[k].start);
                const ClusterRun next = takeExtent ? extents[i++].run : freeRuns[k++];
                gapTo(next.start);
                pos = std::max(pos, next.end());
            }
            gapTo(total);
            extents.insert(extents.end(), gaps.begin(), gaps.end());
            sortExtents(extents);
        }

    } // namespace

    ReconcileStats reconcileScan(std::vector<ExtentEntry>& extents, std::vector<ClusterRun>& freeRuns,
                                 ClusterCount totalClusters, FileIndex unattributedFile) {
        ReconcileStats stats;
        clipToVolume(extents, totalClusters);
        sortExtents(extents);
        stats.droppedExtents = dropOverlaps(extents);
        stats.freeTaken = subtractExtents(freeRuns, extents, totalClusters);
        fillGaps(extents, freeRuns, totalClusters, stats, unattributedFile);
        return stats;
    }

} // namespace icd::core
