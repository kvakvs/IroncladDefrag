#pragma once

#include "core/map/ColorScheme.h"
#include "core/model/DriveInfo.h"
#include "core/resources/ResourceCaps.h"
#include "services/workflow/FakeResults.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace icd::services {

    // A strategy choice remembered for one drive, so selecting the drive again (also after a restart) shows the
    // preset and effort level the user last picked for it.
    struct DriveStrategy {
        std::string preset; // ZoneScript::id of a built-in preset or a custom strategy; an id no longer known falls
                            // back to the default preset.
        EffortLevel effort = EffortLevel::Fast; // Last chosen effort level.

        // Field-wise equality, so an unchanged choice is not written again.
        bool operator==(const DriveStrategy&) const = default;
    };

    // The map's colors (T125): the chosen preset and the user's own colors for each preset, so switching presets keeps
    // every preset's edits.
    struct MapColorSettings {
        core::ColorPreset preset = core::ColorPreset::Standard; // The preset in use.
        std::map<core::ColorPreset, core::ColorScheme::Custom> custom; // Per preset: the user's colors by legend key.

        // Field-wise equality, so an unchanged choice is not written again.
        bool operator==(const MapColorSettings&) const = default;
    };

    // The colors the map draws with for these settings.
    [[nodiscard]] core::ColorScheme colorSchemeOf(const MapColorSettings& colors);

    // "#RRGGBB" of a color (the alpha byte is left out).
    [[nodiscard]] std::string colorText(core::Rgba color);

    // The opaque color of "#RRGGBB" (case-insensitive); nullopt for anything else.
    [[nodiscard]] std::optional<core::Rgba> colorFromText(std::string_view text);

    // Light or dark windows (T126); applied when the app starts.
    enum class Appearance : std::uint8_t {
        System, // Follow Windows' app mode (Settings > Personalization > Colors).
        Light, // Always light.
        Dark, // Always dark.
    };

    // Stable spelling of an appearance in the settings file ("system", "light", "dark").
    [[nodiscard]] const char* appearanceId(Appearance appearance);

    // The appearance of a settings-file spelling; nullopt for anything else.
    [[nodiscard]] std::optional<Appearance> appearanceFromId(std::string_view id);

    // Everything the app keeps between runs (plan.md Phase 10: versioned JSON).
    struct Settings {
        std::map<std::string, DriveStrategy> driveStrategies; // Per drive, by driveSettingsKey().
        std::uint32_t fastBudgetMultiplier =
            1; // Scales the Fast level's budget (plan.md 9.2.1): one of fastBudgetMultipliers().
        core::ResourceSettings resources; // Manual thread and memory caps (plan.md 6.6); 0 is Auto.
        MapColorSettings mapColors; // Map color preset and the user's colors.
        Appearance appearance = Appearance::System; // Light or dark windows, from the next start.
        bool mapActivity = true; // View > Read/write activity: pulses where the disk is read and written (T114).

        // Field-wise equality, for round-trip checks.
        bool operator==(const Settings&) const = default;
    };

    // Format version written to the settings file. Version 1 (drive choices only) is still read; files of other
    // versions are refused, not guessed at.
    inline constexpr std::uint64_t settingsVersion = 2;

    // The Fast budget multipliers Settings offers (plan.md 14 decision 5), smallest first.
    [[nodiscard]] std::span<const std::uint32_t> fastBudgetMultipliers();

    // A settings file that cannot be used (malformed JSON, wrong shape, other version); the message says why.
    class SettingsError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // The settings as an indented JSON document.
    [[nodiscard]] std::string settingsJson(const Settings& settings);

    // Settings from a JSON document. Throws SettingsError when the document is malformed, of another version or of
    // the wrong shape; a drive entry with an unknown effort level is dropped, so one bad entry keeps the rest, and
    // a multiplier or cap that is not a usable number keeps its default, and so do an unknown color preset and
    // colors that are not "#RRGGBB".
    [[nodiscard]] Settings parseSettings(std::string_view text);

    // The key a drive's settings are stored under: the volume GUID name, which survives drive-letter changes, or
    // the root for drives without one (fake drives).
    [[nodiscard]] std::string driveSettingsKey(const core::DriveInfo& drive);

    // Stable spelling of an effort level in the settings file ("fast", "moderate", "maximum").
    [[nodiscard]] const char* effortId(EffortLevel level);

    // The effort level of a settings-file spelling; nullopt for anything else.
    [[nodiscard]] std::optional<EffortLevel> effortFromId(std::string_view id);

} // namespace icd::services
