#include "../precompiled.h"
#include "AppSettingsStore.h"

#include "../support/Logger.h"

#include <fstream>
#include <sstream>

namespace icd {

namespace {
std::filesystem::path LocalAppDataPath()
{
    std::array<wchar_t, MAX_PATH> buffer{};
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count > 0 && count < buffer.size()) {
        return std::filesystem::path(buffer.data());
    }
    return std::filesystem::temp_directory_path();
}
} // namespace

AppSettingsStore::AppSettingsStore()
{
    settingsPath = LocalAppDataPath() / L"IroncladDefrag" / L"settings.txt";
}

std::optional<AppSettings> AppSettingsStore::Load() const
{
    try {
        if (!std::filesystem::exists(settingsPath)) {
            return std::nullopt;
        }

        std::wifstream input(settingsPath);
        if (!input) {
            Logger::Warning(L"Unable to open app settings file: " + settingsPath.wstring());
            return std::nullopt;
        }

        std::wstringstream text;
        text << input.rdbuf();
        return AppSettingsSerializer().Deserialize(text.str());
    } catch (const std::exception&) {
        Logger::Warning(L"Unable to load app settings; defaults will be used.");
        return std::nullopt;
    }
}

bool AppSettingsStore::Save(const AppSettings& settings) const
{
    try {
        std::filesystem::create_directories(settingsPath.parent_path());
        const std::filesystem::path temporaryPath = settingsPath;
        std::filesystem::path temp = temporaryPath;
        temp += L".tmp";

        {
            std::wofstream output(temp, std::ios::trunc);
            if (!output) {
                Logger::Warning(L"Unable to write app settings file: " + temp.wstring());
                return false;
            }
            output << AppSettingsSerializer().Serialize(settings);
        }

        std::error_code ignored;
        std::filesystem::remove(settingsPath, ignored);
        std::filesystem::rename(temp, settingsPath);
        return true;
    } catch (const std::exception&) {
        Logger::Warning(L"Unable to save app settings.");
        return false;
    }
}

} // namespace icd
