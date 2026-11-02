#include "core/strategy/ZoneScript.h"

#include "core/format/ByteSize.h"
#include "core/format/Json.h"
#include "core/format/JsonReader.h"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <set>
#include <utility>

namespace icd::core::strategy {

    namespace {

        // The zone script format this code reads and writes.
        constexpr std::uint64_t scriptVersion = 1;

        // A name table entry: the script spelling of an enum value.
        template <typename E>
        using Named = std::pair<std::string_view, E>;

        constexpr std::array<Named<Effort>, 3> effortNames{{
            {"fast", Effort::Fast},
            {"moderate", Effort::Moderate},
            {"maximum", Effort::Maximum},
        }};

        constexpr std::array<Named<Anchor>, 3> anchorNames{{
            {"front", Anchor::Front},
            {"after-previous", Anchor::AfterPrevious},
            {"back", Anchor::Back},
        }};

        constexpr std::array<Named<ZoneOrder>, 8> orderNames{{
            {"none", ZoneOrder::None},
            {"directory+name", ZoneOrder::DirectoryName},
            {"name", ZoneOrder::Name},
            {"size", ZoneOrder::Size},
            {"age", ZoneOrder::Age},
            {"modified", ZoneOrder::Modified},
            {"created", ZoneOrder::Created},
            {"accessed", ZoneOrder::Accessed},
        }};

        constexpr std::array<Named<ZoneAction>, 5> actionNames{{
            {"keep-in-place", ZoneAction::KeepInPlace},
            {"defragment-only", ZoneAction::DefragmentOnly},
            {"fast-fill", ZoneAction::FastFill},
            {"sort", ZoneAction::Sort},
            {"pack-to-back", ZoneAction::PackToBack},
        }};

        constexpr std::array<Named<classify::ChurnTest>, 3> churnNames{{
            {"any", classify::ChurnTest::Any},
            {"churning", classify::ChurnTest::Churning},
            {"stable", classify::ChurnTest::Stable},
        }};

        // A malformed member while reading; becomes a ScriptIssue of its zone.
        class ParseFailure : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
        };

        // The enum value spelt `text`, or ParseFailure naming the accepted spellings.
        template <typename E, std::size_t N>
        E enumNamed(const std::array<Named<E>, N>& table, std::string_view text, std::string_view what) {
            const auto it = std::ranges::find(table, text, &Named<E>::first);
            if (it != table.end()) {
                return it->second;
            }
            std::string accepted;
            for (const auto& [name, value] : table) {
                accepted += std::format("{}\"{}\"", accepted.empty() ? "" : ", ", name);
            }
            throw ParseFailure(std::format("unknown {} \"{}\" (use {})", what, text, accepted));
        }

        // The spelling of an enum value.
        template <typename E, std::size_t N>
        std::string_view keyOf(const std::array<Named<E>, N>& table, E value) {
            const auto it = std::ranges::find(table, value, &Named<E>::second);
            return it == table.end() ? std::string_view("?") : it->first;
        }

        // Throws for members a reader does not know, so a misspelt key is not silently ignored.
        void checkKeys(const JsonValue& o, std::initializer_list<std::string_view> known) {
            for (const auto& [key, value] : o.asObject()) {
                if (std::ranges::find(known, key) == known.end()) {
                    throw ParseFailure(std::format("unknown key \"{}\"", key));
                }
            }
        }

        // The strings of an optional array.
        std::vector<std::string> stringsOf(const JsonValue* list) {
            std::vector<std::string> out;
            for (const JsonValue& v : list == nullptr ? std::vector<JsonValue>{} : list->asArray()) {
                out.push_back(v.asString());
            }
            return out;
        }

        // A size given as bytes or as "<number> <unit>".
        std::uint64_t sizeOf(const JsonValue& v) {
            if (v.isNumber()) {
                return v.asUnsigned();
            }
            const auto size = parseSizeSpec(v.asString());
            if (!size) {
                throw ParseFailure(std::format("bad size \"{}\" (use bytes or e.g. \"64 MB\")", v.asString()));
            }
            return *size;
        }

        // A percentage member, or 0.
        double percentOf(const JsonValue& o, std::string_view key) {
            const JsonValue* v = o.find(key);
            return v == nullptr ? 0.0 : v->asNumber();
        }

        // The flag bits of a list of flag names.
        std::uint16_t flagsOf(const JsonValue* list) {
            std::uint16_t flags = 0;
            for (const std::string& name : stringsOf(list)) {
                const auto bit = classify::fileFlagNamed(name);
                if (!bit) {
                    throw ParseFailure(std::format("unknown file flag \"{}\"", name));
                }
                flags = static_cast<std::uint16_t>(flags | *bit);
            }
            return flags;
        }

        // Size and age limits of a selector.
        void readLimits(const JsonValue& o, ZoneSelector& s) {
            const JsonValue* minSize = o.find("minSize");
            const JsonValue* maxSize = o.find("maxSize");
            const JsonValue* minAge = o.find("minAgeDays");
            const JsonValue* maxAge = o.find("maxAgeDays");
            s.minSize = minSize == nullptr ? 0 : sizeOf(*minSize);
            s.maxSize = maxSize == nullptr ? s.maxSize : sizeOf(*maxSize);
            s.minAgeDays = minAge == nullptr ? 0 : static_cast<std::uint32_t>(minAge->asUnsigned());
            s.maxAgeDays = maxAge == nullptr ? s.maxAgeDays : static_cast<std::uint32_t>(maxAge->asUnsigned());
        }

        // A zone's selector; absent means every file.
        ZoneSelector selectorOf(const JsonValue* o) {
            ZoneSelector s;
            if (o == nullptr) {
                return s;
            }
            checkKeys(*o,
                      {"classes", "paths", "extensions", "minSize", "maxSize", "minAgeDays", "maxAgeDays", "flags",
                       "notFlags", "churn"});
            s.classes = stringsOf(o->find("classes"));
            s.paths = stringsOf(o->find("paths"));
            s.extensions = stringsOf(o->find("extensions"));
            readLimits(*o, s);
            s.flagsAll = flagsOf(o->find("flags"));
            s.flagsNone = flagsOf(o->find("notFlags"));
            const JsonValue* churn = o->find("churn");
            s.churn = churn == nullptr ? classify::ChurnTest::Any : enumNamed(churnNames, churn->asString(), "churn");
            return s;
        }

        // A zone's gap: 0 or an object of terms.
        GapSpec gapOf(const JsonValue* v) {
            GapSpec g;
            if (v == nullptr || (v->isNumber() && v->asNumber() == 0.0)) {
                return g;
            }
            checkKeys(*v, {"freePercent", "volumePercent", "bytes", "classMultiple", "classes"});
            g.freePercent = percentOf(*v, "freePercent");
            g.volumePercent = percentOf(*v, "volumePercent");
            const JsonValue* bytes = v->find("bytes");
            g.bytes = bytes == nullptr ? 0 : sizeOf(*bytes);
            g.classMultiple = percentOf(*v, "classMultiple");
            g.classes = stringsOf(v->find("classes"));
            return g;
        }

        // A zone's fragment_ok: absent gives the default, "whole" means one fragment.
        std::optional<std::uint64_t> fragmentOkOf(const JsonValue* v) {
            if (v == nullptr) {
                return Zone::defaultFragmentOk;
            }
            if (v->isString() && v->asString() == "whole") {
                return std::nullopt;
            }
            return sizeOf(*v);
        }

        // Per-level order overrides.
        std::array<std::optional<ZoneOrder>, 3> orderByLevelOf(const JsonValue* o) {
            std::array<std::optional<ZoneOrder>, 3> out{};
            if (o == nullptr) {
                return out;
            }
            for (const auto& [key, value] : o->asObject()) {
                const Effort effort = enumNamed(effortNames, key, "effort level");
                out.at(static_cast<std::size_t>(effort)) = enumNamed(orderNames, value.asString(), "order");
            }
            return out;
        }

        // One zone.
        Zone zoneOf(const JsonValue& o) {
            checkKeys(o, {"name", "select", "anchor", "order", "orderByLevel", "action", "fragmentOk", "gapAfter"});
            Zone z;
            z.name = o.at("name").asString();
            z.select = selectorOf(o.find("select"));
            const JsonValue* anchor = o.find("anchor");
            const JsonValue* order = o.find("order");
            z.anchor = anchor == nullptr ? Anchor::AfterPrevious : enumNamed(anchorNames, anchor->asString(), "anchor");
            z.order = order == nullptr ? ZoneOrder::None : enumNamed(orderNames, order->asString(), "order");
            z.orderByLevel = orderByLevelOf(o.find("orderByLevel"));
            z.action = enumNamed(actionNames, o.at("action").asString(), "action");
            z.fragmentOk = fragmentOkOf(o.find("fragmentOk"));
            z.gapAfter = gapOf(o.find("gapAfter"));
            return z;
        }

        // The script's own members; zones are read separately so each zone's problems are reported.
        void readHeader(const JsonValue& root, ZoneScript& s) {
            checkKeys(root, {"version", "id", "name", "description", "zones"});
            const std::uint64_t version = root.at("version").asUnsigned();
            if (version != scriptVersion) {
                throw ParseFailure(
                    std::format("zone script version {} is not supported (expected {})", version, scriptVersion));
            }
            s.id = root.at("id").asString();
            s.name = root.at("name").asString();
            const JsonValue* description = root.find("description");
            s.description = description == nullptr ? std::string() : description->asString();
        }

        // Reads every zone, collecting one issue per zone that cannot be read.
        void readZones(const JsonValue& root, ZoneScript& s, std::vector<ScriptIssue>& issues) {
            const auto& zones = root.at("zones").asArray();
            for (std::size_t index = 0; index < zones.size(); ++index) {
                try {
                    s.zones.push_back(zoneOf(zones[index]));
                }
                catch (const std::exception& e) {
                    issues.push_back({index, e.what()});
                    s.zones.emplace_back();
                }
            }
        }

        // Adds an issue.
        void report(std::vector<ScriptIssue>& issues, std::optional<std::size_t> zone, std::string message) {
            issues.push_back({zone, std::move(message)});
        }

        // Unknown class ids among a list.
        void checkClasses(const std::vector<std::string>& ids, const classify::Catalog& catalog, std::size_t zone,
                          std::vector<ScriptIssue>& issues) {
            for (const std::string& id : ids) {
                if (!catalog.find(id)) {
                    report(issues, zone, std::format("unknown class \"{}\"", id));
                }
            }
        }

        // Path globs and extensions compile into a rule engine.
        void checkPatterns(const ZoneSelector& s, std::size_t zone, std::vector<ScriptIssue>& issues) {
            if (s.paths.empty() && s.extensions.empty()) {
                return;
            }
            try {
                classify::RuleSet rules;
                rules.classNames = {"other", "zone"};
                rules.rules.push_back({.name = "zone", .target = 1, .paths = s.paths, .extensions = s.extensions});
                (void)classify::RuleEngine(std::move(rules));
            }
            catch (const std::exception& e) {
                report(issues, zone, e.what());
            }
        }

        // A selector's classes, patterns and ranges.
        void checkSelector(const ZoneSelector& s, const classify::Catalog& catalog, std::size_t zone,
                           std::vector<ScriptIssue>& issues) {
            checkClasses(s.classes, catalog, zone, issues);
            checkPatterns(s, zone, issues);
            if (s.minSize >= s.maxSize) {
                report(issues, zone, "minSize must be below maxSize");
            }
            if (s.minAgeDays >= s.maxAgeDays) {
                report(issues, zone, "minAgeDays must be below maxAgeDays");
            }
            if ((s.flagsAll & s.flagsNone) != 0) {
                report(issues, zone, "a flag cannot be both required and excluded");
            }
        }

        // A gap's terms.
        void checkGap(const Zone& z, const classify::Catalog& catalog, std::size_t zone,
                      std::vector<ScriptIssue>& issues) {
            const GapSpec& g = z.gapAfter;
            const auto percent = [](double p) { return p >= 0.0 && p <= 100.0; };
            if (!percent(g.freePercent) || !percent(g.volumePercent)) {
                report(issues, zone, "gap percentages must be between 0 and 100");
            }
            if (g.classMultiple < 0.0 || (g.classMultiple > 0.0) != !g.classes.empty()) {
                report(issues, zone, "a gap's classMultiple needs classes, and classes need a positive multiple");
            }
            checkClasses(g.classes, catalog, zone, issues);
            if (!g.empty() && !z.placed()) {
                report(
                    issues, zone,
                    std::format("a {} zone has no place on the volume, so it cannot have a gap", actionKey(z.action)));
            }
        }

        // Action, order and anchor of one zone on its own.
        void checkAction(const Zone& z, std::size_t zone, std::vector<ScriptIssue>& issues) {
            const bool anyOrder = z.order != ZoneOrder::None ||
                std::ranges::any_of(z.orderByLevel, [](auto o) { return o && *o != ZoneOrder::None; });
            if (z.action == ZoneAction::Sort && !anyOrder) {
                report(issues, zone, "a sort zone needs an order");
            }
            if ((z.anchor == Anchor::Back) != (z.action == ZoneAction::PackToBack) && z.placed()) {
                report(issues, zone, "pack-to-back zones and only they are anchored at the back");
            }
            if (z.name.empty()) {
                report(issues, zone, "a zone needs a name");
            }
        }

        // Anchors across zones: front only first, nothing forward after a back zone.
        void checkPlacementOrder(const ZoneScript& script, std::vector<ScriptIssue>& issues) {
            bool seenPlaced = false;
            bool seenBack = false;
            for (std::size_t index = 0; index < script.zones.size(); ++index) {
                const Zone& z = script.zones[index];
                if (!z.placed()) {
                    continue;
                }
                if (z.anchor == Anchor::Front && seenPlaced) {
                    report(issues, index, "only the first placed zone can be anchored at the front");
                }
                if (z.anchor != Anchor::Back && seenBack) {
                    report(issues, index, "zones after a back zone must be back zones too");
                }
                seenPlaced = true;
                seenBack = seenBack || z.anchor == Anchor::Back;
            }
        }

        // Script id, name, zone count and unique zone names.
        void checkScript(const ZoneScript& script, std::vector<ScriptIssue>& issues) {
            if (script.id.empty() || script.name.empty()) {
                report(issues, std::nullopt, "a zone script needs an id and a name");
            }
            if (script.zones.empty()) {
                report(issues, std::nullopt, "a zone script needs at least one zone");
            }
            std::set<std::string> names;
            for (std::size_t index = 0; index < script.zones.size(); ++index) {
                const std::string& name = script.zones[index].name;
                if (!name.empty() && !names.insert(name).second) {
                    report(issues, index, std::format("zone name \"{}\" is used twice", name));
                }
            }
        }

        // Writes an optional string list.
        void writeStrings(JsonWriter& w, std::string_view key, const std::vector<std::string>& list) {
            if (list.empty()) {
                return;
            }
            w.key(key).beginArray();
            for (const std::string& s : list) {
                w.value(s);
            }
            w.endArray();
        }

        // Writes the size and age limits that differ from the defaults.
        void writeLimits(JsonWriter& w, const ZoneSelector& s) {
            const ZoneSelector d;
            if (s.minSize != d.minSize) {
                w.key("minSize").value(sizeSpecText(s.minSize));
            }
            if (s.maxSize != d.maxSize) {
                w.key("maxSize").value(sizeSpecText(s.maxSize));
            }
            if (s.minAgeDays != d.minAgeDays) {
                w.key("minAgeDays").value(std::uint64_t{s.minAgeDays});
            }
            if (s.maxAgeDays != d.maxAgeDays) {
                w.key("maxAgeDays").value(std::uint64_t{s.maxAgeDays});
            }
        }

        // Writes a selector unless it takes every file.
        void writeSelector(JsonWriter& w, const ZoneSelector& s) {
            if (s == ZoneSelector{}) {
                return;
            }
            w.key("select").beginObject();
            writeStrings(w, "classes", s.classes);
            writeStrings(w, "paths", s.paths);
            writeStrings(w, "extensions", s.extensions);
            writeLimits(w, s);
            writeStrings(w, "flags", classify::fileFlagNames(s.flagsAll));
            writeStrings(w, "notFlags", classify::fileFlagNames(s.flagsNone));
            if (s.churn != classify::ChurnTest::Any) {
                w.key("churn").value(keyOf(churnNames, s.churn));
            }
            w.endObject();
        }

        // Writes a gap unless empty.
        void writeGap(JsonWriter& w, const GapSpec& g) {
            if (g.empty()) {
                return;
            }
            w.key("gapAfter").beginObject();
            if (g.freePercent > 0.0) {
                w.key("freePercent").value(g.freePercent);
            }
            if (g.volumePercent > 0.0) {
                w.key("volumePercent").value(g.volumePercent);
            }
            if (g.bytes != 0) {
                w.key("bytes").value(sizeSpecText(g.bytes));
            }
            if (g.classMultiple > 0.0) {
                w.key("classMultiple").value(g.classMultiple);
            }
            writeStrings(w, "classes", g.classes);
            w.endObject();
        }

        // Writes the per-level order overrides that are set.
        void writeOrderByLevel(JsonWriter& w, const Zone& z) {
            if (std::ranges::none_of(z.orderByLevel, [](auto o) { return o.has_value(); })) {
                return;
            }
            w.key("orderByLevel").beginObject();
            for (const Effort effort : allEfforts) {
                const auto order = z.orderByLevel.at(static_cast<std::size_t>(effort));
                if (order) {
                    w.key(effortKey(effort)).value(orderKey(*order));
                }
            }
            w.endObject();
        }

        // Writes one zone.
        void writeZone(JsonWriter& w, const Zone& z) {
            w.beginObject().key("name").value(z.name);
            writeSelector(w, z.select);
            w.key("action").value(actionKey(z.action));
            if (z.anchor != Anchor::AfterPrevious) {
                w.key("anchor").value(anchorKey(z.anchor));
            }
            if (z.order != ZoneOrder::None) {
                w.key("order").value(orderKey(z.order));
            }
            writeOrderByLevel(w, z);
            if (!z.fragmentOk) {
                w.key("fragmentOk").value("whole");
            }
            else if (*z.fragmentOk != Zone::defaultFragmentOk) {
                w.key("fragmentOk").value(sizeSpecText(*z.fragmentOk));
            }
            writeGap(w, z.gapAfter);
            w.endObject();
        }

    } // namespace

    std::string_view effortKey(Effort effort) { return keyOf(effortNames, effort); }
    std::string_view anchorKey(Anchor anchor) { return keyOf(anchorNames, anchor); }
    std::string_view orderKey(ZoneOrder order) { return keyOf(orderNames, order); }
    std::string_view actionKey(ZoneAction action) { return keyOf(actionNames, action); }

    ZoneOrder Zone::orderAt(Effort effort) const {
        return orderByLevel.at(static_cast<std::size_t>(effort)).value_or(order);
    }

    bool Zone::placed() const { return action != ZoneAction::KeepInPlace && action != ZoneAction::DefragmentOnly; }

    ZoneScriptError::ZoneScriptError(std::vector<ScriptIssue> issues) :
        std::runtime_error([&] {
            std::string text = "zone script:";
            for (const ScriptIssue& issue : issues) {
                text += issue.zone ? std::format("\n  zone {}: {}", *issue.zone + 1, issue.message)
                                   : std::format("\n  {}", issue.message);
            }
            return text;
        }()),
        issues_(std::move(issues)) {}

    std::vector<ScriptIssue> validateScript(const ZoneScript& script, const classify::Catalog& catalog) {
        std::vector<ScriptIssue> issues;
        checkScript(script, issues);
        for (std::size_t index = 0; index < script.zones.size(); ++index) {
            const Zone& z = script.zones[index];
            checkAction(z, index, issues);
            checkSelector(z.select, catalog, index, issues);
            checkGap(z, catalog, index, issues);
        }
        checkPlacementOrder(script, issues);
        std::ranges::stable_sort(issues, {}, [](const ScriptIssue& i) { return i.zone.value_or(0); });
        return issues;
    }

    ZoneScript parseZoneScript(std::string_view json, const classify::Catalog& catalog) {
        ZoneScript script;
        std::vector<ScriptIssue> issues;
        try {
            const JsonValue root = parseJson(json);
            readHeader(root, script);
            readZones(root, script, issues);
        }
        catch (const std::exception& e) {
            report(issues, std::nullopt, e.what());
        }
        if (issues.empty()) {
            issues = validateScript(script, catalog);
        }
        if (!issues.empty()) {
            throw ZoneScriptError(std::move(issues));
        }
        return script;
    }

    std::string zoneScriptJson(const ZoneScript& script) {
        JsonWriter w;
        w.beginObject().key("version").value(scriptVersion).key("id").value(script.id).key("name").value(script.name);
        if (!script.description.empty()) {
            w.key("description").value(script.description);
        }
        w.key("zones").beginArray();
        for (const Zone& z : script.zones) {
            writeZone(w, z);
        }
        w.endArray().endObject();
        return w.str() + "\n";
    }

    std::string describeIssue(const ScriptIssue& issue, const ZoneScript& script) {
        if (!issue.zone) {
            return issue.message;
        }
        const std::size_t zone = *issue.zone;
        const std::string name = zone < script.zones.size() ? script.zones[zone].name : std::string();
        return name.empty() ? std::format("zone {}: {}", zone + 1, issue.message)
                            : std::format("zone {} \"{}\": {}", zone + 1, name, issue.message);
    }

} // namespace icd::core::strategy
