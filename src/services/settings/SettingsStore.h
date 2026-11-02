#pragma once

#include "services/settings/Settings.h"

#include <filesystem>
#include <optional>
#include <string>

namespace icd::services {

    // The settings file: read once at start-up, written whole after each change. The file is a few hundred bytes,
    // so a save is cheap enough for the UI thread; it writes a temporary file and renames it over the old one, so
    // a crash mid-save keeps the previous settings. Not thread-safe: the UI thread owns it.
    class SettingsStore {
    public:
        // Loads file. A missing file gives defaults; an unusable one is logged, kept beside as "<name>.bad" and
        // replaced by defaults, so a damaged file never blocks start-up.
        explicit SettingsStore(std::filesystem::path file);

        // %LOCALAPPDATA%\IroncladDefrag\settings.json, beside the logs and snapshots.
        [[nodiscard]] static std::filesystem::path defaultPath();

        // The settings in force.
        [[nodiscard]] const Settings& current() const { return settings_; }

        // Where the settings are kept.
        [[nodiscard]] const std::filesystem::path& path() const { return file_; }

        // Replaces the settings and saves them. Returns false (and logs) when the file cannot be written; the new
        // values still apply for this run.
        bool update(Settings settings);

        // The strategy last chosen for a drive (key from driveSettingsKey()), if any.
        [[nodiscard]] std::optional<DriveStrategy> driveStrategy(const std::string& key) const;

        // Remembers a drive's strategy choice; an unchanged choice is not written again. False when saving failed.
        bool rememberDriveStrategy(const std::string& key, const DriveStrategy& choice);

    private:
        // Reads file_ into settings_ (see the constructor).
        void load();

        // Writes settings_ to file_; throws on I/O errors.
        void save() const;

        std::filesystem::path file_; // The settings file.
        Settings settings_; // In force; what the file holds after the last successful save.
    };

} // namespace icd::services
