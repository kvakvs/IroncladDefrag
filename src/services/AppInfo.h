#pragma once

#include <string>
#include <string_view>

namespace icd::services {

    // Product name shown in titles and reports, defined once so the GUI and the CLI agree.
    std::string_view appName();

    // Product version ("major.minor.patch") from project(VERSION), for titles, --version and logs.
    std::string_view appVersion();

    // Short git hash of the build's source tree, with "-dirty" when it had uncommitted changes; empty when unknown.
    // Build metadata only (About box, log start line, --version), never part of the version itself.
    std::string_view buildCommit();

    // The version with the commit for places that show build metadata: "1.0.0 (abc1234)", or "1.0.0" alone.
    std::string versionWithCommit();

    // How this binary was built ("Release, clang-cl 23.1.2"), for the About box and bug reports.
    std::string buildDescription();

} // namespace icd::services
