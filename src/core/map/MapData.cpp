#include "core/map/MapData.h"

#include <utility>

namespace icd::core {

    MapData::MapData(Snapshot snapshot, World world) : snapshot_(std::move(snapshot)), world_(world) {}

    std::shared_ptr<const MapData> MapData::build(Snapshot snapshot, ScreenAspect aspect, Executor& executor,
                                                  const CancelToken& token) {
        const World world(snapshot.geometry.totalClusters, aspect);
        // Not make_shared: the constructor is private.
        std::shared_ptr<MapData> data(new MapData(std::move(snapshot), world));
        data->pyramid_ = Pyramid::build({.snapshot = data->snapshot_, .world = data->world_}, executor, token);
        data->extentFlags_ = extentMapFlags(data->snapshot_, executor, token);
        return data;
    }

    MapSource MapData::source() const {
        return {.world = world_, .pyramid = pyramid_, .facts = {.snapshot = snapshot_, .extentFlags = extentFlags_}};
    }

    std::size_t MapData::memoryBytes() const { return pyramid_.memoryBytes() + extentFlags_.size(); }

} // namespace icd::core
