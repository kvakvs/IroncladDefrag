#include "core/map/ColorScheme.h"

#include "core/classify/Catalog.h"

#include <algorithm>
#include <array>
#include <utility>

namespace icd::core {

    namespace {

        // A preset color of one legend key.
        using KeyColor = std::pair<std::string_view, Rgba>;

        // Every preset, in picker order.
        constexpr std::array presets{ColorPreset::Standard, ColorPreset::ColorblindSafe, ColorPreset::Dark};

        // Settings-file spellings and picker names, in ColorPreset order.
        constexpr std::array presetIds{"standard", "colorblind", "dark"};
        constexpr std::array presetNames{"Standard", "Colorblind-safe", "Dark"};

        // Prefix of the class keys; the rest is the catalog class id.
        constexpr std::string_view classPrefix = "class.";

        // Color of keys a preset does not know.
        constexpr Rgba unknownKeyColor = rgb(128, 128, 128);

        // The original map colors (T030-T074); classes come from the catalog.
        constexpr std::array standardColors{
            KeyColor{"free", OccupancyPalette::freeColor},
            KeyColor{"fragmentation.contiguous", OccupancyPalette::usedColor},
            KeyColor{"fragmentation.fragmented", OccupancyPalette::fragmentedColor},
            KeyColor{"fragmentation.unmovable", OccupancyPalette::unmovableColor},
            KeyColor{"age.0", rgb(214, 48, 49)},
            KeyColor{"age.1", rgb(238, 130, 38)},
            KeyColor{"age.2", rgb(222, 186, 40)},
            KeyColor{"age.3", rgb(96, 172, 110)},
            KeyColor{"age.4", rgb(60, 124, 196)},
            KeyColor{"age.5", rgb(88, 64, 150)},
            KeyColor{"size.0", rgb(140, 200, 236)},
            KeyColor{"size.1", rgb(84, 160, 220)},
            KeyColor{"size.2", rgb(48, 116, 196)},
            KeyColor{"size.3", rgb(40, 78, 164)},
            KeyColor{"size.4", rgb(44, 44, 122)},
            KeyColor{"size.5", rgb(34, 18, 70)},
            KeyColor{"activity.read", rgb(18, 40, 110)}, // Light free space: dark and saturated pulses.
            KeyColor{"activity.write", rgb(232, 112, 0)},
        };

        // Okabe-Ito for occupancy, viridis (age) and cividis (size) ramps, Paul Tol's muted and light sets for
        // classes: hues chosen to stay apart for deuteranopia, protanopia and tritanopia.
        constexpr std::array colorblindColors{
            KeyColor{"free", rgb(246, 246, 246)},
            KeyColor{"fragmentation.contiguous", rgb(0, 114, 178)},
            KeyColor{"fragmentation.fragmented", rgb(213, 94, 0)},
            KeyColor{"fragmentation.unmovable", rgb(0, 158, 115)},
            KeyColor{"age.0", rgb(253, 231, 37)},
            KeyColor{"age.1", rgb(122, 209, 81)},
            KeyColor{"age.2", rgb(34, 168, 132)},
            KeyColor{"age.3", rgb(42, 120, 142)},
            KeyColor{"age.4", rgb(65, 68, 135)},
            KeyColor{"age.5", rgb(68, 1, 84)},
            KeyColor{"size.0", rgb(255, 234, 70)},
            KeyColor{"size.1", rgb(203, 186, 105)},
            KeyColor{"size.2", rgb(149, 143, 120)},
            KeyColor{"size.3", rgb(102, 105, 112)},
            KeyColor{"size.4", rgb(49, 68, 107)},
            KeyColor{"size.5", rgb(0, 34, 78)},
            KeyColor{"class.unknown", rgb(187, 187, 187)},
            KeyColor{"class.metadata", rgb(85, 85, 85)},
            KeyColor{"class.directories", rgb(170, 68, 153)},
            KeyColor{"class.source", rgb(136, 204, 238)},
            KeyColor{"class.repository", rgb(51, 34, 136)},
            KeyColor{"class.dependencies", rgb(68, 170, 153)},
            KeyColor{"class.build", rgb(221, 204, 119)},
            KeyColor{"class.temp", rgb(238, 204, 102)},
            KeyColor{"class.logs", rgb(187, 187, 136)},
            KeyColor{"class.archives", rgb(136, 34, 85)},
            KeyColor{"class.disc-images", rgb(204, 102, 119)},
            KeyColor{"class.vm-disks", rgb(153, 153, 51)},
            KeyColor{"class.backups", rgb(238, 136, 102)},
            KeyColor{"class.dumps", rgb(102, 17, 0)},
            KeyColor{"class.photos", rgb(17, 119, 51)},
            KeyColor{"class.catalogs", rgb(255, 170, 187)},
            KeyColor{"class.video", rgb(0, 119, 187)},
            KeyColor{"activity.read", rgb(12, 36, 97)}, // Navy and Okabe-Ito orange stay apart for every deficiency.
            KeyColor{"activity.write", rgb(230, 159, 0)},
        };

        // Dark free space with brighter data colors, so the map does not glare in a dark room.
        constexpr std::array darkColors{
            KeyColor{"free", rgb(38, 38, 38)},
            KeyColor{"fragmentation.contiguous", rgb(79, 143, 224)},
            KeyColor{"fragmentation.fragmented", rgb(255, 107, 91)},
            KeyColor{"fragmentation.unmovable", rgb(76, 194, 122)},
            KeyColor{"age.0", rgb(255, 90, 95)},
            KeyColor{"age.1", rgb(255, 159, 67)},
            KeyColor{"age.2", rgb(247, 208, 70)},
            KeyColor{"age.3", rgb(123, 211, 137)},
            KeyColor{"age.4", rgb(78, 168, 222)},
            KeyColor{"age.5", rgb(157, 123, 224)},
            KeyColor{"size.0", rgb(30, 74, 110)},
            KeyColor{"size.1", rgb(36, 104, 154)},
            KeyColor{"size.2", rgb(43, 134, 197)},
            KeyColor{"size.3", rgb(75, 163, 227)},
            KeyColor{"size.4", rgb(124, 192, 240)},
            KeyColor{"size.5", rgb(181, 221, 250)},
            KeyColor{"class.unknown", rgb(160, 160, 160)},
            KeyColor{"class.metadata", rgb(120, 120, 150)},
            KeyColor{"class.directories", rgb(216, 180, 226)},
            KeyColor{"class.source", rgb(108, 180, 255)},
            KeyColor{"class.repository", rgb(74, 143, 224)},
            KeyColor{"class.dependencies", rgb(127, 209, 209)},
            KeyColor{"class.build", rgb(255, 182, 80)},
            KeyColor{"class.temp", rgb(255, 228, 92)},
            KeyColor{"class.logs", rgb(214, 200, 150)},
            KeyColor{"class.archives", rgb(192, 138, 85)},
            KeyColor{"class.disc-images", rgb(215, 162, 116)},
            KeyColor{"class.vm-disks", rgb(160, 112, 63)},
            KeyColor{"class.backups", rgb(154, 132, 112)},
            KeyColor{"class.dumps", rgb(230, 100, 100)},
            KeyColor{"class.photos", rgb(114, 212, 116)},
            KeyColor{"class.catalogs", rgb(255, 122, 168)},
            KeyColor{"class.video", rgb(164, 124, 255)},
            KeyColor{"activity.read", rgb(232, 244, 255)}, // Cool white and warm amber over the dark map.
            KeyColor{"activity.write", rgb(255, 176, 48)},
        };

        // The preset's own table.
        std::span<const KeyColor> tableOf(ColorPreset preset) {
            switch (preset) {
            case ColorPreset::ColorblindSafe:
                return colorblindColors;
            case ColorPreset::Dark:
                return darkColors;
            case ColorPreset::Standard:
                break;
            }
            return standardColors;
        }

        // A class key's catalog color (the standard class colors); nullopt for other keys or unknown classes.
        std::optional<Rgba> catalogColor(std::string_view key) {
            if (!key.starts_with(classPrefix)) {
                return std::nullopt;
            }
            const classify::Catalog& catalog = classify::builtinCatalog();
            const std::string_view id = key.substr(classPrefix.size());
            const auto found = std::ranges::find(catalog.classes, id, &classify::ClassInfo::id);
            if (found == catalog.classes.end()) {
                return std::nullopt;
            }
            return rgb(static_cast<std::uint8_t>(found->color >> 16U), static_cast<std::uint8_t>(found->color >> 8U),
                       static_cast<std::uint8_t>(found->color));
        }

    } // namespace

    std::span<const ColorPreset> colorPresets() { return presets; }

    std::string_view presetId(ColorPreset preset) { return presetIds.at(static_cast<std::size_t>(preset)); }

    std::string_view presetName(ColorPreset preset) { return presetNames.at(static_cast<std::size_t>(preset)); }

    std::optional<ColorPreset> presetFromId(std::string_view id) {
        const auto found = std::ranges::find(presetIds, id);
        return found == presetIds.end()
            ? std::nullopt
            : std::optional(presets.at(static_cast<std::size_t>(found - presetIds.begin())));
    }

    Rgba presetColor(ColorPreset preset, std::string_view key) {
        const std::span<const KeyColor> table = tableOf(preset);
        if (const auto found = std::ranges::find(table, key, &KeyColor::first); found != table.end()) {
            return found->second;
        }
        // Classes a preset does not list (a catalog newer than the preset) keep their catalog color.
        return catalogColor(key).value_or(unknownKeyColor);
    }

    Rgba ColorScheme::color(std::string_view key) const {
        if (const auto found = custom_.find(key); found != custom_.end()) {
            return found->second;
        }
        return presetColor(preset_, key);
    }

    Rgba ColorScheme::outside() const { return preset_ == ColorPreset::Dark ? rgb(58, 58, 58) : rgb(200, 200, 200); }

} // namespace icd::core
