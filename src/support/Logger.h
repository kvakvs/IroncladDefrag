#pragma once

#include <string_view>

namespace icd {

class Logger {
public:
    static void Info(std::wstring_view message);
    static void Warning(std::wstring_view message);
    static void Error(std::wstring_view message);
};

} // namespace icd
