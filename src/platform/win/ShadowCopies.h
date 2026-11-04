#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace icd::platform {

    // Shadow copies of a volume (plan.md 9.4), from Windows' own listing (`vssadmin list shadows`, administrator
    // rights). Each listed copy names its original volume by GUID path, which does not depend on the display language,
    // so counting that path counts the volume's copies. nullopt when the listing cannot be run or fails.
    [[nodiscard]] std::optional<std::size_t> countShadowCopies(const std::wstring& volumeGuidPath,
                                                               const std::function<bool()>& cancelled = {});

    // The whole `vssadmin list shadows` output (UTF-8), so a drive list asks once for all volumes; nullopt when it
    // cannot be run or fails (no administrator rights).
    [[nodiscard]] std::optional<std::string> listShadowCopies(const std::function<bool()>& cancelled = {});

    // The text `vssadmin list shadows` printed.
    struct ShadowListing {
        std::string_view text; // The whole output, UTF-8.
    };

    // Shadow copies of a volume in a `vssadmin list shadows` output: occurrences of its GUID path
    // ("\\?\Volume{...}\"), case-insensitive. Kept apart for tests.
    [[nodiscard]] std::size_t countShadowsInListing(ShadowListing listing, std::string_view volumeGuidPath);

} // namespace icd::platform
