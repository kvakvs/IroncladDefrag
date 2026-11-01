#pragma once

#include "core/job/CancelToken.h"

namespace icd::platform {

    // While alive, Ctrl+C and Ctrl+Break cancel `source` instead of ending the process, so a console command
    // stops cleanly between work items. One at a time.
    class ConsoleCancel {
    public:
        // Installs the handler.
        explicit ConsoleCancel(core::CancelSource& source);
        // Removes it.
        ~ConsoleCancel();
        ConsoleCancel(const ConsoleCancel&) = delete;
        ConsoleCancel& operator=(const ConsoleCancel&) = delete;
    };

    // Switches console output to UTF-8, so non-ASCII names print correctly.
    void useUtf8ConsoleOutput();

} // namespace icd::platform
