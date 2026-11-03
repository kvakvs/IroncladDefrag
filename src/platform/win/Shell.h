#pragma once

#include <string>

namespace icd::platform {

    // Opens the folder holding a file in Explorer with the file selected (SHOpenFolderAndSelectItems, which asks the
    // running Explorer, so the window opens with the user's rights even from this elevated process). False when the
    // path does not exist or the shell refused.
    bool showInExplorer(const std::wstring& path);

} // namespace icd::platform
