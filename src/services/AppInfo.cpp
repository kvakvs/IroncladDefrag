#include "services/AppInfo.h"

#include "core/BuildCommit.h"
#include "core/Version.h"

#include <format>

namespace icd::services {

    std::string_view appName() { return "Ironclad Defrag"; }

    std::string_view appVersion() { return ICD_VERSION_STRING; }

    std::string_view buildCommit() { return ICD_BUILD_COMMIT; }

    std::string versionWithCommit() {
        const std::string_view commit = buildCommit();
        return commit.empty() ? std::string(appVersion()) : std::format("{} ({})", appVersion(), commit);
    }

    namespace {

        // Compiler name and version of this translation unit.
        std::string compilerDescription() {
#if defined(__clang__)
            return std::format("clang-cl {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
            return std::format("MSVC {}", _MSC_FULL_VER);
#else
            return "unknown compiler";
#endif
        }

        // Build configuration, from NDEBUG.
        constexpr std::string_view configuration() {
#ifdef NDEBUG
            return "Release";
#else
            return "Debug";
#endif
        }

    } // namespace

    std::string buildDescription() { return std::format("{}, {}", configuration(), compilerDescription()); }

} // namespace icd::services
