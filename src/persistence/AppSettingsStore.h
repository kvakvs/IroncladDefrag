#pragma once

#include "AppSettingsSerializer.h"

#include <filesystem>
#include <optional>

namespace icd {

// Loads and saves app settings under the current user's local app-data directory.
class AppSettingsStore {
public:
    AppSettingsStore();

    std::optional<AppSettings> Load() const;
    bool Save(const AppSettings& settings) const;
    const std::filesystem::path& GetSettingsPath() const { return settingsPath; }

private:
    std::filesystem::path settingsPath;
};

} // namespace icd
