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
        data->extentValues_ = extentColorValues(data->snapshot_, values, executor, token);
        data->extentFlags_ = extentMapFlags(data->snapshot_, executor, token);
        data->recordBlocks_ = recordBlocks(data->snapshot_, executor, token);
        return data;
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
