#include "platform/win/Console.h"

#include <windows.h>

#include <atomic>

namespace icd::platform {

    namespace {

        // The source the handler cancels; set while a ConsoleCancel lives.
        std::atomic<core::CancelSource*> active{nullptr};

        // Console control handler: cancels instead of letting the default handler end the process.
        BOOL WINAPI onControl(DWORD type) {
            core::CancelSource* source = active.load();
            if ((type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) && source != nullptr) {
                source->cancel();
                return TRUE;
            }
            return FALSE;
        }

    } // namespace

    ConsoleCancel::ConsoleCancel(core::CancelSource& source) {
        active = &source;
        SetConsoleCtrlHandler(onControl, TRUE);
    }

    ConsoleCancel::~ConsoleCancel() {
        SetConsoleCtrlHandler(onControl, FALSE);
        active = nullptr;
    }

    void useUtf8ConsoleOutput() { SetConsoleOutputCP(CP_UTF8); }

} // namespace icd::platform
