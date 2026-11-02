#include "core/planner/DefragOnly.h"

#include "core/planner/Placement.h"

#include <algorithm>

namespace icd::core::planner {

    namespace {

        // Where one file goes: one run near it, else large pieces when allowed.
        std::optional<std::vector<ClusterRun>> destinationFor(const Snapshot& snapshot, const HoleSet& pool,
                                                              const SpaceRequest& request, Lcn near,
                                                              const DefragOptions& options) {
            if (const auto run = findNear(pool, request, near)) {
                return std::vector<ClusterRun>{*run};
            }
            if (!options.fragmentOk) {
                return std::nullopt;
            }
            const std::uint64_t cluster = std::max<std::uint32_t>(snapshot.geometry.bytesPerCluster, 1);
            return findPieces(pool, request, (*options.fragmentOk + cluster - 1) / cluster);
        }

        // Plans one file, or records it as skipped.
        void placeOne(const Snapshot& snapshot, FileIndex file, ClusterRun window, const DefragOptions& options,
                      HoleSet& pool, Plan& plan) {
            const std::vector<ClusterRun> runs = movableRuns(snapshot, file);
            if (runs.empty()) {
                return;
            }
            const ClusterCount clusters = clustersOf(runs);
            const std::vector<ClusterRun> own = sortedRuns(runs);
            const auto destination = destinationFor(
                snapshot, pool, {.clusters = clusters, .own = own, .window = window}, runs.front().start, options);
            if (!destination) {
                plan.skipped.push_back({.file = file, .reason = SkipReason::NoSpace, .clusters = clusters});
                plan.skippedClusters += clusters;
                return;
            }
            takeRuns(pool, *destination);
            plan.add({.file = file, .reason = MoveReason::Defragment, .zone = noZone}, runs, *destination);
            if (options.reuseVacated) {
                releaseRuns(pool, runs);
            }
        }

    } // namespace

    FillResult defragmentDestinations(const Snapshot& snapshot, std::span<const FileIndex> files, HoleSet& pool,
                                      const DefragOptions& options) {
        const ClusterRun window =
            options.window.length == 0 ? ClusterRun{0, snapshot.geometry.totalClusters} : options.window;
        std::vector<FileIndex> order(files.begin(), files.end());
        std::ranges::sort(order, [&](FileIndex a, FileIndex b) {
            const ClusterCount ca = snapshot.files.allocatedClusters(a);
            const ClusterCount cb = snapshot.files.allocatedClusters(b);
            return ca != cb ? ca > cb : a < b;
        });
        FillResult out;
        for (const FileIndex file : order) {
            const std::vector<ClusterRun> runs = movableRuns(snapshot, file);
            const std::vector<ClusterRun> own = sortedRuns(runs);
            const SpaceRequest request{.clusters = clustersOf(runs), .own = own, .window = window};
            auto destination =
                runs.empty() ? std::nullopt : destinationFor(snapshot, pool, request, runs.front().start, options);
            if (!destination) {
                out.overflow.push_back(file);
                continue;
            }
            takeRuns(pool, *destination);
            out.placed.push_back(
                {.file = file, .runs = std::move(*destination), .reason = MoveReason::Defragment, .zone = noZone});
        }
        return out;
    }

    void planDefragment(const Snapshot& snapshot, std::span<const FileIndex> files, HoleSet& pool,
                        const DefragOptions& options, Plan& plan) {
        const ClusterRun window =
            options.window.length == 0 ? ClusterRun{0, snapshot.geometry.totalClusters} : options.window;
        std::vector<std::pair<ClusterCount, FileIndex>> order;
        order.reserve(files.size());
        for (const FileIndex file : files) {
            order.emplace_back(snapshot.files.allocatedClusters(file), file);
        }
        std::ranges::sort(order, [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        for (const auto& [size, file] : order) {
            placeOne(snapshot, file, window, options, pool, plan);
        }
    }

} // namespace icd::core::planner
