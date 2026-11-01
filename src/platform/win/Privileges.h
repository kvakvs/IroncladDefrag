#pragma once

namespace icd::platform {

    // Enables a privilege the process token holds but has switched off, e.g. SE_BACKUP_NAME: administrators
    // own it, and with it FILE_FLAG_BACKUP_SEMANTICS opens files whose ACL admits only SYSTEM (System Volume
    // Information), which a scanner must still read. Returns false when the token lacks the privilege.
    bool enablePrivilege(const wchar_t* name);

} // namespace icd::platform
