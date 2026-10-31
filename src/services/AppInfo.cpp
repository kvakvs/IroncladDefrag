#include "services/AppInfo.h"

#include "core/Version.h"

namespace icd::services {

    std::string_view appName() { return "Ironclad Defrag"; }

    std::string_view appVersion() { return ICD_VERSION_STRING; }

} // namespace icd::services
