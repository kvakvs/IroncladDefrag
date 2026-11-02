#include "platform/win/ShadowCopies.h"

#include "platform/win/ChildProcess.h"
#include "platform/win/TextConvert.h"

#include <algorithm>
#include <exception>

namespace icd::platform {

    namespace {

        // ASCII lower case, for matching GUID paths in any letter case.
        std::string lowered(std::string_view text) {
            std::string out(text);
            std::ranges::transform(out, out.begin(),
                                   [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
            return out;
        }

    } // namespace

    std::size_t countShadowsInListing(ShadowListing listing, std::string_view volumeGuidPath) {
        if (volumeGuidPath.empty()) {
            return 0;
        }
        const std::string text = lowered(listing.text);
        const std::string needle = lowered(volumeGuidPath);
        std::size_t count = 0;
        for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
            ++count;
        }
        return count;
    }

    std::optional<std::size_t> countShadowCopies(const std::wstring& volumeGuidPath,
                                                 const std::function<bool()>& cancelled) {
        std::string output;
        try {
            const std::wstring command = L"\"" + systemProgram(L"vssadmin.exe") + L"\" list shadows";
            const unsigned long code = runCaptured(
                command, [&](std::string_view chunk) { output.append(chunk); },
                cancelled ? cancelled : std::function<bool()>([] { return false; }));
            // vssadmin exits with 1 when there are no shadow copies at all; anything that lists nothing counts 0.
            if (code > 1) {
                return std::nullopt;
            }
        }
        catch (const std::exception&) {
            return std::nullopt;
        }
        const std::string text = oemToUtf8(output);
        return countShadowsInListing({.text = text}, toUtf8(volumeGuidPath));
    }

} // namespace icd::platform
