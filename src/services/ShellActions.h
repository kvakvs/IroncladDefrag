#pragma once

#include <string>
#include <string_view>

namespace icd::services {

    // The full path of a file from its volume's root (a drive letter or mount folder with its trailing backslash)
    // and its path on the volume as snapshots write it (from the volume root, starting with a backslash).
    [[nodiscard]] std::string volumePath(std::string_view root, std::string_view pathOnVolume);

    // Opens the file's folder in Explorer with the file selected (the map's "Show in Explorer"). False when the file
    // is not there any more (moved or deleted since the analysis) or the shell refused.
    [[nodiscard]] bool showInExplorer(const std::string& path);

} // namespace icd::services
