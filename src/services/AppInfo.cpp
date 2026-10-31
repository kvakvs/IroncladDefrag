#include "services/AppInfo.h"

#ifndef ICD_VERSION_STRING
#error "ICD_VERSION_STRING must be defined by the build"
#endif

namespace icd::services {

    std::string_view appName() { return "Ironclad Defrag"; }

    std::string_view appVersion() { return ICD_VERSION_STRING; }

} // namespace icd::services
