#include "core/map/Palette.h"

#include <algorithm>
#include <cmath>

namespace icd::core {

    namespace {

        // One channel of a color.
        std::uint32_t channel(Rgba color, unsigned shift) { return (color >> shift) & 0xFFU; }

    } // namespace

    Rgba blendColors(const ColorMix& mix) {
        // Per channel in integers: this runs once per tile pixel.
        Rgba out = 0xFF000000U;
        for (const unsigned shift : {16U, 8U, 0U}) {
            const auto a = static_cast<std::int32_t>(channel(mix.from, shift));
            const auto b = static_cast<std::int32_t>(channel(mix.to, shift));
            out |= static_cast<std::uint32_t>(a + (b - a) * static_cast<std::int32_t>(mix.share) / 256) << shift;
        }
        return out;
    }

    std::uint32_t usedShare(const PixelSummary& summary) {
        const ClusterCount used = ClusterCount{summary.bin.used} << binUsedShift(std::max(summary.level, 0));
        const ClusterCount whole = std::max<ClusterCount>(summary.capacity, 1);
        return static_cast<std::uint32_t>(std::min<ClusterCount>(256, (used * 256 + whole / 2) / whole));
    }

    OccupancyPalette::OccupancyPalette() = default;

    OccupancyPalette::OccupancyPalette(const OccupancyColors& colors) :
        free_(colors.free), used_(colors.used), fragmented_(colors.fragmented), unmovable_(colors.unmovable),
        outside_(colors.outside) {}

    Rgba OccupancyPalette::color(const PixelSummary& summary) const {
        const MapBin& bin = summary.bin;
        if (bin.used == 0) {
            return free_;
        }
        Rgba data = used_;
        if ((bin.flags & BinUnmovable) != 0) {
            data = unmovable_;
        }
        else if (bin.fragmented >= 128) {
            data = fragmented_;
        }
        return blendColors({.from = free_, .to = data, .share = usedShare(summary)});
    }

} // namespace icd::core
