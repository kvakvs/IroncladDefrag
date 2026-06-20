#include "../precompiled.h"
#include "Logger.h"

#include <iostream>
#include <string>

namespace icd {

namespace {
void WriteLogLine(std::wstring_view level, std::wstring_view message)
{
    std::wstring line;
    line.reserve(level.size() + message.size() + 8);
    line.append(L"[");
    line.append(level);
    line.append(L"] ");
    line.append(message);
    line.append(L"\n");

    OutputDebugStringW(line.c_str());
    std::wcerr << line;
}
} // namespace

void Logger::Info(std::wstring_view message)
{
    WriteLogLine(L"info", message);
}

void Logger::Warning(std::wstring_view message)
{
    WriteLogLine(L"warning", message);
}

void Logger::Error(std::wstring_view message)
{
    WriteLogLine(L"error", message);
}

} // namespace icd
