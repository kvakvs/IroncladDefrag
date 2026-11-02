#pragma once

#include "core/model/DriveInfo.h"
#include "services/workflow/FakeResults.h"

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace icd::services {

    // A strategy choice remembered for one drive, so selecting the drive again (also after a restart) shows the
    // preset and effort level the user last picked for it.
    struct DriveStrategy {
        std::string preset; // core::strategy::ZoneScript::id of a built-in preset; an id no longer known falls back to the default preset.
        EffortLevel effort = EffortLevel::Fast; // Last chosen effort level.

        // Field-wise equality, so an unchanged choice is not written again.
        bool operator==(const DriveStrategy&) const = default;
    };

    // Everything the app keeps between runs (plan.md Phase 10: versioned JSON). T130 and T131 add the Fast budget
    // multiplier, manual caps and custom strategies here.
    struct Settings {
        std::map<std::string, DriveStrategy> driveStrategies; // Per drive, by driveSettingsKey().

        // Field-wise equality, for round-trip checks.
        bool operator==(const Settings&) const = default;
    };

    // Format version written to the settings file; files of other versions are refused, not guessed at.
    inline constexpr std::uint64_t settingsVersion = 1;

    // A settings file that cannot be used (malformed JSON, wrong shape, other version); the message says why.
    class SettingsError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // The settings as an indented JSON document.
    [[nodiscard]] std::string settingsJson(const Settings& settings);

    // Settings from a JSON document. Throws SettingsError when the document is malformed, of another version or of
    // the wrong shape; a drive entry with an unknown effort level is dropped, so one bad entry keeps the rest.
    [[nodiscard]] Settings parseSettings(std::string_view text);

    // The key a drive's settings are stored under: the volume GUID name, which survives drive-letter changes, or
    // the root for drives without one (fake drives).
    [[nodiscard]] std::string driveSettingsKey(const core::DriveInfo& drive);

    // Stable spelling of an effort level in the settings file ("fast", "moderate", "maximum").
    [[nodiscard]] const char* effortId(EffortLevel level);

    // The effort level of a settings-file spelling; nullopt for anything else.
    [[nodiscard]] std::optional<EffortLevel> effortFromId(std::string_view id);

} // namespace icd::services
