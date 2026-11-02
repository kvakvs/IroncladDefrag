#include "core/map/MapData.h"

#include "core/snapshot/FreeRunList.h"

#include <utility>
#include <vector>

namespace icd::core {

    MapData::MapData(Snapshot snapshot, World world) : snapshot_(std::move(snapshot)), world_(world) {}

    namespace {

        // Per record: its owner's class (0 past the end of `classes`); empty without classes.
        std::vector<std::uint8_t> extentClasses(const Snapshot& snapshot, std::span<const std::uint8_t> classes,
                                                Executor& executor, const CancelToken& token) {
            const auto records = snapshot.extents.records();
            std::vector<std::uint8_t> out(classes.empty() ? 0 : records.size(), 0);
            executor.parallelFor(
                {.count = out.size(), .grain = 65536},
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t i = begin; i < end; ++i) {
                        const FileIndex file = records[i].file();
                        out[i] = file < classes.size() ? classes[file] : 0;
                    }
                },
                token);
            return out;
        }

        // A record's class, 0 without classes.
        std::uint8_t kindAt(std::span<const std::uint8_t> kinds, std::size_t record) {
            return record < kinds.size() ? kinds[record] : 0;
        }

        // Whether two records hold the same run.
        bool sameRun(const ExtentRecord& a, const ExtentRecord& b) {
            return a.lcn() == b.lcn() && a.length() == b.length();
        }

        // Appends the runs of records.
        void appendRuns(std::span<const ExtentRecord> records, std::vector<ClusterRun>& out) {
            for (const ExtentRecord& r : records) {
                out.push_back(r.run());
            }
        }

        // LCN ranges where the class of the extents differs between two snapshots of a volume: records with the same
        // run and another class, and records found in only one of them (their files changed; usually dirty already).
        std::vector<ClusterRun> classChanges(const Snapshot& before, std::span<const std::uint8_t> kindsBefore,
                                             const Snapshot& after, std::span<const std::uint8_t> kindsAfter) {
            const auto a = before.extents.records();
            const auto b = after.extents.records();
            std::vector<ClusterRun> out;
            std::size_t i = 0;
            std::size_t k = 0;
            while (i < a.size() && k < b.size()) {
                if (!sameRun(a[i], b[k])) {
                    const bool fromBefore = a[i].lcn() < b[k].lcn();
                    out.push_back(fromBefore ? a[i++].run() : b[k++].run());
                    continue;
                }
                if (kindAt(kindsBefore, i) != kindAt(kindsAfter, k)) {
                    out.push_back(a[i].run());
                }
                ++i;
                ++k;
            }
            appendRuns(a.subspan(i), out);
            appendRuns(b.subspan(k), out);
            return out;
        }

    } // namespace

    std::shared_ptr<const MapData> MapData::build(Snapshot snapshot, ScreenAspect aspect, Executor& executor,
                                                  const CancelToken& token, std::span<const std::uint8_t> classes) {
        const World world(snapshot.geometry.totalClusters, aspect);
        // Not make_shared: the constructor is private.
        std::shared_ptr<MapData> data(new MapData(std::move(snapshot), world));
        data->ageReference_ = newestModification(data->snapshot_, executor, token);
        // Per file while the pyramid is built (it reads them once), then kept per record for the tiles.
        const FileColorValues values = fileColorValues(data->snapshot_, data->ageReference_, executor, token);
        data->pyramid_ = Pyramid::build({.snapshot = data->snapshot_,
                                         .world = data->world_,
                                         .fileKinds = classes,
                                         .fileHeat = values.heat,
                                         .fileSizes = values.sizes},
                                        executor, token);
        data->fillRecordData(values, classes, executor, token);
        return data;
    }

    std::shared_ptr<const MapData> MapData::update(const MapData& previous, Snapshot snapshot,
                                                   std::span<const ClusterRun> dirty, Executor& executor,
                                                   const CancelToken& token, std::span<const std::uint8_t> classes) {
        if (snapshot.geometry.totalClusters != previous.snapshot_.geometry.totalClusters) {
            throw SnapshotError("a map can only be updated with a snapshot of the same volume size");
        }
        std::shared_ptr<MapData> data(new MapData(std::move(snapshot), previous.world_));
        data->ageReference_ = newestModification(data->snapshot_, executor, token);
        const FileColorValues values = fileColorValues(data->snapshot_, data->ageReference_, executor, token);
        const PyramidInputs inputs{.snapshot = data->snapshot_,
                                   .world = data->world_,
                                   .fileKinds = classes,
                                   .fileHeat = values.heat,
                                   .fileSizes = values.sizes};
        data->fillRecordData(values, classes, executor, token);
        if (data->ageReference_ != previous.ageReference_) {
            data->pyramid_ = Pyramid::build(inputs, executor, token); // Every file's age value moved.
            return data;
        }
        std::vector<ClusterRun> ranges(dirty.begin(), dirty.end());
        const auto changed =
            classChanges(previous.snapshot_, previous.extentKinds_, data->snapshot_, data->extentKinds_);
        ranges.insert(ranges.end(), changed.begin(), changed.end());
        data->pyramid_ = previous.pyramid_;
        for (const ClusterRun& range : FreeRunList(std::move(ranges), data->snapshot_.geometry.totalClusters).runs()) {
            data->pyramid_.update(inputs, range, executor, token);
        }
        return data;
    }

    void MapData::fillRecordData(const FileColorValues& values, std::span<const std::uint8_t> classes,
                                 Executor& executor, const CancelToken& token) {
        extentValues_ = extentColorValues(snapshot_, values, executor, token);
        extentKinds_ = extentClasses(snapshot_, classes, executor, token);
        fileClasses_.assign(classes.begin(), classes.end());
        extentFlags_ = extentMapFlags(snapshot_, executor, token);
        recordBlocks_ = recordBlocks(snapshot_, executor, token);
    }

    MapSource MapData::source() const {
        return {.world = world_,
                .pyramid = pyramid_,
                .facts = {.snapshot = snapshot_,
                          .extentFlags = extentFlags_,
                          .extentValues = extentValues_,
                          .extentKinds = extentKinds_,
                          .recordBlocks = recordBlocks_}};
    }

    std::size_t MapData::memoryBytes() const {
        return pyramid_.memoryBytes() + extentFlags_.size() + extentValues_.size() + extentKinds_.size() +
            recordBlocks_.size() * sizeof(std::uint32_t);
    }

} // namespace icd::core
