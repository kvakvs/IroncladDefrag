#include "platform/win/Privileges.h"

#include "platform/win/Handle.h"

#include <windows.h>

#include <shellapi.h>

#include <vector>

namespace icd::platform {

    bool enablePrivilege(const wchar_t* name) {
        HANDLE raw = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw) == FALSE) {
            return false;
        }
        const UniqueHandle token(raw);
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(nullptr, name, &privileges.Privileges[0].Luid) == FALSE) {
            return false;
        }
        // AdjustTokenPrivileges succeeds even when the token lacks the privilege; the last error tells.
        return AdjustTokenPrivileges(token.get(), FALSE, &privileges, sizeof(privileges), nullptr, nullptr) != FALSE &&
            GetLastError() == ERROR_SUCCESS;
    }

    bool relaunchElevated(const std::wstring& arguments) {
        std::vector<wchar_t> path(MAX_PATH);
        for (;;) {
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0) {
                return false;
            }
            if (length < path.size()) {
                break;
            }
            path.resize(path.size() * 2); // Truncated: long paths are allowed.
        }
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"runas";
        info.lpFile = path.data();
        info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
        info.nShow = SW_SHOWNORMAL;
        return ShellExecuteExW(&info) != FALSE;
    }

} // namespace icd::platform
