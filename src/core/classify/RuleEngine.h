#pragma once

#include "core/exec/Executor.h"
#include "core/job/CancelToken.h"
#include "core/snapshot/Snapshot.h"
#include "core/snapshot/Types.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icd::core::classify {

    // A file class (plan.md 8.2-8.6): an index into RuleSet::classNames. One byte per file at 20 M files.
    using ClassId = std::uint8_t;

    // The most classes a rule set may name.
    inline constexpr std::size_t maxClasses = std::numeric_limits<ClassId>::max();

    // An invalid rule set (bad pattern, unknown class, too many classes).
    class RuleError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Whether a rule wants files the USN journal shows as churning (plan.md 8.2 activity signals).
    enum class ChurnTest : std::uint8_t {
        Any, // Churn does not matter.
        Churning, // Only files flagged as churn.
        Stable, // Only files not flagged as churn.
    };

    // One classification rule: every condition that is set must hold. Rules are tried in order and the first match
    // gives the class, so specific rules (repository packs) go before general ones (sources).
    struct Rule {
        std::string name; // For diagnostics and the Strategy tab.
        ClassId target = 0; // The class it gives.
        // Path globs, any of which must match the file's path from the volume root: components separated by / or \,
        // `*` and `?` within a component, `**` for any number of components; anchored at the root unless they start
        // with `**`. ASCII letters compare case-insensitively, as NTFS names do. Empty: any path.
        std::vector<std::string> paths;
        std::vector<std::string> extensions; // Lower-case extensions without the dot, any of which must match.
        std::uint64_t minSize = 0; // Smallest size in bytes (inclusive).
        std::uint64_t maxSize = std::numeric_limits<std::uint64_t>::max(); // Largest size in bytes (exclusive).
        std::uint32_t minAgeDays = 0; // Unmodified for at least this many days.
        std::uint32_t maxAgeDays = std::numeric_limits<std::uint32_t>::max(); // Modified less than this many days ago.
        std::uint16_t flagsAll = 0; // FileFlag bits that must all be set (e.g. FileDirectory).
        std::uint16_t flagsNone = 0; // FileFlag bits that must all be clear.
        ChurnTest churn = ChurnTest::Any; // Churn condition.
    };

    // A user's class for everything below a directory (plan.md 8.2: users can override per directory). It beats the
    // rules for regular files below it; the deepest override wins. Directories themselves keep their rule class, since
    // directory indexes are placed by their own rules (plan.md 8.3).
    struct DirectoryOverride {
        std::string path; // Directory path from the volume root ("\Photos\2024" or "Photos/2024").
        ClassId target = 0; // The class it gives.
    };

    // Classes, ordered rules, the class of files no rule matches, and per-directory overrides.
    struct RuleSet {
        std::vector<std::string> classNames; // Class names; ClassId indexes them.
        std::vector<Rule> rules; // Tried in order.
        ClassId fallback = 0; // Class when no rule matches ("Unknown").
        std::vector<DirectoryOverride> overrides; // User overrides.
    };

    // What classification needs besides the snapshot.
    struct ClassifyInputs {
        std::int64_t now = 0; // FILETIME the ages are measured from (fixed, so results are reproducible).
        std::span<const std::uint8_t> churn; // Per file: non-zero when the file churns; empty when unknown.
    };

    // The facts of one file, for classifying a path without a snapshot (tests, previews of a rule set).
    struct FileFacts {
        std::uint64_t sizeBytes = 0; // Logical size.
        std::int64_t modified = 0; // FILETIME of the last change.
        std::uint16_t flags = 0; // FileFlag bits.
        bool churn = false; // Flagged as churn.
    };

    // Compiled rules. Paths are never built: path globs run as one combined automaton over path components, its state
    // computed once per directory from the root down and stepped once more per file name, so a 20 M-file snapshot is
    // classified in one parallel pass over the files (plan.md 6.2 principles 3 and 8).
    class RuleEngine {
    public:
        // Compiles a rule set. Throws RuleError for bad patterns, unknown classes or too many classes.
        explicit RuleEngine(RuleSet rules);
        ~RuleEngine();
        RuleEngine(RuleEngine&&) noexcept;
        RuleEngine& operator=(RuleEngine&&) noexcept;
        RuleEngine(const RuleEngine&) = delete;
        RuleEngine& operator=(const RuleEngine&) = delete;

        // The rule set as given.
        [[nodiscard]] const RuleSet& rules() const;

        // The class of every file of a snapshot, by FileIndex. The same for any executor.
        [[nodiscard]] std::vector<ClassId> classify(const Snapshot& snapshot, const ClassifyInputs& inputs,
                                                    Executor& executor, const CancelToken& token = {}) const;

        // The class of one file given by its path from the volume root ("\src\main.cpp") and facts; equal to what
        // classify() gives that file in a snapshot.
        [[nodiscard]] ClassId classifyPath(std::string_view path, const FileFacts& facts, std::int64_t now) const;

        struct Compiled; // The automaton, extension table and per-rule tests (RuleEngine.cpp).

    private:
        std::unique_ptr<Compiled> compiled_; // Never null except after a move.
    };

    // The lower-case ASCII extension of a file name without the dot ("tar.gz" gives "gz"; "Makefile" gives ""); a
    // leading dot alone does not start one (".gitignore" gives "").
    [[nodiscard]] std::string extensionOf(std::string_view name);

} // namespace icd::core::classify
