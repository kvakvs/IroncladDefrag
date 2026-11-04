#pragma once

#include <windows.h>

#include <oleauto.h>

namespace icd::platform {

    // Initializes COM (multithreaded) on this thread for the scope of one system query (WMI, Task Scheduler); a thread
    // already in another apartment keeps it, since both services work from either.
    class ComScope {
    public:
        ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
        ~ComScope() {
            if (SUCCEEDED(result_)) {
                CoUninitialize();
            }
        }

    private:
        HRESULT result_; // S_OK or S_FALSE: balanced by CoUninitialize; another mode: nothing to undo.
    };

    // Owns a BSTR for one COM call (or one returned by it, through out()).
    class Bstr {
    public:
        Bstr() = default;
        explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {}
        Bstr(const Bstr&) = delete;
        Bstr& operator=(const Bstr&) = delete;
        ~Bstr() { SysFreeString(value_); }

        // The string for the call (still owned).
        [[nodiscard]] BSTR get() const { return value_; }

        // Where a call returns a string this object then owns (any previous one is freed).
        [[nodiscard]] BSTR* out() {
            SysFreeString(value_);
            value_ = nullptr;
            return &value_;
        }

        // The text, empty for a null BSTR.
        [[nodiscard]] const wchar_t* text() const { return value_ != nullptr ? value_ : L""; }

    private:
        BSTR value_ = nullptr; // Allocated by SysAllocString or by the callee.
    };

    // Owns a VARIANT for one COM call (read from WMI objects, or passed as an empty optional argument).
    struct Variant {
        VARIANT value{}; // Filled by the callee; VT_EMPTY until then.
        Variant() { VariantInit(&value); }
        Variant(const Variant&) = delete;
        Variant& operator=(const Variant&) = delete;
        ~Variant() { VariantClear(&value); }
    };

} // namespace icd::platform
