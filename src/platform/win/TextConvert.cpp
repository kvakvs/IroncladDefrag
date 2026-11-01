#include "platform/win/TextConvert.h"

#include <windows.h>

namespace icd::platform {

    std::string toUtf8(const std::wstring& text) {
        if (text.empty()) {
            return {};
        }
        const int length = static_cast<int>(text.size());
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), length, out.data(), bytes, nullptr, nullptr);
        return out;
    }

    namespace {

        // Multi-byte text in a code page to UTF-16.
        std::wstring toWide(const std::string& text, UINT codePage) {
            if (text.empty()) {
                return {};
            }
            const int length = static_cast<int>(text.size());
            const int chars = MultiByteToWideChar(codePage, 0, text.c_str(), length, nullptr, 0);
            std::wstring out(static_cast<std::size_t>(chars), L'\0');
            MultiByteToWideChar(codePage, 0, text.c_str(), length, out.data(), chars);
            return out;
        }

    } // namespace

    std::wstring fromUtf8(const std::string& text) { return toWide(text, CP_UTF8); }

    std::string oemToUtf8(const std::string& text) { return toUtf8(toWide(text, CP_OEMCP)); }

} // namespace icd::platform
