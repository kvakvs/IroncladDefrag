#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/map/ColorModes.h"
#include "core/map/Pyramid.h"
#include "core/map/TileRasterizer.h"
#include "core/map/World.h"
#include "core/snapshot/Snapshot.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace icd::core {

    // Everything the drive map draws from, owned together: the snapshot, its fixed world, the pyramid, the
    // per-extent map flags and the per-extent values of the color modes. Immutable once built and shared (shared_ptr)
    // between the UI and the workers that render tiles, so a tile still rendering keeps its data alive after the map
    // moved on.
    class MapData {
    public:
        // Builds the world for a screen aspect, the color-mode values, the pyramid and the flags.
        [[nodiscard]] static std::shared_ptr<const MapData> build(Snapshot snapshot, ScreenAspect aspect,
                                                                  Executor& executor, const CancelToken& token = {});

        // The snapshot.
        [[nodiscard]] const Snapshot& snapshot() const { return snapshot_; }

        // The world layout.
        [[nodiscard]] const World& world() const { return world_; }

        // The pyramid.
        [[nodiscard]] const Pyramid& pyramid() const { return pyramid_; }

        // What renderTile() needs.
        [[nodiscard]] MapSource source() const;

        // The time file ages are measured from (newestModification()).
        [[nodiscard]] std::int64_t ageReference() const { return ageReference_; }

        // Bytes of the map's own data (pyramid, flags and color values; not the snapshot).
        [[nodiscard]] std::size_t memoryBytes() const;

    private:
        // Takes the parts.
        MapData(Snapshot snapshot, World world);

        Snapshot snapshot_; // Layout of the volume.
        World world_; // Cell layout, fixed for this map.
        Pyramid pyramid_; // Coarse summaries.
        std::vector<std::uint8_t> extentFlags_; // Per-record map flags.
        std::vector<std::uint8_t> extentValues_; // Per-record age and size values of the color modes.
        std::vector<std::uint32_t> recordBlocks_; // LCN block -> first extent record, for fine tiles.
        std::int64_t ageReference_ = 0; // What ages are measured from.
    };

    // Hash of a tile id, for tile caches.
    struct TileIdHash {
        // Mixes level and position.
        std::size_t operator()(const TileId& tile) const {
            const std::uint64_t mixed = (tile.x * 0x9E3779B97F4A7C15ULL) ^ (tile.y * 0xC2B2AE3D27D4EB4FULL) ^
                static_cast<std::uint64_t>(static_cast<std::int64_t>(tile.level));
            return std::hash<std::uint64_t>{}(mixed);
        }
    };

} // namespace icd::core
