#include "platform/win/ChildProcess.h"

#include "platform/win/Handle.h"
#include "platform/win/TextConvert.h"
#include "platform/win/WinError.h"

#include <windows.h>

#include <array>
#include <stdexcept>

namespace icd::platform {

    namespace {

        // Read end (ours) and write end (the child's stdout/stderr) of the output pipe.
        struct OutputPipe {
            UniqueHandle read; // Not inherited.
            UniqueHandle write; // Inherited by the child, closed by us after it starts.
        };

        // Creates the pipe with only the write end inheritable.
        OutputPipe makePipe() {
            SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
            HANDLE read = nullptr;
            HANDLE write = nullptr;
            if (CreatePipe(&read, &write, &sa, 0) == FALSE) {
                throw std::runtime_error("CreatePipe failed: " + toUtf8(lastWinError()));
            }
            OutputPipe pipe{UniqueHandle(read), UniqueHandle(write)};
            SetHandleInformation(pipe.read.get(), HANDLE_FLAG_INHERIT, 0);
            return pipe;
        }

        // Starts the child with the pipe as stdout and stderr; returns its process handle.
        UniqueHandle startChild(const std::wstring& commandLine, HANDLE output) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = output;
            si.hStdError = output;
            PROCESS_INFORMATION pi{};
            std::wstring mutableLine = commandLine;
            if (CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                               &si, &pi) == FALSE) {
                throw std::runtime_error("cannot start " + toUtf8(commandLine) + ": " + toUtf8(lastWinError()));
            }
            CloseHandle(pi.hThread);
            return UniqueHandle(pi.hProcess);
        }

        // Reads whatever the pipe holds right now; false once the pipe is closed and empty.
        bool drain(HANDLE pipe, const std::function<void(std::string_view)>& onOutput) {
            std::array<char, 4096> buffer{};
            for (;;) {
                DWORD available = 0;
                if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE) {
                    return false; // Broken pipe: the child closed its end.
                }
                if (available == 0) {
                    return true;
                }
                DWORD read = 0;
                if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) == FALSE) {
                    return false;
                }
                onOutput(std::string_view(buffer.data(), read));
            }
        }

    } // namespace

    unsigned long runCaptured(const std::wstring& commandLine, const std::function<void(std::string_view)>& onOutput,
                              const std::function<bool()>& cancelled) {
        OutputPipe pipe = makePipe();
        const UniqueHandle process = startChild(commandLine, pipe.write.get());
        pipe.write.reset(); // Only the child holds the write end now, so the pipe breaks when it exits.
        for (;;) {
            const bool open = drain(pipe.read.get(), onOutput);
            if (cancelled()) {
                TerminateProcess(process.get(), 1);
                WaitForSingleObject(process.get(), INFINITE);
                return 1;
            }
            if (!open) {
                break;
            }
            if (WaitForSingleObject(process.get(), 50) == WAIT_OBJECT_0) {
                drain(pipe.read.get(), onOutput); // Output written just before the exit.
                break;
            }
        }
        WaitForSingleObject(process.get(), INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(process.get(), &code);
        return code;
    }

    std::wstring systemProgram(const std::wstring& fileName) {
        std::array<wchar_t, MAX_PATH> dir{};
        const UINT length = GetSystemDirectoryW(dir.data(), static_cast<UINT>(dir.size()));
        return std::wstring(dir.data(), length) + L"\\" + fileName;
    }

} // namespace icd::platform
