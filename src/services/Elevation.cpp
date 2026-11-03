#include "services/Elevation.h"

#include "core/log/Log.h"
#include "platform/win/Privileges.h"
#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"

namespace icd::services {

    bool runningElevated() { return platform::isElevated(); }

    bool restartElevated(const std::string& arguments) {
        const bool started = platform::relaunchElevated(platform::fromUtf8(arguments));
        core::logging::info("restart with administrator rights", {{"arguments", arguments}, {"started", started}});
        return started;
    }

} // namespace icd::services
