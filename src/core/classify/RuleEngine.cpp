#include "core/classify/RuleEngine.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <optional>
#include <utility>

namespace icd::core::classify {

    namespace {

        // Most automaton states over all path globs of a rule set (16 words of 64 bits).
        constexpr std::size_t maxStateWords = 16;

        // FILETIME ticks per day.
        constexpr std::int64_t ticksPerDay = 864'000'000'000;

        // Files per chunk of the parallel loops.
        constexpr std::size_t fileGrain = 8192;

        // "No extension of interest" in the per-name extension table.
        constexpr std::uint16_t noExtension = 0xFFFF;

        // "No override" in the per-directory override table.
        constexpr std::uint16_t noOverride = 0xFFFF;

        // "Not a directory" in the file -> directory slot table.
        constexpr std::uint32_t noSlot = 0xFFFF'FFFF;

        // Most distinct marker globs, and most rules with markers: each takes one bit of a 64-bit mask.
        constexpr std::size_t maxMarkerBits = 64;

        // A set of automaton states.
        using StateSet = std::array<std::uint64_t, maxStateWords>;

        // ASCII lower case; other bytes (UTF-8 sequences) stay as they are.
        char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

        // A copy in ASCII lower case.
        std::string lowered(std::string_view text) {
            std::string out(text);
            std::ranges::transform(out, out.begin(), lower);
            return out;
        }

        // Whether glob character g (lower case or `?`) matches name character c.
        bool charMatches(char g, char c) { return g == '?' || g == lower(c); }

        // Whether a glob of one component (lower case, `*` and `?`) matches a name, ignoring ASCII case. A failed
        // match after a `*` lets that `*` take one more character.
        bool globMatch(std::string_view glob, std::string_view name) {
            std::size_t g = 0;
            std::size_t n = 0;
            std::size_t starGlob = std::string_view::npos; // Position after the last `*`.
            std::size_t starName = 0; // Name position that `*` was last tried at.
            while (n < name.size()) {
                const bool more = g < glob.size();
                if (more && glob[g] == '*') {
                    starGlob = ++g;
                    starName = n;
                }
                else if (more && charMatches(glob[g], name[n])) {
                    ++g;
                    ++n;
                }
                else if (starGlob != std::string_view::npos) {
                    g = starGlob;
                    n = ++starName;
                }
                else {
                    return false;
                }
            }
            return glob.find_first_not_of('*', g) == std::string_view::npos;
        }

        // Path components split at / and \, empty ones dropped.
        std::vector<std::string_view> components(std::string_view path) {
            std::vector<std::string_view> out;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= path.size(); ++i) {
                if (i == path.size() || path[i] == '/' || path[i] == '\\') {
                    if (i > start) {
                        out.push_back(path.substr(start, i - start));
                    }
                    start = i + 1;
                }
            }
            return out;
        }

        // Whether two names are equal ignoring ASCII case.
        bool sameName(std::string_view a, std::string_view b) {
            return a.size() == b.size() &&
                std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
        }

        // Whether a < b ignoring ASCII case, for the sorted marker names.
        bool lessIgnoringCase(std::string_view a, std::string_view b) {
            return std::ranges::lexicographical_compare(a, b, [](char x, char y) { return lower(x) < lower(y); });
        }

        // Throws unless a glob is one path component (markers and folders name entries of one directory).
        void checkComponent(std::string_view glob, std::string_view what) {
            const bool split = glob.find_first_of("/\\") != std::string_view::npos;
            if (glob.empty() || split || glob.find("**") != std::string_view::npos) {
                throw RuleError(std::format("{} \"{}\" must be one name (\"*\" and \"?\" allowed)", what, glob));
            }
        }

        // Marker globs numbered as bits, so the markers a directory holds are one 64-bit mask. Exact names are found
        // by binary search; only the few globs with wildcards are matched one by one.
        class MarkerTable {
        public:
            // The bit of a glob, numbering new ones. Throws RuleError past maxMarkerBits globs.
            std::uint64_t bitOf(std::string_view glob) {
                const std::string g = lowered(glob);
                const bool wild = g.find_first_of("*?") != std::string::npos;
                std::vector<Entry>& list = wild ? wild_ : exact_;
                const auto it = std::ranges::lower_bound(list, g, lessIgnoringCase, &Entry::glob);
                if (it != list.end() && it->glob == g) {
                    return it->bit;
                }
                if (count_ == maxMarkerBits) {
                    throw RuleError(std::format("more than {} distinct project markers", maxMarkerBits));
                }
                const std::uint64_t bit = std::uint64_t{1} << count_++;
                list.insert(it, {.glob = g, .bit = bit});
                return bit;
            }

            // The bits of the globs a name matches (0 for almost every name).
            [[nodiscard]] std::uint64_t of(std::string_view name) const {
                std::uint64_t bits = 0;
                const auto it = std::ranges::lower_bound(exact_, name, lessIgnoringCase, &Entry::glob);
                if (it != exact_.end() && sameName(it->glob, name)) {
                    bits |= it->bit;
                }
                for (const Entry& w : wild_) {
                    bits |= globMatch(w.glob, name) ? w.bit : 0;
                }
                return bits;
            }

        private:
            // One glob and its bit.
            struct Entry {
                std::string glob; // Lower case.
                std::uint64_t bit = 0; // Its bit.
            };

            std::vector<Entry> exact_; // Globs without wildcards, sorted.
            std::vector<Entry> wild_; // Globs with `*` or `?`, sorted.
            std::size_t count_ = 0; // Bits given out.
        };

        // One step of a path glob.
        struct Token {
            std::string glob; // Lower-case component glob; empty for `**`.
            bool anyDepth = false; // `**`: any number of components.
        };

        // All path globs of a rule set as one automaton: state i of a glob with tokens t0..tk-1 is "the first i tokens
        // matched"; state k accepts. States of all globs are numbered one after another.
        class PathAutomaton {
        public:
            // Adds a glob and returns its accepting state. Throws RuleError when the automaton grows too large.
            std::size_t add(std::string_view pattern) {
                const std::vector<std::string_view> parts = components(pattern);
                if (parts.empty()) {
                    throw RuleError(std::format("empty path pattern \"{}\"", pattern));
                }
                const std::size_t first = tokens_.size();
                checkParts(parts, pattern);
                for (const std::string_view part : parts) {
                    tokens_.push_back({part == "**" ? std::string() : lowered(part), part == "**"});
                }
                tokens_.push_back({}); // The accepting state has no token.
                starts_.push_back(first);
                if (tokens_.size() > maxStateWords * 64) {
                    throw RuleError("too many path pattern parts");
                }
                return tokens_.size() - 1;
            }

            // Whether any glob was added.
            [[nodiscard]] bool empty() const { return starts_.empty(); }

            // Words a state set needs.
            [[nodiscard]] std::size_t words() const { return (tokens_.size() + 63) / 64; }

            // The state set at the volume root: every glob at its start.
            [[nodiscard]] StateSet initial() const {
                StateSet s{};
                for (const std::size_t start : starts_) {
                    set(s, start);
                }
                close(s);
                return s;
            }

            // The state set after one more path component.
            [[nodiscard]] StateSet step(const StateSet& in, std::string_view name) const {
                StateSet out{};
                for (std::size_t w = 0; w < words(); ++w) {
                    for (std::uint64_t bits = in[w]; bits != 0; bits &= bits - 1) {
                        advance(w * 64 + static_cast<std::size_t>(std::countr_zero(bits)), name, out);
                    }
                }
                close(out);
                return out;
            }

            // Whether a state is in a set.
            static bool has(const StateSet& s, std::size_t state) {
                return ((s[state / 64] >> (state % 64)) & 1U) != 0;
            }

        private:
            // Throws for parts mixing `**` with other text.
            static void checkParts(const std::vector<std::string_view>& parts, std::string_view pattern) {
                const auto mixed = [](std::string_view part) {
                    return part != "**" && part.find("**") != std::string_view::npos;
                };
                if (std::ranges::any_of(parts, mixed)) {
                    throw RuleError(std::format("\"**\" must be a whole path part in \"{}\"", pattern));
                }
            }

            // Adds a state to a set.
            static void set(StateSet& s, std::size_t state) { s[state / 64] |= std::uint64_t{1} << (state % 64); }

            // Where state s goes on a component: `**` stays, a matching glob moves on.
            void advance(std::size_t s, std::string_view name, StateSet& out) const {
                const Token& t = tokens_[s];
                if (t.anyDepth) {
                    set(out, s);
                }
                else if (!t.glob.empty() && globMatch(t.glob, name)) {
                    set(out, s + 1);
                }
            }

            // Adds the states reachable without a component: past every `**` (zero components), in order so chains of
            // them close in one pass.
            void close(StateSet& s) const {
                for (std::size_t state = 0; state + 1 < tokens_.size(); ++state) {
                    if (tokens_[state].anyDepth && has(s, state)) {
                        set(s, state + 1);
                    }
                }
            }

            std::vector<Token> tokens_; // Per state: its token (empty for accepting states).
            std::vector<std::size_t> starts_; // First state of each glob.
        };

        // One rule ready to test.
        struct CompiledRule {
            ClassId target = 0; // The class it gives.
            std::vector<std::size_t> accepts; // Accepting states of its path globs; empty: any path.
            std::vector<std::uint64_t> extensions; // Bitset over the extension table; empty: any extension.
            std::uint64_t markerBit = 0; // Its bit among the rules with markers; 0: no marker condition.
            std::uint64_t markerMask = 0; // Bits of its marker globs in the marker table.
            std::vector<std::string> folders; // Lower-case folder globs; empty: in or below a marked directory.
            const Rule* source = nullptr; // Sizes, ages, flags, churn.
        };

        // What the rule tests look at for one file.
        struct FileView {
            std::uint64_t sizeBytes = 0; // Logical size.
            std::int64_t ageDays = 0; // Whole days since the last change.
            std::uint16_t flags = 0; // FileFlag bits.
            std::uint16_t extension = noExtension; // Index into the extension table.
            bool churn = false; // Flagged as churn.
            const StateSet* states = nullptr; // Path automaton state after the file's own name.
            std::uint64_t inside = 0; // Marker bits of the rules whose project or build folder holds the file.
        };

        // Whole days from a FILETIME to now; changes in the future count as new.
        std::int64_t ageInDays(std::int64_t modified, std::int64_t now) {
            return modified >= now ? 0 : (now - modified) / ticksPerDay;
        }

        // Whether the size, age, flag and churn conditions of a rule hold.
        bool factsMatch(const Rule& r, const FileView& v) {
            const bool flags = (v.flags & r.flagsAll) == r.flagsAll && (v.flags & r.flagsNone) == 0;
            const bool size = v.sizeBytes >= r.minSize && v.sizeBytes < r.maxSize;
            const bool age = v.ageDays >= r.minAgeDays && v.ageDays < static_cast<std::int64_t>(r.maxAgeDays);
            const bool churn = r.churn == ChurnTest::Any || (r.churn == ChurnTest::Churning) == v.churn;
            return flags && size && age && churn;
        }

        // Whether a file's extension is one of a rule's.
        bool extensionMatches(const CompiledRule& r, const FileView& v) {
            if (r.extensions.empty()) {
                return true;
            }
            return v.extension != noExtension && ((r.extensions[v.extension / 64] >> (v.extension % 64)) & 1U) != 0;
        }

        // Whether a file's path matches one of a rule's globs.
        bool pathMatches(const CompiledRule& r, const FileView& v) {
            return r.accepts.empty() ||
                std::ranges::any_of(r.accepts, [&](std::size_t s) { return PathAutomaton::has(*v.states, s); });
        }

        // Whether a file is inside the project or build folder a rule's markers call for.
        bool markersMatch(const CompiledRule& r, const FileView& v) {
            return r.markerBit == 0 || (v.inside & r.markerBit) != 0;
        }

        // The marker bits around one directory, named so the two masks cannot be swapped.
        struct DirectoryMarkers {
            std::uint64_t own = 0; // Markers among the directory's entries.
            std::uint64_t parent = 0; // Markers among its parent's entries.
        };

        // Whether a directory meets a rule's marker condition by itself: it holds a marker (no folders), or it is one
        // of the rule's folders and its parent holds a marker.
        bool entersRule(const CompiledRule& r, std::string_view name, DirectoryMarkers m) {
            if (r.folders.empty()) {
                return (m.own & r.markerMask) != 0;
            }
            return (m.parent & r.markerMask) != 0 &&
                std::ranges::any_of(r.folders, [&](const std::string& f) { return globMatch(f, name); });
        }

    } // namespace

    // The automaton, the extension table, the rules and the overrides, ready to use.
    struct RuleEngine::Compiled {
        RuleSet set; // As given; rules point into it.
        PathAutomaton automaton; // All path globs.
        std::vector<std::string> extensions; // Sorted, distinct, lower case.
        std::vector<CompiledRule> rules; // In order.
        std::vector<std::vector<std::string>> overridePaths; // Per override: its components.
        MarkerTable markers; // Marker globs of all rules.
        std::vector<std::size_t> markerRules; // Indexes of the rules with markers, in bit order.

        // Marker bits of the rules a directory meets by itself, from the markers among its own and its parent's
        // entries; the caller adds its parent's result.
        [[nodiscard]] std::uint64_t entered(std::string_view name, DirectoryMarkers m) const {
            std::uint64_t out = 0;
            if ((m.own | m.parent) == 0) {
                return out;
            }
            for (const std::size_t i : markerRules) {
                out |= entersRule(rules[i], name, m) ? rules[i].markerBit : 0;
            }
            return out;
        }

        // Index of an extension in the table, or noExtension.
        [[nodiscard]] std::uint16_t extensionId(std::string_view name) const {
            const std::string ext = extensionOf(name);
            const auto it = std::ranges::lower_bound(extensions, ext);
            return ext.empty() || it == extensions.end() || *it != ext
                ? noExtension
                : static_cast<std::uint16_t>(it - extensions.begin());
        }

        // The first rule matching a file, or the fallback.
        [[nodiscard]] ClassId firstMatch(const FileView& v) const {
            for (const CompiledRule& r : rules) {
                if (factsMatch(*r.source, v) && extensionMatches(r, v) && markersMatch(r, v) && pathMatches(r, v)) {
                    return r.target;
                }
            }
            return set.fallback;
        }
    };

    namespace {

        // Throws unless a class id names a class.
        void checkClass(ClassId id, const RuleSet& set, std::string_view what) {
            if (id >= set.classNames.size()) {
                throw RuleError(
                    std::format("{} names class {}, but there are {} classes", what, id, set.classNames.size()));
            }
        }

        // The extension table: every rule extension, lower case, sorted, distinct.
        std::vector<std::string> extensionTable(const RuleSet& set) {
            std::vector<std::string> out;
            for (const Rule& r : set.rules) {
                for (const std::string& e : r.extensions) {
                    out.push_back(lowered(e.starts_with('.') ? std::string_view(e).substr(1) : std::string_view(e)));
                }
            }
            std::ranges::sort(out);
            out.erase(std::ranges::unique(out).begin(), out.end());
            if (out.size() >= noExtension) {
                throw RuleError("too many extensions");
            }
            return out;
        }

        // A rule's marker and folder condition, numbering its bit. Throws RuleError for folders without markers, globs
        // of more than one name or too many rules with markers.
        void compileMarkers(const Rule& r, std::size_t index, CompiledRule& out, RuleEngine::Compiled& c) {
            if (r.markers.empty()) {
                if (!r.folders.empty()) {
                    throw RuleError(std::format("rule \"{}\": folders need markers (use paths otherwise)", r.name));
                }
                return;
            }
            if (c.markerRules.size() == maxMarkerBits) {
                throw RuleError(std::format("more than {} rules with markers", maxMarkerBits));
            }
            out.markerBit = std::uint64_t{1} << c.markerRules.size();
            c.markerRules.push_back(index);
            for (const std::string& m : r.markers) {
                checkComponent(m, "marker");
                out.markerMask |= c.markers.bitOf(m);
            }
            for (const std::string& f : r.folders) {
                checkComponent(f, "folder");
                out.folders.push_back(lowered(f));
            }
        }

        // A rule's extensions as a bitset over the table.
        std::vector<std::uint64_t> extensionBits(const Rule& r, const RuleEngine::Compiled& c) {
            if (r.extensions.empty()) {
                return {};
            }
            std::vector<std::uint64_t> bits((c.extensions.size() + 63) / 64, 0);
            for (const std::string& e : r.extensions) {
                const std::uint16_t id = c.extensionId("x." + std::string(e.starts_with('.') ? e.substr(1) : e));
                bits[id / 64] |= std::uint64_t{1} << (id % 64);
            }
            return bits;
        }

    } // namespace

    RuleEngine::RuleEngine(RuleSet rules) : compiled_(std::make_unique<Compiled>()) {
        Compiled& c = *compiled_;
        c.set = std::move(rules);
        if (c.set.classNames.empty() || c.set.classNames.size() > maxClasses) {
            throw RuleError(std::format("a rule set needs 1 to {} classes", maxClasses));
        }
        checkClass(c.set.fallback, c.set, "the fallback");
        c.extensions = extensionTable(c.set);
        for (const Rule& r : c.set.rules) {
            checkClass(r.target, c.set, std::format("rule \"{}\"", r.name));
            CompiledRule compiled{.target = r.target,
                                  .accepts = {},
                                  .extensions = extensionBits(r, c),
                                  .markerBit = 0,
                                  .markerMask = 0,
                                  .folders = {},
                                  .source = &r};
            compileMarkers(r, c.rules.size(), compiled, c);
            for (const std::string& p : r.paths) {
                compiled.accepts.push_back(c.automaton.add(p));
            }
            c.rules.push_back(std::move(compiled));
        }
        for (const DirectoryOverride& o : c.set.overrides) {
            checkClass(o.target, c.set, std::format("the override of \"{}\"", o.path));
            std::vector<std::string> parts;
            for (const std::string_view part : components(o.path)) {
                parts.emplace_back(part);
            }
            c.overridePaths.push_back(std::move(parts));
        }
    }

    RuleEngine::~RuleEngine() = default;
    RuleEngine::RuleEngine(RuleEngine&&) noexcept = default;
    RuleEngine& RuleEngine::operator=(RuleEngine&&) noexcept = default;

    const RuleSet& RuleEngine::rules() const { return compiled_->set; }

    namespace {

        // The deepest override whose directory holds a file below the given directory components, or noOverride.
        std::uint16_t overrideFor(std::span<const std::string_view> dirs,
                                  const std::vector<std::vector<std::string>>& overrides) {
            std::uint16_t best = noOverride; // The first of equally deep ones wins, as in a snapshot.
            for (std::size_t i = 0; i < overrides.size(); ++i) {
                const auto& o = overrides[i];
                const bool deeper = best == noOverride || o.size() > overrides[best].size();
                const bool below = deeper && o.size() <= dirs.size() &&
                    std::ranges::equal(o, dirs.first(o.size()),
                                       [](const std::string& a, std::string_view b) { return sameName(a, b); });
                best = below ? static_cast<std::uint16_t>(i) : best;
            }
            return best;
        }

    } // namespace

    namespace {

        // One of the given entries whose name is a marker.
        struct MarkerEntry {
            std::vector<std::string_view> parts; // Its path components.
            std::uint64_t bits = 0; // Its marker bits.
        };

        // The entries that are markers; the others cannot change a class, so they are not even split.
        std::vector<MarkerEntry> markerEntries(const RuleEngine::Compiled& c, std::span<const std::string> entries) {
            std::vector<MarkerEntry> out;
            for (const std::string& e : entries) {
                const std::size_t slash = e.find_last_of("/\\");
                const std::uint64_t bits =
                    c.markers.of(std::string_view(e).substr(slash == std::string::npos ? 0 : slash + 1));
                if (bits != 0) {
                    out.push_back({.parts = components(e), .bits = bits});
                }
            }
            return out;
        }

        // Marker bits of the entries directly in the directory made of a path's first `depth` components: the marker
        // entries there, and the path's own next component.
        std::uint64_t markersAt(const RuleEngine::Compiled& c, std::span<const std::string_view> parts,
                                std::size_t depth, std::span<const MarkerEntry> entries) {
            const auto prefix = parts.first(depth);
            std::uint64_t bits = depth < parts.size() ? c.markers.of(parts[depth]) : 0;
            for (const MarkerEntry& e : entries) {
                const bool here = e.parts.size() == depth + 1 &&
                    std::ranges::equal(std::span(e.parts).first(depth), prefix, sameName);
                bits |= here ? e.bits : 0;
            }
            return bits;
        }

        // FileView::inside of a path: the marker rules met by any directory from the root down to the file's own
        // directory (or the file itself when it is a directory).
        std::uint64_t insideOfPath(const RuleEngine::Compiled& c, std::span<const std::string_view> parts,
                                   bool directory, std::span<const std::string> entries) {
            const std::vector<MarkerEntry> markerList = markerEntries(c, entries);
            const std::size_t own = parts.empty() || directory ? parts.size() : parts.size() - 1;
            std::uint64_t inside = 0;
            std::uint64_t parentMarkers = 0;
            for (std::size_t depth = 0; depth <= own; ++depth) {
                const std::uint64_t markers = markersAt(c, parts, depth, markerList);
                inside |= c.entered(depth == 0 ? std::string_view() : parts[depth - 1],
                                    {.own = markers, .parent = parentMarkers});
                parentMarkers = markers;
            }
            return inside;
        }

    } // namespace

    ClassId RuleEngine::classifyPath(std::string_view path, const FileFacts& facts, std::int64_t now,
                                     std::span<const std::string> entries) const {
        const Compiled& c = *compiled_;
        const std::vector<std::string_view> parts = components(path);
        StateSet states = c.automaton.initial();
        for (const std::string_view part : parts) {
            states = c.automaton.step(states, part);
        }
        const std::string_view name = parts.empty() ? std::string_view() : parts.back();
        const bool directory = (facts.flags & FileDirectory) != 0;
        const std::uint16_t o = parts.empty() || directory
            ? noOverride
            : overrideFor(std::span(parts).first(parts.size() - 1), c.overridePaths);
        if (o != noOverride) {
            return c.set.overrides[o].target;
        }
        const FileView v{.sizeBytes = facts.sizeBytes,
                         .ageDays = ageInDays(facts.modified, now),
                         .flags = facts.flags,
                         .extension = c.extensionId(name),
                         .churn = facts.churn,
                         .states = &states,
                         .inside = c.markerRules.empty() ? 0 : insideOfPath(c, parts, directory, entries)};
        return c.firstMatch(v);
    }

    namespace {

        // One classification of a snapshot.
        class SnapshotClassifier {
        public:
            SnapshotClassifier(const RuleEngine::Compiled& c, const Snapshot& s, const ClassifyInputs& inputs,
                               Executor& executor, const CancelToken& token) :
                c_(c), s_(s), inputs_(inputs), executor_(executor), token_(token) {}

            // Directory states and overrides from the root down, then every file.
            std::vector<ClassId> run() {
                nameExtensions();
                collectMarkers();
                directories();
                std::vector<ClassId> out(s_.files.size(), 0);
                executor_.parallelFor(
                    {out.size(), fileGrain},
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t f = begin; f < end; ++f) {
                            out[f] = classOf(static_cast<FileIndex>(f));
                        }
                    },
                    token_);
                return out;
            }

        private:
            // The extension id of every distinct name, once.
            void nameExtensions() {
                extensionOfName_.assign(s_.names.size(), noExtension);
                executor_.parallelFor(
                    {extensionOfName_.size(), fileGrain},
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t n = begin; n < end; ++n) {
                            extensionOfName_[n] = c_.extensionId(s_.names.name(static_cast<NameId>(n)));
                        }
                    },
                    token_);
                if (!usesMarkers()) {
                    return;
                }
                markerName_.assign(s_.names.size(), 0);
                executor_.parallelFor(
                    {markerName_.size(), fileGrain},
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t n = begin; n < end; ++n) {
                            markerName_[n] = c_.markers.of(s_.names.name(static_cast<NameId>(n))) != 0 ? 1 : 0;
                        }
                    },
                    token_);
            }

            // Whether any rule has markers; without them none of the marker tables is built.
            [[nodiscard]] bool usesMarkers() const { return !c_.markerRules.empty(); }

            // The marker bits of every directory that holds a marker, from one pass over the files, sorted by
            // directory so each directory needs one lookup.
            void collectMarkers() {
                if (!usesMarkers()) {
                    return;
                }
                const ChunkedRange range{s_.files.size(), fileGrain};
                std::vector<std::vector<std::pair<FileIndex, std::uint64_t>>> parts(range.chunks());
                executor_.parallelFor(
                    range,
                    [&](std::size_t begin, std::size_t end) {
                        for (std::size_t f = begin; f < end; ++f) {
                            addMarker(static_cast<FileIndex>(f), parts[begin / fileGrain]);
                        }
                    },
                    token_);
                for (const auto& part : parts) {
                    markersOf_.insert(markersOf_.end(), part.begin(), part.end());
                }
                std::ranges::sort(markersOf_);
                mergeMarkers();
            }

            // Records a file's marker bits under its directory when its name is a marker.
            void addMarker(FileIndex f, std::vector<std::pair<FileIndex, std::uint64_t>>& out) const {
                const NameId name = s_.files.name(f);
                const FileIndex parent = s_.files.parent(f);
                if (markerName_[name] != 0 && parent != noFile && parent != f) {
                    out.emplace_back(parent, c_.markers.of(s_.names.name(name)));
                }
            }

            // Joins the entries of one directory into one.
            void mergeMarkers() {
                std::size_t out = 0;
                for (std::size_t i = 0; i < markersOf_.size(); ++i) {
                    if (out > 0 && markersOf_[out - 1].first == markersOf_[i].first) {
                        markersOf_[out - 1].second |= markersOf_[i].second;
                    }
                    else {
                        markersOf_[out++] = markersOf_[i];
                    }
                }
                markersOf_.resize(out);
            }

            // The marker bits of a directory's entries.
            [[nodiscard]] std::uint64_t markersIn(FileIndex d) const {
                const auto it =
                    std::ranges::lower_bound(markersOf_, d, {}, &std::pair<FileIndex, std::uint64_t>::first);
                return it != markersOf_.end() && it->first == d ? it->second : 0;
            }

            // Directories in levels from the roots; each level's states and overrides from its parents', in parallel.
            void directories() {
                slotOf_.assign(s_.files.size(), noSlot);
                std::vector<FileIndex> level;
                for (const FileIndex root : s_.tree.roots()) {
                    if (s_.files.has(root, FileDirectory)) {
                        level.push_back(root);
                    }
                }
                rootOverrides(level);
                while (!level.empty()) {
                    const std::size_t first = place(level);
                    executor_.parallelFor(
                        {level.size(), 1024},
                        [&](std::size_t begin, std::size_t end) {
                            for (std::size_t i = begin; i < end; ++i) {
                                describeDirectory(first + i);
                            }
                        },
                        token_);
                    level = nextLevel(level);
                }
            }

            // Override ids of directories named by overrides, found by walking names from the roots.
            void rootOverrides(const std::vector<FileIndex>& roots) {
                for (std::size_t i = 0; i < c_.overridePaths.size(); ++i) {
                    for (const FileIndex root : roots) {
                        const FileIndex d = find(root, c_.overridePaths[i]);
                        if (d != noFile) {
                            ownOverride_.emplace_back(d, static_cast<std::uint16_t>(i));
                        }
                    }
                }
                std::ranges::sort(ownOverride_);
            }

            // The directory at a path below a root, or noFile.
            [[nodiscard]] FileIndex find(FileIndex root, const std::vector<std::string>& parts) const {
                FileIndex at = root;
                for (const std::string& part : parts) {
                    const auto children = s_.tree.children(at);
                    const auto it = std::ranges::find_if(children, [&](FileIndex child) {
                        return s_.files.has(child, FileDirectory) &&
                            sameName(s_.names.name(s_.files.name(child)), part);
                    });
                    if (it == children.end()) {
                        return noFile;
                    }
                    at = *it;
                }
                return at;
            }

            // Gives a level's directories their slots; returns the first.
            std::size_t place(const std::vector<FileIndex>& level) {
                const std::size_t first = overrides_.size();
                for (std::size_t i = 0; i < level.size(); ++i) {
                    slotOf_[level[i]] = static_cast<std::uint32_t>(first + i);
                }
                slotFiles_.insert(slotFiles_.end(), level.begin(), level.end());
                states_.resize((first + level.size()) * c_.automaton.words());
                overrides_.resize(first + level.size(), noOverride);
                if (usesMarkers()) {
                    markers_.resize(first + level.size(), 0);
                    inside_.resize(first + level.size(), 0);
                }
                return first;
            }

            // The directories below a level, in level then child order.
            [[nodiscard]] std::vector<FileIndex> nextLevel(const std::vector<FileIndex>& level) const {
                std::vector<FileIndex> next;
                for (const FileIndex d : level) {
                    for (const FileIndex child : s_.tree.children(d)) {
                        if (s_.files.has(child, FileDirectory) && slotOf_[child] == noSlot) {
                            next.push_back(child);
                        }
                    }
                }
                return next;
            }

            // A directory's automaton state and override, from its parent's.
            void describeDirectory(std::size_t slot) {
                const FileIndex d = slotFiles_[slot];
                const StateSet states = ownStates(d);
                std::ranges::copy(std::span(states).first(c_.automaton.words()),
                                  states_.begin() + static_cast<std::ptrdiff_t>(slot * c_.automaton.words()));
                const auto own = std::ranges::lower_bound(ownOverride_, std::pair(d, std::uint16_t{0}));
                const bool named = own != ownOverride_.end() && own->first == d;
                const FileIndex parent = s_.files.parent(d);
                const std::uint32_t parentSlot = parent == noFile ? noSlot : slotOf_[parent];
                overrides_[slot] = named ? own->second : (parentSlot == noSlot ? noOverride : overrides_[parentSlot]);
                if (usesMarkers()) {
                    describeMarkers(slot, d);
                }
            }

            // A directory's marker bits and the marker rules it is inside of, from its parent's.
            void describeMarkers(std::size_t slot, FileIndex d) {
                const FileIndex parent = s_.files.parent(d);
                const std::uint32_t parentSlot = parent == noFile || parent == d ? noSlot : slotOf_[parent];
                const bool hasParent = parentSlot != noSlot;
                markers_[slot] = markersIn(d);
                const std::string_view name = hasParent ? s_.names.name(s_.files.name(d)) : std::string_view();
                inside_[slot] = (hasParent ? inside_[parentSlot] : 0) |
                    c_.entered(name, {.own = markers_[slot], .parent = hasParent ? markers_[parentSlot] : 0});
            }

            // FileView::inside of a file: its own slot's for a directory, else its directory's.
            [[nodiscard]] std::uint64_t insideOf(FileIndex f) const {
                if (!usesMarkers()) {
                    return 0;
                }
                const FileIndex parent = s_.files.parent(f);
                const std::uint32_t own = slotOf_[f];
                const std::uint32_t slot = own != noSlot ? own : (parent == noFile ? noSlot : slotOf_[parent]);
                return slot == noSlot ? 0 : inside_[slot];
            }

            // The automaton state of a directory's parent (the root state for roots and unreachable parents).
            [[nodiscard]] StateSet parentStates(FileIndex f) const {
                const FileIndex parent = s_.files.parent(f);
                const std::uint32_t slot = parent == noFile || parent == f ? noSlot : slotOf_[parent];
                StateSet out = c_.automaton.initial();
                if (slot != noSlot) {
                    std::ranges::copy_n(states_.begin() + static_cast<std::ptrdiff_t>(slot * c_.automaton.words()),
                                        static_cast<std::ptrdiff_t>(c_.automaton.words()), out.begin());
                }
                return out;
            }

            // The automaton state after a file's own name (roots: the root state).
            [[nodiscard]] StateSet ownStates(FileIndex f) const {
                const FileIndex parent = s_.files.parent(f);
                if (parent == noFile || parent == f || c_.automaton.empty()) {
                    return c_.automaton.initial();
                }
                return c_.automaton.step(parentStates(f), s_.names.name(s_.files.name(f)));
            }

            // The class of one file: an override of its directory for regular files, else the first matching rule.
            [[nodiscard]] ClassId classOf(FileIndex f) const {
                const auto& files = s_.files;
                const FileIndex parent = files.parent(f);
                const std::uint32_t parentSlot = parent == noFile ? noSlot : slotOf_[parent];
                const std::uint16_t o = parentSlot == noSlot ? noOverride : overrides_[parentSlot];
                if (o != noOverride && !files.has(f, FileDirectory)) {
                    return c_.set.overrides[o].target;
                }
                const StateSet states = ownStates(f);
                const FileView v{.sizeBytes = files.sizeBytes(f),
                                 .ageDays = ageInDays(files.times(f).modified, inputs_.now),
                                 .flags = files.flags(f),
                                 .extension = extensionOfName_[files.name(f)],
                                 .churn = !inputs_.churn.empty() && inputs_.churn[f] != 0,
                                 .states = &states,
                                 .inside = insideOf(f)};
                return c_.firstMatch(v);
            }

            const RuleEngine::Compiled& c_; // The rules.
            const Snapshot& s_; // The files.
            const ClassifyInputs& inputs_; // Time and churn.
            Executor& executor_; // Parallel loops.
            const CancelToken& token_; // Checked by the loops.
            std::vector<std::uint16_t> extensionOfName_; // Per name id.
            std::vector<std::uint32_t> slotOf_; // File -> directory slot.
            std::vector<FileIndex> slotFiles_; // Directory slot -> file.
            std::vector<std::uint64_t> states_; // Per directory slot: automaton state words.
            std::vector<std::uint16_t> overrides_; // Per directory slot: override in force.
            std::vector<std::pair<FileIndex, std::uint16_t>> ownOverride_; // Directories named by overrides.
            std::vector<std::uint8_t> markerName_; // Per name id: 1 when the name is a marker.
            std::vector<std::pair<FileIndex, std::uint64_t>> markersOf_; // Directories holding markers, sorted.
            std::vector<std::uint64_t> markers_; // Per directory slot: marker bits of its entries.
            std::vector<std::uint64_t> inside_; // Per directory slot: marker rules it is inside of.
        };

    } // namespace

    std::vector<ClassId> RuleEngine::classify(const Snapshot& snapshot, const ClassifyInputs& inputs,
                                              Executor& executor, const CancelToken& token) const {
        if (!inputs.churn.empty() && inputs.churn.size() != snapshot.files.size()) {
            throw RuleError("the churn flags do not match the snapshot's files");
        }
        return SnapshotClassifier(*compiled_, snapshot, inputs, executor, token).run();
    }

    std::string extensionOf(std::string_view name) {
        const std::size_t dot = name.rfind('.');
        if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
            return {};
        }
        return lowered(name.substr(dot + 1));
    }

} // namespace icd::core::classify
