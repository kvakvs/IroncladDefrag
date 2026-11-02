#include "services/settings/Settings.h"

#include "core/format/Json.h"
#include "core/format/JsonReader.h"

#include <array>
#include <format>

namespace icd::services {

    namespace {

        // Settings-file spellings of the effort levels, in EffortLevel order.
        constexpr std::array effortIds{"fast", "moderate", "maximum"};

        // A string member, or nullopt when it is absent or of another kind.
        std::optional<std::string> stringMember(const core::JsonValue& object, std::string_view key) {
            const core::JsonValue* member = object.find(key);
            return member != nullptr && member->isString() ? std::optional(member->asString()) : std::nullopt;
        }

        // One drive entry, or nullopt when it is not an object with a strategy and a known effort level.
        std::optional<DriveStrategy> driveEntry(const core::JsonValue& entry) {
            if (!entry.isObject()) {
                return std::nullopt;
            }
            const std::optional<std::string> preset = stringMember(entry, "strategy");
            const std::optional<std::string> effort = stringMember(entry, "effort");
            const std::optional<EffortLevel> level = effort ? effortFromId(*effort) : std::nullopt;
            if (!preset || !level) {
                return std::nullopt;
            }
            return DriveStrategy{*preset, *level};
        }

        // Reads the "drives" object into settings; unusable entries are skipped.
        void readDrives(const core::JsonValue& drives, Settings& settings) {
            for (const auto& [key, entry] : drives.asObject()) {
                if (std::optional<DriveStrategy> choice = driveEntry(entry)) {
                    settings.driveStrategies[key] = std::move(*choice);
                }
            }
        }

        // Checks the version member before anything else is read.
        void checkVersion(const core::JsonValue& root) {
            const core::JsonValue* version = root.find("version");
            if (version == nullptr) {
                throw SettingsError("settings file has no version");
            }
            const std::uint64_t number = version->asUnsigned();
            if (number != settingsVersion) {
                throw SettingsError(
                    std::format("settings file version {} is not supported (expected {})", number, settingsVersion));
            }
        }

    } // namespace

    std::string settingsJson(const Settings& settings) {
        core::JsonWriter out;
        out.beginObject().key("version").value(settingsVersion).key("drives").beginObject();
        for (const auto& [key, choice] : settings.driveStrategies) {
            out.key(key).beginObject();
            out.key("strategy").value(choice.preset).key("effort").value(effortId(choice.effort));
            out.endObject();
        }
        out.endObject().endObject();
        return out.str() + "\n";
    }

    Settings parseSettings(std::string_view text) {
        try {
            const core::JsonValue root = core::parseJson(text);
            if (!root.isObject()) {
                throw SettingsError("settings file is not a JSON object");
            }
            checkVersion(root);
            Settings settings;
            if (const core::JsonValue* drives = root.find("drives")) {
                readDrives(*drives, settings);
            }
            return settings;
        }
        catch (const core::JsonError& error) {
            throw SettingsError(std::string("settings file: ") + error.what());
        }
    }

    std::string driveSettingsKey(const core::DriveInfo& drive) {
        return drive.layout.volumeName.empty() ? drive.root : drive.layout.volumeName;
    }

    const char* effortId(EffortLevel level) { return effortIds.at(static_cast<std::size_t>(level)); }

    std::optional<EffortLevel> effortFromId(std::string_view id) {
        for (std::size_t index = 0; index < effortIds.size(); ++index) {
            if (id == effortIds.at(index)) {
                return static_cast<EffortLevel>(index);
            }
        }
        return std::nullopt;
    }

} // namespace icd::services
