#include "core/map/MapData.h"

#include <utility>

namespace icd::core {

    MapData::MapData(Snapshot snapshot, World world) : snapshot_(std::move(snapshot)), world_(world) {}

    std::shared_ptr<const MapData> MapData::build(Snapshot snapshot, ScreenAspect aspect, Executor& executor,
                                                  const CancelToken& token) {
        const World world(snapshot.geometry.totalClusters, aspect);
        // Not make_shared: the constructor is private.
        std::shared_ptr<MapData> data(new MapData(std::move(snapshot), world));
        data->ageReference_ = newestModification(data->snapshot_, executor, token);
        // Per file while the pyramid is built (it reads them once), then kept per record for the tiles.
        const FileColorValues values = fileColorValues(data->snapshot_, data->ageReference_, executor, token);
        data->pyramid_ = Pyramid::build(
            {.snapshot = data->snapshot_, .world = data->world_, .fileHeat = values.heat, .fileSizes = values.sizes},
            executor, token);
        data->fillRecordData(values, executor, token);
        return data;
    }

    std::shared_ptr<const MapData> MapData::update(const MapData& previous, Snapshot snapshot,
                                                   std::span<const ClusterRun> dirty, Executor& executor,
                                                   const CancelToken& token) {
        if (snapshot.geometry.totalClusters != previous.snapshot_.geometry.totalClusters) {
            throw SnapshotError("a map can only be updated with a snapshot of the same volume size");
        }
        std::shared_ptr<MapData> data(new MapData(std::move(snapshot), previous.world_));
        data->ageReference_ = newestModification(data->snapshot_, executor, token);
        const FileColorValues values = fileColorValues(data->snapshot_, data->ageReference_, executor, token);
        const PyramidInputs inputs{
            .snapshot = data->snapshot_, .world = data->world_, .fileHeat = values.heat, .fileSizes = values.sizes};
        if (data->ageReference_ != previous.ageReference_) {
            data->pyramid_ = Pyramid::build(inputs, executor, token); // Every file's age value moved.
        }
        else {
            data->pyramid_ = previous.pyramid_;
            for (const ClusterRun& range : dirty) {
                data->pyramid_.update(inputs, range, executor, token);
            }
        }
        data->fillRecordData(values, executor, token);
        return data;
    }

    void MapData::fillRecordData(const FileColorValues& values, Executor& executor, const CancelToken& token) {
        extentValues_ = extentColorValues(snapshot_, values, executor, token);
        extentFlags_ = extentMapFlags(snapshot_, executor, token);
        recordBlocks_ = recordBlocks(snapshot_, executor, token);
    }

    MapSource MapData::source() const {
        return {.world = world_,
                .pyramid = pyramid_,
                .facts = {.snapshot = snapshot_,
                          .extentFlags = extentFlags_,
                          .extentValues = extentValues_,
                          .recordBlocks = recordBlocks_}};
    }

    std::size_t MapData::memoryBytes() const {
        return pyramid_.memoryBytes() + extentFlags_.size() + extentValues_.size() +
            recordBlocks_.size() * sizeof(std::uint32_t);
    }

} // namespace icd::core
