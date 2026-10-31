#pragma once

#include <string>

namespace icd::platform {

    // Turns a Win32 error code into "<system message> (error N)" so failures can be logged and shown
    // without each caller repeating FormatMessage boilerplate.
    std::wstring formatWinError(unsigned long code);

    // formatWinError(GetLastError()), captured before anything else can overwrite the last error.
    std::wstring lastWinError();

} // namespace icd::platform
