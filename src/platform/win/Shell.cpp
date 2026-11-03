#include "platform/win/Shell.h"

#include <windows.h>

#include <objbase.h>
#include <shlobj.h>

namespace icd::platform {

    namespace {

        // Initializes COM on this thread for the lifetime of the object, when it was not already.
        class ComScope {
        public:
            // Joins the thread's apartment (the UI thread usually has one already).
            ComScope() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}

            // Leaves it again if this object joined it.
            ~ComScope() {
                if (SUCCEEDED(result_)) {
                    CoUninitialize();
                }
            }

            ComScope(const ComScope&) = delete;
            ComScope& operator=(const ComScope&) = delete;
            ComScope(ComScope&&) = delete;
            ComScope& operator=(ComScope&&) = delete;

        private:
            HRESULT result_; // S_OK or S_FALSE: balanced by CoUninitialize; another mode: nothing to undo.
        };

    } // namespace

    bool showInExplorer(const std::wstring& path) {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return false;
        }
        const ComScope com;
        PIDLIST_ABSOLUTE item = ILCreateFromPathW(path.c_str());
        if (item == nullptr) {
            return false;
        }
        const HRESULT shown = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
        ILFree(item);
        return SUCCEEDED(shown);
    }

} // namespace icd::platform
