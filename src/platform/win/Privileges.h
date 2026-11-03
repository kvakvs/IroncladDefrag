#pragma once

#include <string>

namespace icd::platform {

    // Enables a privilege the process token holds but has switched off, e.g. SE_BACKUP_NAME: administrators
    // own it, and with it FILE_FLAG_BACKUP_SEMANTICS opens files whose ACL admits only SYSTEM (System Volume
    // Information), which a scanner must still read. Returns false when the token lacks the privilege.
    bool enablePrivilege(const wchar_t* name);

    // Starts this program again with administrator rights (the UAC prompt) and the given command line arguments, for
    // a process that was started without them (plan.md 4: moves need them). False when the user declined or the start
    // failed; the caller keeps running then.
    bool relaunchElevated(const std::wstring& arguments);

} // namespace icd::platform
