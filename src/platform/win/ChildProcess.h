#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace icd::platform {

    // Runs a console program without a window, with stdout and stderr on one pipe, and hands each chunk of
    // output to `onOutput` as it arrives (on the calling thread). `cancelled` is polled about every 50 ms;
    // when it returns true the process is terminated and runCaptured() returns early. Returns the exit code
    // (1 after a termination). Throws std::runtime_error when the program cannot be started.
    unsigned long runCaptured(const std::wstring& commandLine, const std::function<void(std::string_view)>& onOutput,
                              const std::function<bool()>& cancelled);

    // Full path of a program in the System32 directory, so a copy elsewhere on PATH is never picked up.
    std::wstring systemProgram(const std::wstring& fileName);

} // namespace icd::platform
