#include "core/snapshot/SnapshotStats.h"

#include <algorithm>

namespace icd::core {

    namespace {

        // Percent of part in whole; 0 for an empty whole.
        double percentOf(std::uint64_t part, std::uint64_t whole) {
            return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
        }

        // Adds one file's figures.
        void countFile(const Snapshot& snapshot, FileIndex file, VolumeStats& stats) {
            const std::uint16_t flags = snapshot.files.flags(file);
            if ((flags & FileDirectory) != 0) {
                ++stats.directories;
                return;
            }
            if ((flags & FileMetadata) != 0) {
                return;
            }
            ++stats.files;
            const ClusterCount clusters = snapshot.files.allocatedClusters(file);
            if (clusters == 0) {
                return;
            }
            ++stats.filesWithClusters;
            stats.fileClusters += clusters;
            const std::size_t fragments = snapshot.extents.fragmentCount(file);
            if (fragments > 1) {
                ++stats.fragmentedFiles;
                stats.extraFragments += fragments - 1;
                stats.fragmentedClusters += clusters;
            }
        }

        // Adds the per-file figures of b to a.
        VolumeStats addFiles(VolumeStats a, const VolumeStats& b) {
            a.files += b.files;
            a.directories += b.directories;
            a.filesWithClusters += b.filesWithClusters;
            a.fragmentedFiles += b.fragmentedFiles;
            a.extraFragments += b.extraFragments;
            a.fragmentedClusters += b.fragmentedClusters;
            a.fileClusters += b.fileClusters;
            return a;
        }

        // Free-space and unmovable figures (sequential: one pass over runs and records).
        void countSpace(const Snapshot& snapshot, VolumeStats& stats) {
            const ClusterCount largeRun = std::max<ClusterCount>(1, (64ULL << 20U) / snapshot.geometry.bytesPerCluster);
            for (const ClusterRun& run : snapshot.freeSpace.runs()) {
                stats.freeInLargeRuns += run.length >= largeRun ? run.length : 0;
                stats.largestFreeRun = std::max(stats.largestFreeRun, run.length);
            }
            stats.freeClusters = snapshot.freeSpace.freeClusters();
            stats.freeRuns = snapshot.freeSpace.size();
            for (const ExtentRecord& record : snapshot.extents.records()) {
                stats.unmovableClusters += (record.flags() & ExtentUnmovable) != 0 ? record.length() : 0;
            }
        }

    } // namespace

    double VolumeStats::fragmentedFilePercent() const { return percentOf(fragmentedFiles, filesWithClusters); }

    double VolumeStats::fragmentedClusterPercent() const { return percentOf(fragmentedClusters, fileClusters); }

    double VolumeStats::largeRunFreePercent() const { return percentOf(freeInLargeRuns, freeClusters); }

    VolumeStats summarize(const Snapshot& snapshot, Executor& executor) {
        const ChunkedRange range{snapshot.files.size(), 16384};
        VolumeStats stats = parallelReduce(
            executor, range, VolumeStats{},
            [&](std::size_t begin, std::size_t end) {
                VolumeStats part;
                for (std::size_t file = begin; file < end; ++file) {
                    countFile(snapshot, static_cast<FileIndex>(file), part);
                }
                return part;
            },
            addFiles);
        countSpace(snapshot, stats);
        return stats;
    }

} // namespace icd::core
