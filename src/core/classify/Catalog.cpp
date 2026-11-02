#include "core/classify/Catalog.h"

#include "core/format/JsonReader.h"
#include "core/snapshot/FileTable.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <initializer_list>
#include <limits>
#include <utility>

namespace icd::core::classify {

    namespace {

        // The catalog format this code reads.
        constexpr std::uint64_t catalogVersion = 1;

        // File flag names as catalog rules write them.
        constexpr std::array<std::pair<std::string_view, std::uint16_t>, 10> flagNames{{
            {"directory", FileDirectory},
            {"resident", FileResident},
            {"compressed", FileCompressed},
            {"sparse", FileSparse},
            {"encrypted", FileEncrypted},
            {"reparse-point", FileReparsePoint},
            {"metadata", FileMetadata},
            {"hard-linked", FileHardLinked},
            {"alternate-streams", FileAlternateStreams},
            {"unmovable", FileUnmovable},
        }};

        // Binary size units.
        constexpr std::array<std::pair<std::string_view, std::uint64_t>, 5> sizeUnits{{
            {"B", 1},
            {"KB", 1ULL << 10U},
            {"MB", 1ULL << 20U},
            {"GB", 1ULL << 30U},
            {"TB", 1ULL << 40U},
        }};

        // The bits of a list of flag names.
        std::uint16_t flagsOf(const JsonValue* list) {
            std::uint16_t flags = 0;
            for (const JsonValue& name : list == nullptr ? std::vector<JsonValue>{} : list->asArray()) {
                const auto it =
                    std::ranges::find(flagNames, name.asString(), &std::pair<std::string_view, std::uint16_t>::first);
                if (it == flagNames.end()) {
                    throw CatalogError(std::format("unknown file flag \"{}\"", name.asString()));
                }
                flags = static_cast<std::uint16_t>(flags | it->second);
            }
            return flags;
        }

        // A size given as bytes or as "<number> <unit>".
        std::uint64_t sizeOf(const JsonValue& v) {
            if (v.isNumber()) {
                return v.asUnsigned();
            }
            const std::string& text = v.asString();
            std::uint64_t number = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
            std::string_view unit(end, text.data() + text.size());
            unit.remove_prefix(std::min(unit.find_first_not_of(' '), unit.size()));
            const auto it = std::ranges::find(sizeUnits, unit, &std::pair<std::string_view, std::uint64_t>::first);
            if (error != std::errc() || it == sizeUnits.end()) {
                throw CatalogError(std::format("bad size \"{}\" (use bytes or e.g. \"100 MB\")", text));
            }
            return number * it->second;
        }

        // The strings of an optional array.
        std::vector<std::string> stringsOf(const JsonValue* list) {
            std::vector<std::string> out;
            for (const JsonValue& v : list == nullptr ? std::vector<JsonValue>{} : list->asArray()) {
                out.push_back(v.asString());
            }
            return out;
        }

        // The churn condition of a rule.
        ChurnTest churnOf(const JsonValue* v) {
            const std::string text = v == nullptr ? "any" : v->asString();
            if (text == "any" || text == "churning" || text == "stable") {
                return text == "any" ? ChurnTest::Any : (text == "churning" ? ChurnTest::Churning : ChurnTest::Stable);
            }
            throw CatalogError(std::format("churn must be \"any\", \"churning\" or \"stable\", not \"{}\"", text));
        }

        // A class color "#RRGGBB".
        std::uint32_t colorOf(const std::string& text) {
            std::uint32_t color = 0;
            const bool shape = text.size() == 7 && text[0] == '#';
            const auto [end, error] = std::from_chars(text.data() + std::min<std::size_t>(1, text.size()),
                                                      text.data() + text.size(), color, 16);
            if (!shape || error != std::errc() || end != text.data() + text.size()) {
                throw CatalogError(std::format("bad color \"{}\" (use \"#RRGGBB\")", text));
            }
            return color;
        }

        // A class id by name, or CatalogError.
        ClassId classNamed(const Catalog& c, const std::string& id) {
            const auto found = c.find(id);
            if (!found) {
                throw CatalogError(std::format("unknown class \"{}\"", id));
            }
            return *found;
        }

        // Optional whole-number member (capped to the type), or a default.
        template <typename T>
        T numberOr(const JsonValue& o, std::string_view key, T otherwise) {
            const JsonValue* v = o.find(key);
            constexpr std::uint64_t most = std::numeric_limits<T>::max();
            return v == nullptr ? otherwise : static_cast<T>(std::min<std::uint64_t>(v->asUnsigned(), most));
        }

        // Throws for members a reader does not know, so a misspelt key is not silently ignored.
        void checkKeys(const JsonValue& o, std::initializer_list<std::string_view> known) {
            for (const auto& [key, value] : o.asObject()) {
                if (std::ranges::find(known, key) == known.end()) {
                    throw CatalogError(std::format("unknown key \"{}\"", key));
                }
            }
        }

        // Sizes and ages of a rule.
        void readLimits(const JsonValue& o, Rule& r) {
            const JsonValue* minSize = o.find("minSize");
            const JsonValue* maxSize = o.find("maxSize");
            r.minSize = minSize == nullptr ? 0 : sizeOf(*minSize);
            r.maxSize = maxSize == nullptr ? r.maxSize : sizeOf(*maxSize);
            r.minAgeDays = numberOr<std::uint32_t>(o, "minAgeDays", 0);
            r.maxAgeDays = numberOr<std::uint32_t>(o, "maxAgeDays", r.maxAgeDays);
        }

        // One rule.
        Rule ruleOf(const Catalog& c, const JsonValue& o) {
            Rule r;
            r.name = o.at("name").asString();
            try {
                checkKeys(o,
                          {"name", "class", "paths", "extensions", "minSize", "maxSize", "minAgeDays", "maxAgeDays",
                           "flags", "notFlags", "churn"});
                r.target = classNamed(c, o.at("class").asString());
                r.paths = stringsOf(o.find("paths"));
                r.extensions = stringsOf(o.find("extensions"));
                readLimits(o, r);
                r.flagsAll = flagsOf(o.find("flags"));
                r.flagsNone = flagsOf(o.find("notFlags"));
                r.churn = churnOf(o.find("churn"));
            }
            catch (const std::exception& e) {
                throw CatalogError(std::format("rule \"{}\": {}", r.name, e.what()));
            }
            return r;
        }

        // One class.
        ClassInfo classOf(const JsonValue& o) {
            checkKeys(o, {"id", "name", "color", "description"});
            const JsonValue* description = o.find("description");
            return {.id = o.at("id").asString(),
                    .name = o.at("name").asString(),
                    .color = colorOf(o.at("color").asString()),
                    .description = description == nullptr ? std::string() : description->asString()};
        }

        // The classes, refusing duplicate ids.
        void readClasses(const JsonValue& root, Catalog& c) {
            for (const JsonValue& o : root.at("classes").asArray()) {
                ClassInfo info = classOf(o);
                if (c.find(info.id)) {
                    throw CatalogError(std::format("class \"{}\" is listed twice", info.id));
                }
                c.rules.classNames.push_back(info.name);
                c.classes.push_back(std::move(info));
            }
        }

        // The whole catalog from a parsed document.
        Catalog catalogOf(const JsonValue& root) {
            if (root.at("version").asUnsigned() != catalogVersion) {
                throw CatalogError(std::format("catalog version {} is not supported (expected {})",
                                               root.at("version").asUnsigned(), catalogVersion));
            }
            Catalog c;
            readClasses(root, c);
            c.rules.fallback = classNamed(c, root.at("fallback").asString());
            for (const JsonValue& o : root.at("rules").asArray()) {
                c.rules.rules.push_back(ruleOf(c, o));
            }
            return c;
        }

    } // namespace

    std::optional<ClassId> Catalog::find(std::string_view id) const {
        const auto it = std::ranges::find(classes, id, &ClassInfo::id);
        return it == classes.end() ? std::nullopt : std::optional(static_cast<ClassId>(it - classes.begin()));
    }

    Catalog parseCatalog(std::string_view json) {
        try {
            Catalog c = catalogOf(parseJson(json));
            (void)RuleEngine(c.rules); // Bad patterns and too many classes show up now, not at first use.
            return c;
        }
        catch (const CatalogError&) {
            throw;
        }
        catch (const std::exception& e) {
            throw CatalogError(std::format("class catalog: {}", e.what()));
        }
    }

    const Catalog& builtinCatalog() {
        static const Catalog catalog = parseCatalog(builtinCatalogJson());
        return catalog;
    }

} // namespace icd::core::classify
