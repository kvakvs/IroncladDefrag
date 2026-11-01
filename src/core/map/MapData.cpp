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
        data->values_ = fileColorValues(data->snapshot_, data->ageReference_, executor, token);
        data->pyramid_ = Pyramid::build({.snapshot = data->snapshot_,
                                         .world = data->world_,
                                         .fileHeat = data->values_.heat,
                                         .fileSizes = data->values_.sizes},
                                        executor, token);
        data->extentFlags_ = extentMapFlags(data->snapshot_, executor, token);
        return data;
    }

    MapSource MapData::source() const {
        return {.world = world_,
                .pyramid = pyramid_,
                .facts = {
                    .snapshot = snapshot_, .extentFlags = extentFlags_, .heat = values_.heat, .sizes = values_.sizes}};
    }

    std::size_t MapData::memoryBytes() const {
        return pyramid_.memoryBytes() + extentFlags_.size() + values_.heat.size() + values_.sizes.size();
    }

} // namespace icd::core
