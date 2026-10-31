#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace icd::core {

    // Logical cluster number: a cluster's position on the volume. 64-bit in every interface (plan.md 5).
    using Lcn = std::uint64_t;

    // A number of clusters.
    using ClusterCount = std::uint64_t;

    // Index of a file in a snapshot's FileTable. 32 bits keep per-file and per-extent data compact; 20 M
    // files at the design target leave ample room (plan.md 6.2 principle 4).
    using FileIndex = std::uint32_t;

    // "No file": the parent of a root, or an extent that belongs to nobody.
    inline constexpr FileIndex noFile = std::numeric_limits<FileIndex>::max();

    // A contiguous range of clusters [start, start + length).
    struct ClusterRun {
        Lcn start = 0; // First cluster.
        ClusterCount length = 0; // Number of clusters.

        // One past the last cluster.
        [[nodiscard]] constexpr Lcn end() const { return start + length; }

        // Whether lcn lies inside the run.
        [[nodiscard]] constexpr bool contains(Lcn lcn) const { return lcn >= start && lcn - start < length; }

        // Runs compare as values (tests, deduplication).
        friend constexpr bool operator==(const ClusterRun&, const ClusterRun&) = default;
    };

    // Size facts of the volume a snapshot describes.
    struct VolumeGeometry {
        std::uint32_t bytesPerCluster = 4096; // Cluster size.
        ClusterCount totalClusters = 0; // Clusters on the volume; valid LCNs are [0, totalClusters).
    };

    // Thrown when snapshot input is inconsistent (overlapping extents, runs outside the volume, bad parent
    // indexes). A scanner that produces such data has a bug or read a damaged volume, and nothing may be
    // planned on it.
    class SnapshotError : public std::runtime_error {
    public:
        // Keeps the message.
        explicit SnapshotError(const std::string& message) : std::runtime_error(message) {}
    };

} // namespace icd::core
