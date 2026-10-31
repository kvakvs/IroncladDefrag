#include "platform/win/WinError.h"

#include <windows.h>

namespace icd::platform {

    std::wstring formatWinError(unsigned long code) {
        wchar_t* buffer = nullptr;
        const DWORD length =
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
        std::wstring message;
        if (length != 0 && buffer != nullptr) {
            message.assign(buffer, length);
            LocalFree(buffer);
            while (!message.empty() &&
                   (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' ||
                    message.back() == L'.')) {
                message.pop_back();
            }
        }
        if (message.empty()) {
            message = L"Unknown error";
        }
        return message + L" (error " + std::to_wstring(code) + L")";
    }

    std::wstring lastWinError() { return formatWinError(GetLastError()); }

} // namespace icd::platform
