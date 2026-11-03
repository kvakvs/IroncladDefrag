#include "services/ShellActions.h"

#include "core/log/Log.h"
#include "platform/win/Shell.h"
#include "platform/win/TextConvert.h"

namespace icd::services {

    namespace {

        // Whether a character separates path parts.
        constexpr bool separator(char c) { return c == '\\' || c == '/'; }

    } // namespace

    std::string volumePath(std::string_view root, std::string_view pathOnVolume) {
        while (!root.empty() && separator(root.back())) {
            root.remove_suffix(1);
        }
        while (!pathOnVolume.empty() && separator(pathOnVolume.front())) {
            pathOnVolume.remove_prefix(1);
        }
        std::string out(root);
        out += '\\';
        out += pathOnVolume;
        return out;
    }

    bool showInExplorer(const std::string& path) {
        const bool shown = platform::showInExplorer(platform::fromUtf8(path));
        if (!shown) {
            core::logging::warning("show in explorer failed", {{"path", path}});
        }
        return shown;
    }

} // namespace icd::services
