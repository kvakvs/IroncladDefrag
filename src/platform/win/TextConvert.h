#pragma once

#include <string>

namespace icd::platform {

    // UTF-16 (Windows API) to UTF-8 (core model); invalid sequences become U+FFFD.
    std::string toUtf8(const std::wstring& text);

    // UTF-8 (core model) to UTF-16 (Windows API).
    std::wstring fromUtf8(const std::string& text);

    // Console output of system tools (OEM code page, as defrag.exe writes into a pipe) to UTF-8.
    std::string oemToUtf8(const std::string& text);

} // namespace icd::platform
