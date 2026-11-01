#pragma once

#include <string>

namespace icd::platform {

    // UTF-16 (Windows API) to UTF-8 (core model); invalid sequences become U+FFFD.
    std::string toUtf8(const std::wstring& text);

    // UTF-8 (core model) to UTF-16 (Windows API).
    std::wstring fromUtf8(const std::string& text);

} // namespace icd::platform
