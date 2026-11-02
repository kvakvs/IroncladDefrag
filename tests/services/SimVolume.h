#pragma once

// A volume in memory for execution tests (T101-T103): every cluster has an owner, every file its clusters in stream and
// VCN order. It stands in for the file system: revalidation reads files from it, the executor moves clusters on it,
// and tests change it between planning and execution (files deleted, changed, new files on planned destinations).

#include "core/execute/MovePieces.h"
#include "core/execute/PlanReplay.h"
#include "core/snapshot/Snapshot.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace icd::testing {

    using core::ClusterCount;
    using core::ClusterRun;
    using core::Lcn;
    using core::execute::StreamRun;

    // See the file comment.
    class SimVolume final : public core::execute::CurrentFiles {
    public:
        // One cluster of a file.
        struct Cluster {
            std::uint16_t stream = 0; // Stream ordinal.
            std::uint64_t vcn = 0; // Position in the stream.
            Lcn lcn = 0; // Where it is.
        };

        // The volume a snapshot describes: movable extents become clusters of their file (VCNs counted per stream),
        // unmovable ones and the snapshot's used space stay owned by "the system" (reference 1).
        explicit SimVolume(const core::Snapshot& snapshot) : owner_(snapshot.geometry.totalClusters, systemOwner) {
            for (const ClusterRun& r : snapshot.freeSpace.runs()) {
                for (Lcn c = r.start; c < r.end(); ++c) {
                    owner_[c] = 0;
                }
            }
            for (core::FileIndex f = 0; f < snapshot.files.size(); ++f) {
                addFile(snapshot, f);
            }
        }

        // See CurrentFiles.
        [[nodiscard]] std::optional<std::vector<StreamRun>> runsOf(std::uint64_t reference) const override {
            const auto found = files_.find(reference);
            if (found == files_.end()) {
                return std::nullopt;
            }
            std::vector<StreamRun> out;
            for (const Cluster& c : found->second) {
                if (!out.empty() && out.back().stream == c.stream && out.back().vcn + out.back().length == c.vcn &&
                    out.back().lcn + out.back().length == c.lcn) {
                    ++out.back().length;
                    continue;
                }
                out.push_back({c.stream, c.vcn, c.lcn, 1});
            }
            return out;
        }

        // Free runs now.
        [[nodiscard]] std::vector<ClusterRun> freeRuns() const {
            std::vector<ClusterRun> out;
            for (Lcn c = 0; c < owner_.size(); ++c) {
                if (owner_[c] != 0) {
                    continue;
                }
                if (!out.empty() && out.back().end() == c) {
                    ++out.back().length;
                }
                else {
                    out.push_back({c, 1});
                }
            }
            return out;
        }

        // Whether every cluster of a run is free.
        [[nodiscard]] bool isFree(ClusterRun run) const {
            for (Lcn c = run.start; c < run.end(); ++c) {
                if (c >= owner_.size() || owner_[c] != 0) {
                    return false;
                }
            }
            return true;
        }

        // Moves `count` clusters of a stream from `vcn` to `to` like FSCTL_MOVE_FILE; false (nothing changed) when the
        // file or range does not exist or a target cluster is not free.
        bool move(std::uint64_t reference, std::uint16_t stream, std::uint64_t vcn, Lcn to, ClusterCount count) {
            const auto found = files_.find(reference);
            if (found == files_.end() || !isFree({to, count})) {
                return false;
            }
            // Clusters are kept in (stream, VCN) order without gaps, so the range is one slice.
            auto& clusters = found->second;
            const auto first = std::ranges::lower_bound(clusters, std::pair(stream, vcn), {},
                                                        [](const Cluster& c) { return std::pair(c.stream, c.vcn); });
            const auto offset = static_cast<std::size_t>(first - clusters.begin());
            if (offset + count > clusters.size() || clusters[offset].stream != stream || clusters[offset].vcn != vcn ||
                clusters[offset + count - 1].stream != stream || clusters[offset + count - 1].vcn != vcn + count - 1) {
                return false;
            }
            for (std::size_t k = offset; k < offset + count; ++k) {
                owner_[clusters[k].lcn] = 0;
            }
            for (std::size_t k = offset; k < offset + count; ++k) {
                clusters[k].lcn = to + (k - offset);
                owner_[clusters[k].lcn] = reference;
            }
            return true;
        }

        // Deletes a file: its clusters become free.
        void remove(std::uint64_t reference) {
            for (const Cluster& c : files_.at(reference)) {
                owner_[c.lcn] = 0;
            }
            files_.erase(reference);
        }

        // Another program takes free clusters (a new file); they must be free.
        void occupy(ClusterRun run) {
            for (Lcn c = run.start; c < run.end(); ++c) {
                owner_[c] = systemOwner;
            }
        }

        // Another program frees clusters it held (a deleted file outside the plan).
        void release(ClusterRun run) {
            for (Lcn c = run.start; c < run.end(); ++c) {
                owner_[c] = 0;
            }
        }

        // A file's clusters.
        [[nodiscard]] const std::vector<Cluster>& clustersOf(std::uint64_t reference) const {
            return files_.at(reference);
        }

        // Whether the file exists.
        [[nodiscard]] bool has(std::uint64_t reference) const { return files_.contains(reference); }

        // The owner of a cluster: 0 free, 1 the system, else a file reference.
        [[nodiscard]] std::uint64_t ownerOf(Lcn lcn) const { return owner_.at(lcn); }

        // The owner of clusters nobody may move.
        static constexpr std::uint64_t systemOwner = 1;

    private:
        // Adds a snapshot file's movable clusters.
        void addFile(const core::Snapshot& snapshot, core::FileIndex f) {
            const std::uint64_t reference = snapshot.files.fileReference(f);
            const auto records = snapshot.extents.records();
            std::vector<Cluster> clusters;
            std::map<std::uint16_t, std::uint64_t> vcns;
            for (const std::uint32_t position : snapshot.extents.extentsOf(f)) {
                const core::ExtentRecord& r = records[position];
                if ((r.flags() & core::ExtentUnmovable) != 0) {
                    continue;
                }
                for (Lcn c = r.lcn(); c < r.end(); ++c) {
                    clusters.push_back({r.stream(), vcns[r.stream()]++, c});
                    owner_[c] = std::max(reference, systemOwner); // References 0 and 1 cannot be told apart.
                }
            }
            if (reference > systemOwner) {
                files_[reference] = std::move(clusters);
            }
        }

        std::vector<std::uint64_t> owner_; // Per cluster.
        std::map<std::uint64_t, std::vector<Cluster>> files_; // Per file reference: clusters in stream and VCN order.
    };

} // namespace icd::testing
