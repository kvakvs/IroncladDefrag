#pragma once

#include <string_view>

namespace icd::services {

    // Product name and version shared by the GUI and the CLI, so both report the same identity.
    std::string_view appName();
    std::string_view appVersion();

} // namespace icd::services
