#pragma once

#include <filesystem>
#include <string_view>

namespace icd::services {

    // Sets up the global logger for one executable (file under %LOCALAPPDATA% plus debugger output) and
    // writes the startup line, so the GUI and the CLI log the same way.
    // Returns the log file path, or an empty path when the file could not be opened (debugger output only).
    std::filesystem::path initAppLogging(std::string_view executableName);

    // Writes the shutdown line and flushes every sink; call once before the process exits.
    void shutdownAppLogging();

} // namespace icd::services
