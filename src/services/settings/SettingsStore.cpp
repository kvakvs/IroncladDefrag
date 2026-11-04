#include "services/settings/SettingsStore.h"

#include "core/log/Log.h"
#include "platform/win/LogSinks.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace icd::services {

    namespace {

        // Moves an unusable settings file aside so the next save does not destroy it.
        void keepBadFile(const std::filesystem::path& file) {
            std::filesystem::path bad = file;
            bad += ".bad";
            std::error_code error;
            std::filesystem::rename(file, bad, error);
        }

    } // namespace

    std::optional<std::string> readTextFile(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void writeFileAtomically(const std::filesystem::path& file, std::string_view text) {
        std::filesystem::create_directories(file.parent_path());
        std::filesystem::path temporary = file;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            out.flush();
            if (!out) {
                throw std::runtime_error("cannot write " + temporary.string());
            }
        }
        std::filesystem::rename(temporary, file); // Replaces the old file in one step.
    }

    SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) { load(); }

    std::filesystem::path SettingsStore::defaultPath() {
        return platform::defaultLogDirectory().parent_path() / L"settings.json";
    }

    void SettingsStore::load() {
        const std::optional<std::string> text = readTextFile(file_);
        if (!text) {
            return; // First run: defaults.
        }
        try {
            settings_ = parseSettings(*text);
        }
        catch (const SettingsError& error) {
            core::logging::warning("settings file unusable, defaults used",
                                   {{"path", file_.string()}, {"reason", error.what()}});
            keepBadFile(file_);
        }
    }

    void SettingsStore::save() const { writeFileAtomically(file_, settingsJson(settings_)); }

    bool SettingsStore::update(Settings settings) {
        settings_ = std::move(settings);
        try {
            save();
            return true;
        }
        catch (const std::exception& error) {
            core::logging::warning("settings not saved", {{"path", file_.string()}, {"reason", error.what()}});
            return false;
        }
    }

    std::optional<DriveStrategy> SettingsStore::driveStrategy(const std::string& key) const {
        const auto found = settings_.driveStrategies.find(key);
        return found == settings_.driveStrategies.end() ? std::nullopt : std::optional(found->second);
    }

    bool SettingsStore::rememberDriveStrategy(const std::string& key, const DriveStrategy& choice) {
        if (driveStrategy(key) == choice) {
            return true;
        }
        Settings next = settings_;
        next.driveStrategies[key] = choice;
        return update(std::move(next));
    }

} // namespace icd::services
