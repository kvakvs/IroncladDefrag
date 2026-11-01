#include "core/snapshot/SnapshotBuilder.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>

namespace icd::core {

    namespace {

        // Total order of pending extents: by LCN, then owner, so equal input in any order sorts the same.
        bool lcnOrder(const ExtentEntry& a, const ExtentEntry& b) {
            return std::tie(a.run.start, a.file, a.stream, a.vcn, a.run.length) <
                std::tie(b.run.start, b.file, b.stream, b.vcn, b.run.length);
        }

        // Throws unless an extent belongs to a file of snapshot and lies on its volume.
        void checkExtent(const ExtentEntry& entry, const Snapshot& snapshot) {
            if (entry.file >= snapshot.files.size()) {
                throw SnapshotError(
                    std::format("extent at LCN {} belongs to unknown file {}", entry.run.start, entry.file));
            }
            const ClusterCount total = snapshot.geometry.totalClusters;
            if (entry.run.end() > total || entry.run.end() < entry.run.start) {
                throw SnapshotError(std::format("extent at LCN {} with {} clusters is outside the volume ({} clusters)",
                                                entry.run.start, entry.run.length, total));
            }
        }

    } // namespace

    SnapshotBuilder::SnapshotBuilder(VolumeGeometry geometry,
                                     const std::shared_ptr<std::pmr::memory_resource>& storage) {
        snapshot_.geometry = geometry;
        if (storage) {
            snapshot_.names = NamePool(storage); // The allocators move with the containers and keep the storage.
            snapshot_.extents = ExtentIndex(storage);
        }
    }

    FileIndex SnapshotBuilder::addFile(const FileEntry& entry) {
        FileTable& files = snapshot_.files;
        if (files.size() >= noFile) {
            throw SnapshotError("too many files for 32-bit file indexes");
        }
        const auto index = static_cast<FileIndex>(files.size());
        files.names_.push_back(snapshot_.names.intern(entry.name));
        files.parents_.push_back(entry.parent);
        files.sizes_.push_back(entry.sizeBytes);
        files.clusters_.push_back(0); // Summed from the extents in build().
        files.flags_.push_back(entry.flags);
        files.times_.push_back(entry.times);
        files.references_.push_back(entry.fileReference);
        return index;
    }

    void SnapshotBuilder::addExtent(const ExtentEntry& entry) { extents_.push_back(entry); }

    void SnapshotBuilder::addExtents(std::vector<ExtentEntry> batch) {
        if (extents_.empty()) {
            extents_ = std::move(batch);
            return;
        }
        extents_.insert(extents_.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
    }

    void SnapshotBuilder::addFreeRun(ClusterRun run) { freeRuns_.push_back(run); }

    Snapshot SnapshotBuilder::build() && {
        Snapshot snapshot = std::move(snapshot_);
        buildExtents(snapshot);
        groupByFile(snapshot);
        snapshot.tree = DirectoryTree(snapshot.files);
        snapshot.freeSpace = FreeRunList(std::move(freeRuns_), snapshot.geometry.totalClusters);
        snapshot.names.freezeLookup();
        return snapshot;
    }

    void SnapshotBuilder::buildExtents(Snapshot& snapshot) {
        std::erase_if(extents_, [](const ExtentEntry& e) { return e.run.length == 0; });
        for (const ExtentEntry& entry : extents_) {
            checkExtent(entry, snapshot);
        }
        std::ranges::sort(extents_, lcnOrder);
        auto& records = snapshot.extents.records_;
        records.reserve(extents_.size());
        recordVcns_.reserve(extents_.size());
        Lcn previousEnd = 0;
        for (const ExtentEntry& entry : extents_) {
            if (entry.run.start < previousEnd) {
                throw SnapshotError(std::format("extents overlap at LCN {}", entry.run.start));
            }
            // Runs longer than a record holds become consecutive records; fragmentCount() rejoins them.
            for (ClusterCount done = 0; done < entry.run.length; done += maxExtentLength) {
                const ClusterCount piece = std::min(maxExtentLength, entry.run.length - done);
                records.emplace_back(ExtentRecord::Fields{.run = {entry.run.start + done, piece},
                                                          .file = entry.file,
                                                          .stream = entry.stream,
                                                          .flags = entry.flags});
                recordVcns_.push_back(entry.vcn + done);
            }
            previousEnd = entry.run.end();
        }
        if (records.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw SnapshotError("too many extents for 32-bit record positions");
        }
        extents_ = {};
    }

    void SnapshotBuilder::groupByFile(Snapshot& snapshot) {
        ExtentIndex& index = snapshot.extents;
        const std::size_t fileCount = snapshot.files.size();
        index.fileStart_.assign(fileCount + 1, 0);
        for (const ExtentRecord& record : index.records_) {
            ++index.fileStart_[record.file() + 1];
            snapshot.files.clusters_[record.file()] += record.length();
        }
        std::partial_sum(index.fileStart_.begin(), index.fileStart_.end(), index.fileStart_.begin());
        index.byFile_.resize(index.records_.size());
        std::vector<std::uint32_t> next(index.fileStart_.begin(), index.fileStart_.end() - 1);
        for (std::uint32_t position = 0; position < index.records_.size(); ++position) {
            index.byFile_[next[index.records_[position].file()]++] = position;
        }
        // Inside a file: by stream, then by position in the stream.
        const auto streamOrder = [&](std::uint32_t a, std::uint32_t b) {
            return std::pair(index.records_[a].stream(), recordVcns_[a]) <
                std::pair(index.records_[b].stream(), recordVcns_[b]);
        };
        for (std::size_t file = 0; file < fileCount; ++file) {
            const auto begin = index.byFile_.begin() + index.fileStart_[file];
            std::sort(begin, index.byFile_.begin() + index.fileStart_[file + 1], streamOrder);
        }
        recordVcns_ = {};
    }

} // namespace icd::core
