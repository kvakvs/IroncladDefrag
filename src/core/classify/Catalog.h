#pragma once

#include "core/classify/RuleEngine.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icd::core::classify {

    // A catalog file that cannot be used (bad JSON, unknown class or flag, bad rule); the message says what and where.
    class CatalogError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // One file class as the catalog describes it.
    struct ClassInfo {
        std::string id; // Stable identifier used by rules, zone scripts and settings ("photos").
        std::string name; // Display name ("Photos").
        std::uint32_t color = 0; // 0xRRGGBB for the class color mode and the legend.
        std::string description; // One line for tooltips and the Strategy tab.
    };

    // File classes and their rules as data (plan.md 8.2-8.6), so presets and user edits share one engine.
    struct Catalog {
        std::vector<ClassInfo> classes; // ClassId indexes them.
        RuleSet rules; // classNames are the classes' display names.

        // The id of a class by its identifier, or nullopt.
        [[nodiscard]] std::optional<ClassId> find(std::string_view id) const;
    };

    // Reads a catalog from JSON (data/classes.json is the format's reference). Throws CatalogError.
    [[nodiscard]] Catalog parseCatalog(std::string_view json);

    // The built-in catalog: data/classes.json, embedded at build time and parsed once.
    [[nodiscard]] const Catalog& builtinCatalog();

    // The text of data/classes.json as built into the program (generated source).
    [[nodiscard]] std::string_view builtinCatalogJson();

} // namespace icd::core::classify
