#include "core/planner/ZoneAssignment.h"

#include "core/classify/RuleEngine.h"

#include <algorithm>
#include <memory>
#include <optional>

namespace icd::core::planner {

    namespace {

        // FILETIME ticks per day.
        constexpr std::int64_t ticksPerDay = 864'000'000'000;

        // Flags that pin a file where it is (plan.md 10.2: encrypted files move only after passing on the test bed).
        constexpr std::uint16_t pinningFlags = FileUnmovable | FileMetadata | FileResident | FileEncrypted;

        // One zone's selector, compiled for per-file tests.
        struct CompiledSelector {
            const strategy::ZoneSelector* select = nullptr; // The source.
            std::vector<bool> classes; // Per catalog class: selected; empty when any class is.
            std::vector<classify::ClassId> patternMatch; // Per file: 1 when the globs and extensions match; empty: any.
        };

        // The class table of a selector.
        std::vector<bool> classTable(const strategy::ZoneSelector& s, const classify::Catalog& catalog) {
            if (s.classes.empty()) {
                return {};
            }
            std::vector<bool> table(catalog.classes.size(), false);
            for (const std::string& id : s.classes) {
                const auto found = catalog.find(id);
                if (found) {
                    table[*found] = true;
                }
            }
            return table;
        }

        // Per-file pattern results of a selector's globs and extensions, or empty when it has none.
        std::vector<classify::ClassId> patternResults(const strategy::ZoneSelector& s, const Snapshot& snapshot,
                                                      const AssignInputs& inputs, Executor& executor,
                                                      const CancelToken& token) {
            if (s.paths.empty() && s.extensions.empty()) {
                return {};
            }
            classify::RuleSet rules;
            rules.classNames = {"other", "zone"};
            rules.rules.push_back({.name = "zone", .target = 1, .paths = s.paths, .extensions = s.extensions});
            const classify::RuleEngine engine(std::move(rules));
            return engine.classify(snapshot, {.now = inputs.now, .churn = {}}, executor, token);
        }

        // Age in whole days at `now`; files from the future count as new.
        std::uint32_t ageDays(std::int64_t now, std::int64_t modified) {
            const std::int64_t days = now > modified ? (now - modified) / ticksPerDay : 0;
            return static_cast<std::uint32_t>(std::min<std::int64_t>(days, std::numeric_limits<std::uint32_t>::max()));
        }

        // The size, age and flag conditions of a selector.
        bool factsMatch(const strategy::ZoneSelector& s, const Snapshot& snapshot, FileIndex file, std::int64_t now) {
            const std::uint64_t size = snapshot.files.sizeBytes(file);
            const std::uint32_t age = ageDays(now, snapshot.files.times(file).modified);
            const std::uint16_t flags = snapshot.files.flags(file);
            return size >= s.minSize && size < s.maxSize && age >= s.minAgeDays && age < s.maxAgeDays &&
                (flags & s.flagsAll) == s.flagsAll && (flags & s.flagsNone) == 0;
        }

        // The churn condition of a selector.
        bool churnMatches(classify::ChurnTest test, const AssignInputs& inputs, FileIndex file) {
            const bool churns = file < inputs.churn.size() && inputs.churn[file] != 0;
            return test == classify::ChurnTest::Any || (test == classify::ChurnTest::Churning) == churns;
        }

        // Whether a compiled selector takes a file.
        bool selects(const CompiledSelector& c, const Snapshot& snapshot, const AssignInputs& inputs, FileIndex file) {
            const classify::ClassId cls = file < inputs.classes.size() ? inputs.classes[file] : 0;
            const bool classOk = c.classes.empty() || (cls < c.classes.size() && c.classes[cls]);
            const bool patternOk = c.patternMatch.empty() || c.patternMatch[file] != 0;
            return classOk && patternOk && churnMatches(c.select->churn, inputs, file) &&
                factsMatch(*c.select, snapshot, file, inputs.now);
        }

        // The role of a file in a zone (or in none).
        FileRole roleOf(const strategy::ZoneScript& script, ZoneIndex zone, bool fixed) {
            if (fixed) {
                return FileRole::Fixed;
            }
            if (zone == noZone) {
                return FileRole::Defragment;
            }
            switch (script.zones[zone].action) {
            case strategy::ZoneAction::KeepInPlace:
                return FileRole::InPlace;
            case strategy::ZoneAction::DefragmentOnly:
                return FileRole::Defragment;
            default:
                return FileRole::Placed;
            }
        }

        // Clusters of a file's movable extents.
        ClusterCount movableClusters(const Snapshot& snapshot, FileIndex file) {
            ClusterCount sum = 0;
            const auto records = snapshot.extents.records();
            for (const std::uint32_t position : snapshot.extents.extentsOf(file)) {
                const ExtentRecord& r = records[position];
                sum += (r.flags() & ExtentUnmovable) != 0 ? 0 : r.length();
            }
            return sum;
        }

    } // namespace

    bool fixedFile(const Snapshot& snapshot, FileIndex file, ClusterCount movable) {
        return movable == 0 || (snapshot.files.flags(file) & pinningFlags) != 0;
    }

    ZoneAssignment assignZones(const Snapshot& snapshot, const strategy::ZoneScript& script,
                               const classify::Catalog& catalog, const AssignInputs& inputs, Executor& executor,
                               const CancelToken& token) {
        std::vector<CompiledSelector> selectors;
        selectors.reserve(script.zones.size());
        for (const strategy::Zone& zone : script.zones) {
            selectors.push_back({.select = &zone.select,
                                 .classes = classTable(zone.select, catalog),
                                 .patternMatch = patternResults(zone.select, snapshot, inputs, executor, token)});
        }
        const std::size_t files = snapshot.files.size();
        ZoneAssignment out{.zoneOf = std::vector<ZoneIndex>(files, noZone),
                           .roles = std::vector<FileRole>(files, FileRole::Fixed),
                           .movable = std::vector<ClusterCount>(files, 0),
                           .zoneClusters = {}};
        executor.parallelFor(
            {.count = files, .grain = 8192},
            [&](std::size_t begin, std::size_t end) {
                for (std::size_t f = begin; f < end; ++f) {
                    const auto file = static_cast<FileIndex>(f);
                    const auto it = std::ranges::find_if(
                        selectors, [&](const CompiledSelector& c) { return selects(c, snapshot, inputs, file); });
                    const ZoneIndex zone =
                        it == selectors.end() ? noZone : static_cast<ZoneIndex>(it - selectors.begin());
                    out.movable[f] = movableClusters(snapshot, file);
                    out.zoneOf[f] = zone;
                    out.roles[f] = roleOf(script, zone, fixedFile(snapshot, file, out.movable[f]));
                }
            },
            token);
        out.zoneClusters.assign(script.zones.size(), 0);
        for (std::size_t f = 0; f < files; ++f) {
            if (out.roles[f] == FileRole::Placed) {
                out.zoneClusters[out.zoneOf[f]] += out.movable[f];
            }
        }
        return out;
    }

} // namespace icd::core::planner
