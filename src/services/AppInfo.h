#pragma once

#include <string_view>

namespace icd::services {

    // Product name shown in titles and reports, defined once so the GUI and the CLI agree.
    std::string_view appName();

    // Product version ("major.minor.patch") from project(VERSION), for titles, --version and logs.
    std::string_view appVersion();

} // namespace icd::services
