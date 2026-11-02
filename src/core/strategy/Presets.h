#pragma once

#include "core/strategy/ZoneScript.h"

#include <optional>
#include <string_view>
#include <vector>

namespace icd::core::strategy {

    // The built-in strategies (data/strategies/*.json, embedded at build time, plan.md 8.3-8.8), parsed and validated
    // against the built-in catalog once. The default (mixed data drive) comes first, then the storage presets, then
    // the general strategies.
    [[nodiscard]] const std::vector<ZoneScript>& builtinPresets();

    // The index of a built-in preset by id, or nullopt.
    [[nodiscard]] std::optional<std::size_t> builtinPresetIndex(std::string_view id);

    // The JSON texts of the built-in presets in builtinPresets() order (generated sources).
    [[nodiscard]] std::vector<std::string_view> builtinPresetJson();

} // namespace icd::core::strategy
