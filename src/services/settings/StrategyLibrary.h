#pragma once

#include "core/strategy/ZoneScript.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace icd::services {

    // A strategy the library cannot store, import or export (unreadable file, invalid script, unknown id); the message
    // says why and is shown to the user as is.
    class StrategyLibraryError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Prefix of every custom strategy id, so custom ids never collide with built-in preset ids and fit
    // DriveStrategy::preset beside them.
    inline constexpr std::string_view customStrategyPrefix = "custom-";

    // Whether an id names a custom strategy (by its prefix, whether or not it exists).
    [[nodiscard]] bool isCustomStrategyId(std::string_view id);

    // The id a strategy name gets: the prefix and the name's ASCII letters and digits in lower case, other runs as one
    // "-" ("My photos!" -> "custom-my-photos"); a name without any gives "custom-strategy".
    [[nodiscard]] std::string customStrategyId(std::string_view name);

    // The user's own strategies (plan.md 13 Phase 10): zone scripts saved from the Strategy tab or imported, one JSON
    // file per strategy (the zone script format of data/strategies, so an exported file imports anywhere) in the
    // "strategies" directory beside the settings file. Loaded once; every change is written at once (temporary file and
    // rename). Lookups cover the built-in presets too, so the GUI and the CLI resolve a remembered id the same way. Not
    // thread-safe: the UI thread (or the CLI's main thread) owns it.
    class StrategyLibrary {
    public:
        // Loads every *.json file of directory. A missing directory is an empty library; a file that is not a valid
        // custom strategy is logged and left alone, so one bad file never hides the others.
        explicit StrategyLibrary(std::filesystem::path directory);

        // The directory the strategies are kept in.
        [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

        // The custom strategies, ordered by name.
        [[nodiscard]] const std::vector<core::strategy::ZoneScript>& custom() const { return custom_; }

        // Every strategy a picker offers: the built-in presets in their order, then the custom ones by name. Pointers
        // stay valid until the library changes.
        [[nodiscard]] std::vector<const core::strategy::ZoneScript*> all() const;

        // A built-in or custom strategy by id; null when there is none.
        [[nodiscard]] const core::strategy::ZoneScript* find(std::string_view id) const;

        // Saves script as a custom strategy named name. A custom strategy of the same name is replaced (its id kept);
        // otherwise the script gets a new id from the name. Returns the stored script. Throws StrategyLibraryError
        // when the script is invalid or the file cannot be written (the library is then unchanged).
        const core::strategy::ZoneScript& save(core::strategy::ZoneScript script, const std::string& name);

        // Deletes a custom strategy; false when there is none with that id. Throws StrategyLibraryError when its file
        // cannot be removed.
        bool remove(std::string_view id);

        // Reads a zone script file and adds it as a custom strategy under its own name, or "name (2)" and so on when a
        // custom strategy has that name, so an import never replaces one. Throws StrategyLibraryError with every
        // problem when the file cannot be used.
        const core::strategy::ZoneScript& importFile(const std::filesystem::path& file);

        // Writes a script (built-in, custom or edited) to file in the zone script format. Throws StrategyLibraryError
        // when the file cannot be written.
        static void exportFile(const core::strategy::ZoneScript& script, const std::filesystem::path& file);

    private:
        // Reads the directory into custom_ (see the constructor).
        void load();

        // Reads one file into custom_ unless it is unusable or its id is taken.
        void loadFile(const std::filesystem::path& file);

        // The custom strategy with this name, or null.
        [[nodiscard]] core::strategy::ZoneScript* customNamed(std::string_view name);

        // name, or "name (2)", "name (3)", ... whichever no custom strategy uses yet.
        [[nodiscard]] std::string freeName(const std::string& name);

        // An id from name that no custom strategy uses yet ("custom-x", "custom-x-2", ...).
        [[nodiscard]] std::string freeId(std::string_view name) const;

        // Where a custom strategy's file lives.
        [[nodiscard]] std::filesystem::path fileOf(std::string_view id) const;

        // Keeps custom_ ordered by name (then id), as the picker shows it.
        void sort();

        std::filesystem::path directory_; // Where the files are.
        std::vector<core::strategy::ZoneScript> custom_; // Loaded and saved custom strategies.
    };

} // namespace icd::services
