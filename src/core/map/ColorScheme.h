#pragma once

#include "core/map/Palette.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace icd::core {

    // The built-in color sets of the map (plan.md 7.4); the user picks one and may change any of its colors.
    enum class ColorPreset : std::uint8_t {
        Standard, // The original colors: light map, catalog class colors.
        ColorblindSafe, // Okabe-Ito and viridis-like hues that stay apart under the common color vision deficiencies.
        Dark, // Dark free space and background, brighter data colors.
    };

    // Every preset, in the order the picker lists them.
    [[nodiscard]] std::span<const ColorPreset> colorPresets();

    // Stable spelling in the settings file ("standard", "colorblind", "dark").
    [[nodiscard]] std::string_view presetId(ColorPreset preset);

    // Name for the picker ("Standard", "Colorblind-safe", "Dark").
    [[nodiscard]] std::string_view presetName(ColorPreset preset);

    // The preset of a settings-file spelling; nullopt for anything else.
    [[nodiscard]] std::optional<ColorPreset> presetFromId(std::string_view id);

    // A preset's color for a legend key ("free", "fragmentation.contiguous", "age.0", "size.5", "class.photos"; see
    // LegendEntry::key); a neutral grey for keys it does not know.
    [[nodiscard]] Rgba presetColor(ColorPreset preset, std::string_view key);

    // The colors the map draws with: a preset plus the user's own colors for some legend keys. Plain data, so the
    // palettes and the legend take it by value and tests build their own.
    class ColorScheme {
    public:
        // The user's colors by legend key.
        using Custom = std::map<std::string, Rgba, std::less<>>;

        // A preset with custom colors over it.
        explicit ColorScheme(ColorPreset preset = ColorPreset::Standard, Custom custom = {}) :
            preset_(preset), custom_(std::move(custom)) {}

        // The preset under the custom colors.
        [[nodiscard]] ColorPreset preset() const { return preset_; }

        // The user's colors.
        [[nodiscard]] const Custom& custom() const { return custom_; }

        // The color of a legend key: the user's, else the preset's.
        [[nodiscard]] Rgba color(std::string_view key) const;

        // Area past the end of the volume and around the world (Palette::outside()).
        [[nodiscard]] Rgba outside() const;

        // Schemes compare as values (unchanged settings repaint nothing).
        friend bool operator==(const ColorScheme&, const ColorScheme&) = default;

    private:
        ColorPreset preset_; // Base colors.
        Custom custom_; // Overrides by key.
    };

} // namespace icd::core
