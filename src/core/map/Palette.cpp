#include "core/map/Palette.h"

#include <algorithm>
#include <cmath>

namespace icd::core {

    namespace {

        // One channel of a color.
        std::uint32_t channel(Rgba color, unsigned shift) { return (color >> shift) & 0xFFU; }

        // A mix of two colors: from + (to - from) * share / 256.
        struct Mix {
            Rgba from = 0; // Color at share 0.
            Rgba to = 0; // Color at share 256.
            std::uint32_t share = 0; // 0-256.
        };

        // The mixed color, per channel in integers (this runs once per tile pixel).
        Rgba blend(const Mix& mix) {
            const Rgba from = mix.from;
            const Rgba to = mix.to;
            const std::uint32_t share = mix.share;
            Rgba out = 0xFF000000U;
            for (const unsigned shift : {16U, 8U, 0U}) {
                const auto a = static_cast<std::int32_t>(channel(from, shift));
                const auto b = static_cast<std::int32_t>(channel(to, shift));
                out |= static_cast<std::uint32_t>(a + (b - a) * static_cast<std::int32_t>(share) / 256) << shift;
            }
            return out;
        }

    } // namespace

    Rgba OccupancyPalette::color(const PixelSummary& summary) const {
        const MapBin& bin = summary.bin;
        const int level = summary.level;
        const ClusterCount capacity = summary.capacity;
        const ClusterCount used = ClusterCount{bin.used} << binUsedShift(std::max(level, 0));
        if (used == 0) {
            return freeColor;
        }
        Rgba data = usedColor;
        if ((bin.flags & BinUnmovable) != 0) {
            data = unmovableColor;
        }
        else if (bin.fragmented >= 128) {
            data = fragmentedColor;
        }
        const ClusterCount whole = std::max<ClusterCount>(capacity, 1);
        const auto share = static_cast<std::uint32_t>(std::min<ClusterCount>(256, (used * 256 + whole / 2) / whole));
        return blend({.from = freeColor, .to = data, .share = share});
    }

} // namespace icd::core
