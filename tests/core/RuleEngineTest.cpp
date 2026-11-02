// Rule engine (T070): path globs, rule order, conditions, per-directory overrides, and the same classes for a
// snapshot as for its paths one by one, for any executor.

#include "core/classify/RuleEngine.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <map>
#include <string>

namespace icd::core::classify { namespace {

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // The "now" of the tests.
    constexpr std::int64_t now = 1000 * day;

    // Classes of the hand-made rule sets.
    enum : ClassId { Unknown, Source, Pack, Loose, Deps, Build, Temp, Photo, Big, Old, Churn, Dir, User };

    // Class names in ClassId order.
    std::vector<std::string> names() {
        return {"Unknown", "Source", "Pack", "Loose", "Deps", "Build", "Temp",
                "Photo",   "Big",    "Old",  "Churn", "Dir",  "User"};
    }

    // A rule with paths and extensions.
    Rule rule(ClassId target, std::vector<std::string> paths, std::vector<std::string> extensions = {}) {
        Rule r;
        r.name = target < names().size() ? names()[target] : "bad";
        r.target = target;
        r.paths = std::move(paths);
        r.extensions = std::move(extensions);
        return r;
    }

    // A development-drive rule set in the spirit of plan.md 8.3.
    RuleSet devRules() {
        RuleSet set;
        set.classNames = names();
        Rule dir;
        dir.target = Dir;
        dir.flagsAll = FileDirectory;
        set.rules = {dir,
                     rule(Pack, {"**/.git/objects/pack/*"}, {"pack", "idx"}),
                     rule(Loose, {"**/.git/objects/?\?/*"}), // "?\?" keeps the compiler from reading a trigraph.
                     rule(Deps, {"**/node_modules/**"}),
                     rule(Build, {"**/obj/**", "**/bin/**", "**/CMakeFiles/**"}),
                     rule(Build, {}, {"obj", "pdb", "pch"}),
                     rule(Temp, {"**/~*", "**/.cache/**"}, {}),
                     rule(Temp, {}, {"tmp", "swp"}),
                     rule(Source, {}, {"cpp", "h", "py", "js"}),
                     rule(Photo, {}, {".JPG", "cr3"})};
        set.fallback = Unknown;
        return set;
    }

    // Facts of a plain file.
    FileFacts plain(std::uint64_t size = 1000) { return {.sizeBytes = size, .modified = now - day, .flags = 0}; }

    TEST(RuleEngine, PathGlobsMatchComponents) {
        const RuleEngine engine(devRules());
        const auto at = [&](std::string_view path) { return engine.classifyPath(path, plain(), now); };
        EXPECT_EQ(at("\\repo\\.git\\objects\\pack\\pack-1.pack"), Pack);
        EXPECT_EQ(at("\\repo\\.git\\objects\\pack\\pack-1.idx"), Pack);
        EXPECT_EQ(at("\\repo\\.git\\objects\\pack\\readme.txt"), Unknown); // Path matches, extension does not.
        EXPECT_EQ(at("/repo/.git/objects/ab/cdef0123"), Loose); // Either separator.
        EXPECT_EQ(at("\\repo\\.git\\objects\\abc\\cdef"), Unknown); // `??` is exactly two characters.
        EXPECT_EQ(at("\\NODE_MODULES\\lib\\index.js"), Deps); // Case-insensitive, `**` matches zero parts.
        EXPECT_EQ(at("\\a\\b\\node_modules\\c\\d\\x.cpp"), Deps); // Order: deps before sources.
        EXPECT_EQ(at("\\proj\\obj\\Debug\\main.cpp"), Build);
        EXPECT_EQ(at("\\proj\\objects\\main.cpp"), Source); // "obj" is a whole part.
        EXPECT_EQ(at("\\proj\\~lock.docx"), Temp);
        EXPECT_EQ(at("\\photos\\IMG_0001.JPG"), Photo); // Extensions given with a dot or in capitals still match.
        EXPECT_EQ(at("\\photos\\IMG_0001.CR3"), Photo);
        EXPECT_EQ(at("\\Makefile"), Unknown);
    }

    TEST(RuleEngine, AnchoredGlobsStartAtTheRoot) {
        RuleSet set;
        set.classNames = names();
        set.rules = {rule(User, {"Users/*/Pictures/**"}), rule(Temp, {"*.tmp"})};
        const RuleEngine engine(set);
        const auto at = [&](std::string_view path) { return engine.classifyPath(path, plain(), now); };
        EXPECT_EQ(at("\\Users\\ann\\Pictures\\a.png"), User);
        EXPECT_EQ(at("\\Users\\ann\\Pictures\\2024\\b.png"), User);
        EXPECT_EQ(at("\\Data\\Users\\ann\\Pictures\\a.png"), Unknown);
        EXPECT_EQ(at("\\Users\\Pictures\\a.png"), Unknown); // `*` is exactly one part.
        EXPECT_EQ(at("\\x.tmp"), Temp);
        EXPECT_EQ(at("\\dir\\x.tmp"), Unknown); // Anchored at the root.
    }

    TEST(RuleEngine, FirstMatchingRuleWins) {
        RuleSet set;
        set.classNames = names();
        set.rules = {rule(Source, {}, {"cpp"}), rule(Build, {"**/build/**"})};
        const auto at = [&](const RuleSet& s) { return RuleEngine(s).classifyPath("\\build\\a.cpp", plain(), now); };
        EXPECT_EQ(at(set), Source);
        std::swap(set.rules[0], set.rules[1]);
        EXPECT_EQ(at(set), Build);
    }

    TEST(RuleEngine, SizeAgeFlagAndChurnConditions) {
        RuleSet set;
        set.classNames = names();
        Rule churn;
        churn.target = Churn;
        churn.churn = ChurnTest::Churning;
        Rule big;
        big.target = Big;
        big.minSize = 100 << 20;
        Rule old;
        old.target = Old;
        old.minAgeDays = 30;
        old.flagsNone = FileDirectory;
        Rule recent;
        recent.target = Temp;
        recent.maxAgeDays = 2;
        recent.flagsAll = FileCompressed;
        set.rules = {churn, big, old, recent};
        const RuleEngine engine(set);
        const auto at = [&](FileFacts f) { return engine.classifyPath("\\f", f, now); };
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now, .flags = 0, .churn = true}), Churn);
        EXPECT_EQ(at({.sizeBytes = 100 << 20, .modified = now, .flags = 0}), Big);
        EXPECT_EQ(at({.sizeBytes = (100 << 20) - 1, .modified = now, .flags = 0}), Unknown);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now - 30 * day, .flags = 0}), Old);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now - 29 * day, .flags = 0}), Unknown);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now - 30 * day, .flags = FileDirectory}), Unknown);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now - day, .flags = FileCompressed}), Temp);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now - 2 * day, .flags = FileCompressed}), Unknown);
        EXPECT_EQ(at({.sizeBytes = 1, .modified = now + day, .flags = FileCompressed}), Temp); // Future: new.
    }

    TEST(RuleEngine, ExtensionsOfNames) {
        EXPECT_EQ(extensionOf("a.TAR.GZ"), "gz");
        EXPECT_EQ(extensionOf("Makefile"), "");
        EXPECT_EQ(extensionOf(".gitignore"), "");
        EXPECT_EQ(extensionOf("trailing."), "");
    }

    TEST(RuleEngine, OverridesBeatRulesDeepestFirstAndSkipDirectories) {
        RuleSet set = devRules();
        set.overrides = {{"\\work", User}, {"work/keep/deep", Photo}, {"\\work\\keep", Old}};
        const RuleEngine engine(set);
        const auto at = [&](std::string_view path, std::uint16_t flags = 0) {
            return engine.classifyPath(path, {.sizeBytes = 1, .modified = now, .flags = flags}, now);
        };
        EXPECT_EQ(at("\\work\\a.cpp"), User);
        EXPECT_EQ(at("\\WORK\\keep\\b.cpp"), Old);
        EXPECT_EQ(at("\\work\\keep\\deep\\x\\c.cpp"), Photo);
        EXPECT_EQ(at("\\work\\keep\\sub", FileDirectory), Dir); // Directories keep their rule class.
        EXPECT_EQ(at("\\workshop\\a.cpp"), Source);
        EXPECT_EQ(at("\\work"), Unknown); // A file named like the directory is not below it.
    }

    TEST(RuleEngine, BadRuleSetsAreRefused) {
        RuleSet set = devRules();
        set.rules.push_back(rule(static_cast<ClassId>(names().size()), {}));
        EXPECT_THROW(RuleEngine{set}, RuleError);
        set = devRules();
        set.rules.push_back(rule(Temp, {"a**b"}));
        EXPECT_THROW(RuleEngine{set}, RuleError);
        set = devRules();
        set.rules.push_back(rule(Temp, {"/"}));
        EXPECT_THROW(RuleEngine{set}, RuleError);
        set = devRules();
        set.fallback = 200;
        EXPECT_THROW(RuleEngine{set}, RuleError);
        set = devRules();
        set.classNames.clear();
        EXPECT_THROW(RuleEngine{set}, RuleError);
    }

    // A hand-made development tree.
    Snapshot devTree() {
        SnapshotBuilder b({4096, 1000});
        std::map<std::string, FileIndex> dirs;
        dirs[""] = b.addFile({"", noFile, 0, FileDirectory, {}, 5});
        const auto dir = [&](const std::string& parent, const std::string& name) {
            dirs[parent + "\\" + name] = b.addFile({name, dirs.at(parent), 0, FileDirectory, {}, 0});
        };
        const auto file = [&](const std::string& parent, const std::string& name) {
            b.addFile({name, dirs.at(parent), 10, 0, {.created = 0, .modified = now - day, .accessed = 0}, 0});
        };
        dir("", "repo");
        dir("\\repo", ".git");
        dir("\\repo\\.git", "objects");
        dir("\\repo\\.git\\objects", "pack");
        dir("\\repo\\.git\\objects", "ab");
        dir("\\repo", "node_modules");
        dir("\\repo", "obj");
        dir("", "work");
        dir("\\work", "keep");
        file("\\repo\\.git\\objects\\pack", "p.pack");
        file("\\repo\\.git\\objects\\ab", "cdef");
        file("\\repo\\node_modules", "index.js");
        file("\\repo\\obj", "main.obj");
        file("\\repo", "main.cpp");
        file("\\repo", "~tmp.docx");
        file("\\work", "a.cpp");
        file("\\work\\keep", "b.cpp");
        file("", "IMG.jpg");
        return std::move(b).build();
    }

    // Expects classify() to give every file of a snapshot what classifyPath() gives its path.
    void expectSnapshotEqualsPaths(const RuleEngine& engine, const Snapshot& s, std::span<const std::uint8_t> churn) {
        InlineExecutor inline_;
        const auto classes = engine.classify(s, {.now = now, .churn = churn}, inline_);
        ASSERT_EQ(classes.size(), s.files.size());
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            const FileFacts facts{.sizeBytes = s.files.sizeBytes(f),
                                  .modified = s.files.times(f).modified,
                                  .flags = s.files.flags(f),
                                  .churn = !churn.empty() && churn[f] != 0};
            ASSERT_EQ(classes[f], engine.classifyPath(s.path(f), facts, now)) << s.path(f);
        }
    }

    TEST(RuleEngine, SnapshotClassesEqualPathClasses) {
        RuleSet set = devRules();
        set.overrides = {{"\\work", User}, {"\\work\\keep", Old}, {"\\missing", Photo}};
        const RuleEngine engine(set);
        const Snapshot s = devTree();
        expectSnapshotEqualsPaths(engine, s, {});
        InlineExecutor inline_;
        const auto classes = engine.classify(s, {.now = now, .churn = {}}, inline_);
        std::map<std::string, ClassId> byPath;
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            byPath[s.path(f)] = classes[f];
        }
        EXPECT_EQ(byPath["\\repo\\.git\\objects\\pack\\p.pack"], Pack);
        EXPECT_EQ(byPath["\\repo\\.git\\objects\\ab\\cdef"], Loose);
        EXPECT_EQ(byPath["\\repo\\node_modules\\index.js"], Deps);
        EXPECT_EQ(byPath["\\repo\\obj\\main.obj"], Build);
        EXPECT_EQ(byPath["\\repo\\main.cpp"], Source);
        EXPECT_EQ(byPath["\\repo\\~tmp.docx"], Temp);
        EXPECT_EQ(byPath["\\work\\a.cpp"], User);
        EXPECT_EQ(byPath["\\work\\keep\\b.cpp"], Old);
        EXPECT_EQ(byPath["\\work\\keep"], Dir);
        EXPECT_EQ(byPath["\\IMG.jpg"], Photo);
    }

    // Rules over the synthetic volume's names (dirN, fN.ext).
    RuleSet syntheticRules() {
        RuleSet set;
        set.classNames = names();
        Rule churn = rule(Churn, {"**/dir1*/**"});
        churn.churn = ChurnTest::Churning;
        Rule big;
        big.target = Big;
        big.minSize = 50 << 20;
        set.rules = {rule(Dir, {"**/dir2?"}),
                     churn,
                     rule(Pack, {"**/dir3*/f1*"}, {"cpp"}),
                     rule(Deps, {"dir4*/**", "**/dir5*/dir6*/**"}),
                     big,
                     rule(Photo, {}, {"jpg"}),
                     rule(Source, {"**/f2*"})};
        set.overrides = {{"dir7", User}};
        return set;
    }

    TEST(RuleEngine, SyntheticVolumeMatchesPathsForAnyExecutor) {
        SynthOptions o;
        o.files = 20'000;
        o.filesPerDirectory = 8;
        o.geometry = {4096, 1ULL << 22U};
        const Snapshot s = generateSyntheticVolume(o);
        std::vector<std::uint8_t> churn(s.files.size());
        for (std::size_t f = 0; f < churn.size(); ++f) {
            churn[f] = f % 3 == 0 ? 1 : 0;
        }
        RuleSet set = syntheticRules();
        // Override the first top-level directory too, whatever it is called.
        const FileIndex root = s.tree.roots().front();
        set.overrides.push_back({std::string(s.names.name(s.files.name(s.tree.children(root).front()))), Old});
        const RuleEngine engine(set);
        expectSnapshotEqualsPaths(engine, s, churn);
        InlineExecutor inline_;
        services::ThreadPool pool(5);
        const auto serial = engine.classify(s, {.now = now, .churn = churn}, inline_);
        EXPECT_EQ(serial, engine.classify(s, {.now = now, .churn = churn}, pool));
        std::map<ClassId, std::size_t> counts;
        for (const ClassId c : serial) {
            ++counts[c];
        }
        EXPECT_GE(counts.size(), 6U); // The rules really fire.
    }

}} // namespace icd::core::classify
