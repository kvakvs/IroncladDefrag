#include "platform/win/Privileges.h"

#include "platform/win/Handle.h"

#include <windows.h>

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

} // namespace icd::platform
