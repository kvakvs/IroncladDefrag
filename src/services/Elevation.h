#pragma once

#include <string>

namespace icd::services {

    // Whether this process has administrator rights. The GUI asks for them in its manifest (plan.md 4), but a start
    // that skipped the prompt (RunAsInvoker, a scripted run) has none: analysis, TRIM and moves then cannot work.
    [[nodiscard]] bool runningElevated();

    // Starts the GUI again with administrator rights (the UAC prompt) and these command line arguments (UTF-8), so a
    // start without them can recover. False when the user declined or the start failed.
    [[nodiscard]] bool restartElevated(const std::string& arguments);

} // namespace icd::services
