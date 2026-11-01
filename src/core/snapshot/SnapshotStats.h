#pragma once

#include "core/exec/Executor.h"
#include "core/snapshot/Snapshot.h"

#include <cstdint>

namespace icd::core {

    // Volume-wide figures of a snapshot for the Summary tab, the drive list and `icd-cli analyze`: how fragmented
    // the files are and how scattered the free space is.
    struct VolumeStats {
        std::uint64_t files = 0; // Files (not directories, not NTFS metadata).
        std::uint64_t directories = 0; // Directories.
        std::uint64_t filesWithClusters = 0; // Files that own clusters (not resident or empty).
        std::uint64_t fragmentedFiles = 0; // Files in more than one fragment.
        std::uint64_t extraFragments = 0; // Fragments beyond the first, summed over files.
        ClusterCount fragmentedClusters = 0; // Clusters of fragmented files.
        ClusterCount fileClusters = 0; // Clusters of all files.
        ClusterCount unmovableClusters = 0; // Clusters of unmovable extents (metadata, pinned, unattributed).
        ClusterCount freeClusters = 0; // Free space.
        std::uint64_t freeRuns = 0; // Separate free runs.
        ClusterCount largestFreeRun = 0; // Longest free run.
        ClusterCount freeInLargeRuns = 0; // Free clusters in runs of 64 MB or more (room for big files).

        // Share of files with clusters that are fragmented, in percent (0 without such files).
        [[nodiscard]] double fragmentedFilePercent() const;

        // Share of file clusters that belong to fragmented files, in percent.
        [[nodiscard]] double fragmentedClusterPercent() const;

        // Share of free space in runs of 64 MB or more, in percent.
        [[nodiscard]] double largeRunFreePercent() const;
    };

    // The figures of a snapshot; files are counted in parallel chunks combined in order, so the result does not
    // depend on the executor.
    [[nodiscard]] VolumeStats summarize(const Snapshot& snapshot, Executor& executor);

} // namespace icd::core
