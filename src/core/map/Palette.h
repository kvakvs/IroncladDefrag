#pragma once

#include "core/map/BinSummary.h"
#include "core/snapshot/Types.h"

#include <cstdint>

namespace icd::core {

    // A pixel as 0xAARRGGBB. In little-endian memory that is B, G, R, A: the layout of 32-bit DIBs and of
    // Direct2D B8G8R8A8 bitmaps, so tiles go to the GPU without conversion.
    using Rgba = std::uint32_t;

    // An opaque color.
    [[nodiscard]] constexpr Rgba rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return 0xFF000000U | (std::uint32_t{red} << 16U) | (std::uint32_t{green} << 8U) | blue;
    }

    // What a palette colors: a summary of a 2^level x 2^level block (level 0 or less: one cell) that holds
    // capacity clusters of the volume (never 0; blocks wholly outside the volume get Palette::outside()).
    struct PixelSummary {
        MapBin bin; // The summary.
        int level = 0; // Its level.
        ClusterCount capacity = 1; // Clusters of the volume it covers.

        // Summaries compare as values (color caching).
        friend constexpr bool operator==(const PixelSummary&, const PixelSummary&) = default;
    };

    // Maps a map summary to a color. Each color mode of the map (file class, fragmentation, age, ...) is one
    // palette (plan.md 7.4), so switching modes re-renders tiles without touching the data.
    class Palette {
    public:
        virtual ~Palette() = default;

        // Color of a summary.
        [[nodiscard]] virtual Rgba color(const PixelSummary& summary) const = 0;

        // Grid lines between cells at deep zoom.
        [[nodiscard]] virtual Rgba gridLine() const { return rgb(255, 255, 255); }

        // Area past the end of the volume.
        [[nodiscard]] virtual Rgba outside() const { return rgb(200, 200, 200); }

        // Clusters of the selected file (plan.md 7.4); a color no data or free space takes, so a selected
        // fragment stands out in every mode.
        [[nodiscard]] virtual Rgba selection() const { return rgb(255, 196, 0); }
    };

    // Used versus free space, with fragmented and unmovable data highlighted: the map's look until the color
    // modes exist (T032), and a stable palette for tests.
    class OccupancyPalette : public Palette {
    public:
        // Free cells.
        static constexpr Rgba freeColor = rgb(246, 246, 246);

        // Used, contiguous data.
        static constexpr Rgba usedColor = rgb(52, 110, 200);

        // Data of fragmented files.
        static constexpr Rgba fragmentedColor = rgb(212, 64, 52);

        // Unmovable data.
        static constexpr Rgba unmovableColor = rgb(46, 150, 82);

        // Blends from free to the data color by the used share of the block.
        [[nodiscard]] Rgba color(const PixelSummary& summary) const override;
    };

} // namespace icd::core
