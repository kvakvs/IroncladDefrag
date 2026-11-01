#pragma once

// The test bed scenarios of plan.md section 12. Each one writes a seeded, reproducible layout under its
// own folder and interleaves writes, deletes and appends so the volume ends up fragmented the way real
// drives of that kind do.

#include "FileOps.h"

#include <span>
#include <string_view>

namespace icd::testvol {

    // Writes one scenario under `dir` (relative to the root) using up to `bytes`; returns the files it left.
    using ScenarioFn = std::vector<FileSpec> (*)(Generator& gen, const std::wstring& dir, std::uint64_t bytes);

    // A named scenario with its default fill level.
    struct Scenario {
        std::wstring_view name; // Command-line name, e.g. L"photos".
        double fill; // Share of the volume used when it is done (default --fill).
        ScenarioFn run; // Generator.
        std::string_view about; // One-line description for --help.
    };

    // All scenarios in plan.md order.
    std::span<const Scenario> scenarios();

    // The scenario with this name, or nullptr.
    const Scenario* findScenario(std::wstring_view name);

} // namespace icd::testvol
