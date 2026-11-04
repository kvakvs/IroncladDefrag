#include "services/settings/StrategyLibrary.h"

#include "core/classify/Catalog.h"
#include "core/log/Log.h"
#include "core/strategy/Presets.h"
#include "services/settings/SettingsStore.h"

#include <algorithm>
#include <format>
#include <system_error>
#include <tuple>

namespace icd::services {

    namespace {

        // Every issue of a script on its own line, for an error message.
        std::string issuesText(const std::vector<core::strategy::ScriptIssue>& issues,
                               const core::strategy::ZoneScript& script) {
            std::string text;
            for (const auto& issue : issues) {
                text += (text.empty() ? "" : "\n") + core::strategy::describeIssue(issue, script);
            }
            return text;
        }

        // Whether c is kept in an id: ASCII letters and digits.
        bool idCharacter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

        // Lower case of an ASCII letter; other characters unchanged.
        char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    } // namespace

    bool isCustomStrategyId(std::string_view id) { return id.starts_with(customStrategyPrefix); }

    std::string customStrategyId(std::string_view name) {
        std::string slug;
        bool separate = false; // A run of other characters is waiting to become one "-".
        for (const char c : name) {
            if (!idCharacter(c)) {
                separate = !slug.empty();
                continue;
            }
            if (separate) {
                slug += '-';
                separate = false;
            }
            slug += lower(c);
        }
        return std::string(customStrategyPrefix) + (slug.empty() ? std::string("strategy") : slug);
    }

    StrategyLibrary::StrategyLibrary(std::filesystem::path directory) : directory_(std::move(directory)) { load(); }

    void StrategyLibrary::load() {
        std::error_code error;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(directory_, error), end; !error && it != end; it.increment(error)) {
            if (it->path().extension() == ".json") {
                files.push_back(it->path());
            }
        }
        std::ranges::sort(files); // The same files load the same way on every start.
        for (const auto& file : files) {
            loadFile(file);
        }
        sort();
    }

    void StrategyLibrary::loadFile(const std::filesystem::path& file) {
        const std::optional<std::string> text = readTextFile(file);
        try {
            core::strategy::ZoneScript script =
                core::strategy::parseZoneScript(text.value_or(std::string()), core::classify::builtinCatalog());
            if (!isCustomStrategyId(script.id) || find(script.id) != nullptr) {
                throw StrategyLibraryError(std::format("id \"{}\" is not a free custom strategy id", script.id));
            }
            custom_.push_back(std::move(script));
        }
        catch (const std::exception& error) {
            core::logging::warning("custom strategy skipped", {{"path", file.string()}, {"reason", error.what()}});
        }
    }

    std::vector<const core::strategy::ZoneScript*> StrategyLibrary::all() const {
        std::vector<const core::strategy::ZoneScript*> list;
        for (const auto& preset : core::strategy::builtinPresets()) {
            list.push_back(&preset);
        }
        for (const auto& script : custom_) {
            list.push_back(&script);
        }
        return list;
    }

    const core::strategy::ZoneScript* StrategyLibrary::find(std::string_view id) const {
        if (const auto index = core::strategy::builtinPresetIndex(id); index) {
            return &core::strategy::builtinPresets().at(*index);
        }
        const auto found = std::ranges::find(custom_, id, &core::strategy::ZoneScript::id);
        return found == custom_.end() ? nullptr : &*found;
    }

    const core::strategy::ZoneScript& StrategyLibrary::save(core::strategy::ZoneScript script,
                                                            const std::string& name) {
        script.name = name;
        const core::strategy::ZoneScript* existing = customNamed(name);
        script.id = existing != nullptr ? existing->id : freeId(name);
        if (const auto issues = core::strategy::validateScript(script, core::classify::builtinCatalog());
            !issues.empty()) {
            throw StrategyLibraryError("The strategy has problems:\n" + issuesText(issues, script));
        }
        try {
            writeFileAtomically(fileOf(script.id), core::strategy::zoneScriptJson(script));
        }
        catch (const std::exception& error) {
            throw StrategyLibraryError(std::format("Cannot save the strategy: {}", error.what()));
        }
        const std::string id = script.id;
        if (core::strategy::ZoneScript* old = customNamed(name); old != nullptr) {
            *old = std::move(script);
        }
        else {
            custom_.push_back(std::move(script));
        }
        sort();
        return *find(id);
    }

    bool StrategyLibrary::remove(std::string_view id) {
        const auto found = std::ranges::find(custom_, id, &core::strategy::ZoneScript::id);
        if (found == custom_.end()) {
            return false;
        }
        std::error_code error;
        std::filesystem::remove(fileOf(id), error);
        if (error) {
            throw StrategyLibraryError(std::format("Cannot delete the strategy file: {}", error.message()));
        }
        custom_.erase(found);
        return true;
    }

    const core::strategy::ZoneScript& StrategyLibrary::importFile(const std::filesystem::path& file) {
        const std::optional<std::string> text = readTextFile(file);
        if (!text) {
            throw StrategyLibraryError(std::format("Cannot read {}", file.string()));
        }
        try {
            core::strategy::ZoneScript script =
                core::strategy::parseZoneScript(*text, core::classify::builtinCatalog());
            const std::string name = freeName(script.name);
            return save(std::move(script), name);
        }
        catch (const core::strategy::ZoneScriptError& error) {
            throw StrategyLibraryError(
                std::format("{} is not a usable zone script:\n{}", file.filename().string(), error.what()));
        }
    }

    void StrategyLibrary::exportFile(const core::strategy::ZoneScript& script, const std::filesystem::path& file) {
        try {
            writeFileAtomically(file, core::strategy::zoneScriptJson(script));
        }
        catch (const std::exception& error) {
            throw StrategyLibraryError(std::format("Cannot export the strategy: {}", error.what()));
        }
    }

    core::strategy::ZoneScript* StrategyLibrary::customNamed(std::string_view name) {
        const auto found = std::ranges::find(custom_, name, &core::strategy::ZoneScript::name);
        return found == custom_.end() ? nullptr : &*found;
    }

    std::string StrategyLibrary::freeName(const std::string& name) {
        std::string free = name;
        for (int suffix = 2; customNamed(free) != nullptr; ++suffix) {
            free = std::format("{} ({})", name, suffix);
        }
        return free;
    }

    std::string StrategyLibrary::freeId(std::string_view name) const {
        const std::string base = customStrategyId(name);
        std::string id = base;
        for (int suffix = 2; find(id) != nullptr; ++suffix) {
            id = std::format("{}-{}", base, suffix);
        }
        return id;
    }

    std::filesystem::path StrategyLibrary::fileOf(std::string_view id) const {
        return directory_ / std::filesystem::path(std::string(id) + ".json");
    }

    void StrategyLibrary::sort() {
        std::ranges::sort(custom_, {}, [](const core::strategy::ZoneScript& s) { return std::tie(s.name, s.id); });
    }

} // namespace icd::services
