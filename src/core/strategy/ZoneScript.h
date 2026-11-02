#pragma once

#include "core/classify/Catalog.h"
#include "core/classify/RuleEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icd::core::strategy {

    // How far the planner goes towards a strategy's target (plan.md 9.2). Core's own copy, so the planner needs no
    // services types; services maps its EffortLevel onto it one to one.
    enum class Effort : std::uint8_t {
        Fast, // Biggest gain in the shortest time: hot spots only, small budget.
        Moderate, // All zones, misplaced files above a benefit threshold, medium budget.
        Maximum, // The complete target layout, no budget.
    };

    // The three levels, for loops over them.
    inline constexpr std::array<Effort, 3> allEfforts{Effort::Fast, Effort::Moderate, Effort::Maximum};

    // Stable name of an effort level in zone scripts and reports: "fast", "moderate", "maximum".
    [[nodiscard]] std::string_view effortKey(Effort effort);

    // Where a zone sits on the volume (plan.md 8.1).
    enum class Anchor : std::uint8_t {
        Front, // Starts at the beginning of the volume (outer, fast tracks); only the first placed zone.
        AfterPrevious, // Follows the previous placed zone (and its gap).
        Back, // Packed against the end of the volume; zones after a back zone are back zones too.
    };

    // The order files keep inside a zone (plan.md 8.1). Every order has a fixed direction, so "position increases"
    // means the same thing in every zone.
    enum class ZoneOrder : std::uint8_t {
        None, // No order; only packing.
        DirectoryName, // By directory path, then name: a folder reads as one sequential run.
        Name, // By file name only.
        Size, // Smallest first.
        Age, // Newest first, oldest at the end (modification time, or creation time when that is older).
        Modified, // Most recently modified first.
        Created, // Most recently created first.
        Accessed, // Most recently accessed first (access-time sort; only meaningful when the volume tracks it).
    };

    // What the planner does with a zone's files (plan.md 8.1).
    enum class ZoneAction : std::uint8_t {
        KeepInPlace, // Never move (churning files); they are obstacles for every other zone.
        DefragmentOnly, // Join fragments near where the file is; never relocate it to another zone.
        FastFill, // Compact the zone: files from its tail into holes near its head; order ignored.
        Sort, // Bring the zone into its order, moving as few bytes as possible.
        PackToBack, // Pack contiguously so the zone ends at the end of the volume.
    };

    // Which files a zone takes. Every condition that is set must hold; a selector with no condition takes every file
    // (a catch-all "rest" zone). Files go to the first zone whose selector matches.
    struct ZoneSelector {
        std::vector<std::string> classes{}; // Catalog class ids, any of which must match; empty: any class.
        std::vector<std::string> paths{}; // Path globs as in the class catalog, any of which must match; empty: any.
        std::vector<std::string> extensions{}; // Lower-case extensions without the dot, any of which must match.
        std::uint64_t minSize = 0; // Smallest size in bytes (inclusive).
        std::uint64_t maxSize = std::numeric_limits<std::uint64_t>::max(); // Largest size in bytes (exclusive).
        std::uint32_t minAgeDays = 0; // Unmodified for at least this many days.
        std::uint32_t maxAgeDays = std::numeric_limits<std::uint32_t>::max(); // Modified less than this long ago.
        std::uint16_t flagsAll = 0; // FileFlag bits that must all be set.
        std::uint16_t flagsNone = 0; // FileFlag bits that must all be clear.
        classify::ChurnTest churn = classify::ChurnTest::Any; // Churn condition.

        // Selectors compare as values (round-trip tests, edited-script detection).
        friend bool operator==(const ZoneSelector&, const ZoneSelector&) = default;
    };

    // Free space kept right after a zone (plan.md 8.1 gap_after, 8.3 churn gap). The size is the largest of the terms
    // that are set; all zero means no gap. The churn gap "max(5% of volume, 2 x build output + temp)" is
    // {volumePercent 5, classMultiple 2, classes [build, temp]}.
    struct GapSpec {
        double freePercent = 0.0; // Share of the free space left after the plan, in percent.
        double volumePercent = 0.0; // Share of the whole volume, in percent.
        std::uint64_t bytes = 0; // Absolute size.
        double classMultiple = 0.0; // Multiple of the bytes the listed classes hold.
        std::vector<std::string> classes{}; // Class ids classMultiple counts.

        // Whether any term is set.
        [[nodiscard]] bool empty() const {
            return freePercent <= 0.0 && volumePercent <= 0.0 && bytes == 0 && classMultiple <= 0.0;
        }

        // Gaps compare as values.
        friend bool operator==(const GapSpec&, const GapSpec&) = default;
    };

    // One zone of a strategy (plan.md 8.1).
    struct Zone {
        std::string name{}; // Shown in the Strategy tab and in plan reports.
        ZoneSelector select{}; // Which files belong here.
        Anchor anchor = Anchor::AfterPrevious; // Where it sits.
        ZoneOrder order = ZoneOrder::None; // Order inside the zone.
        // Per-level override of order, indexed by Effort (video: none at Fast and Moderate, age at Maximum).
        std::array<std::optional<ZoneOrder>, 3> orderByLevel{};
        ZoneAction action = ZoneAction::FastFill; // What the planner does.
        // Smallest fragment accepted as good enough; nullopt means the whole file must be one fragment.
        std::optional<std::uint64_t> fragmentOk = defaultFragmentOk;
        GapSpec gapAfter{}; // Free space kept after the zone.

        // The default "good enough" fragment size (plan.md 3, fact 2).
        static constexpr std::uint64_t defaultFragmentOk = 64ULL << 20U;

        // The order that applies at an effort level.
        [[nodiscard]] ZoneOrder orderAt(Effort effort) const;

        // Whether the zone has a place on the volume (fast-fill, sort and pack-to-back zones do; the others leave
        // their files where they are).
        [[nodiscard]] bool placed() const;

        // Zones compare as values.
        friend bool operator==(const Zone&, const Zone&) = default;
    };

    // A strategy as data (plan.md 8.1): zones in disk order. Presets and user edits share it, so one engine plans both.
    struct ZoneScript {
        std::string id{}; // Stable key in settings ("mixed"), so renaming keeps remembered choices.
        std::string name{}; // Shown in the preset picker.
        std::string description{}; // One paragraph for the Strategy tab.
        std::vector<Zone> zones{}; // In disk order: front zones first, back zones last.

        // Scripts compare as values.
        friend bool operator==(const ZoneScript&, const ZoneScript&) = default;
    };

    // One problem found in a zone script.
    struct ScriptIssue {
        std::optional<std::size_t> zone{}; // Zone index, or nullopt for the script as a whole.
        std::string message{}; // What is wrong, for the Strategy tab and error messages.
    };

    // A zone script that cannot be used; what() lists every issue, one per line.
    class ZoneScriptError : public std::runtime_error {
    public:
        // Keeps the issues and builds the message from them.
        explicit ZoneScriptError(std::vector<ScriptIssue> issues);

        // The issues, in script order.
        [[nodiscard]] const std::vector<ScriptIssue>& issues() const { return issues_; }

    private:
        std::vector<ScriptIssue> issues_{}; // What is wrong.
    };

    // Every problem of a script against a catalog: unknown classes, bad globs, conflicting anchors and actions, empty
    // or duplicate names, bad ranges. Empty when the script can be planned.
    [[nodiscard]] std::vector<ScriptIssue> validateScript(const ZoneScript& script, const classify::Catalog& catalog);

    // Reads a zone script from JSON and validates it. Throws ZoneScriptError with every issue found (unknown keys and
    // values included).
    [[nodiscard]] ZoneScript parseZoneScript(std::string_view json, const classify::Catalog& catalog);

    // Writes a script as JSON that parseZoneScript() reads back to an equal script; defaults are left out.
    [[nodiscard]] std::string zoneScriptJson(const ZoneScript& script);

    // Text of an issue: "zone 2 \"Photos\": unknown class \"fotos\"".
    [[nodiscard]] std::string describeIssue(const ScriptIssue& issue, const ZoneScript& script);

    // Script names of the enums (as JSON writes them).
    [[nodiscard]] std::string_view anchorKey(Anchor anchor); // "front", "after-previous", "back".
    [[nodiscard]] std::string_view orderKey(ZoneOrder order); // "none", "directory+name", ...
    [[nodiscard]] std::string_view actionKey(ZoneAction action); // "keep-in-place", "fast-fill", ...

} // namespace icd::core::strategy
