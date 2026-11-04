// Rule engine (T070): path globs, rule order, conditions, per-directory overrides, and the same classes for a
// snapshot as for its paths one by one, for any executor.

#include "core/classify/RuleEngine.h"
#include "core/snapshot/SnapshotBuilder.h"
#include "core/synth/SyntheticVolume.h"
#include "services/exec/ThreadPool.h"

#include <gtest/gtest.h>

#include <format>
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

    // A rule that needs markers, optionally below folders.
    Rule marked(ClassId target, std::vector<std::string> markers, std::vector<std::string> folders = {},
                std::vector<std::string> extensions = {}) {
        Rule r = rule(target, {}, std::move(extensions));
        r.markers = std::move(markers);
        r.folders = std::move(folders);
        return r;
    }

    // Build folders next to their manifests, and sources only inside projects.
    RuleSet markerRules() {
        RuleSet set;
        set.classNames = names();
        Rule dir;
        dir.target = Dir;
        dir.flagsAll = FileDirectory;
        set.rules = {dir, marked(Build, {"Cargo.toml", "*.SLN"}, {"target", "bin*"}), marked(Deps, {"pyvenv.cfg"}),
                     marked(Source, {".git", "Cargo.toml"}, {}, {"cpp", "rs"})};
        return set;
    }

    TEST(RuleEngine, MarkersComeFromTheDirectoryListing) {
        const RuleEngine engine(markerRules());
        const auto at = [&](std::vector<std::string> entries, std::string_view path, std::uint16_t flags = 0) {
            return engine.classifyPath(path, {.sizeBytes = 1, .modified = now, .flags = flags}, now, entries);
        };
        EXPECT_EQ(at({"\\rs\\Cargo.toml"}, "\\rs\\target\\debug\\app.exe"), Build);
        EXPECT_EQ(at({"\\RS\\cargo.TOML"}, "\\rs\\TARGET\\app.exe"), Build); // Names ignore case.
        EXPECT_EQ(at({"\\rs\\README.md"}, "\\rs\\target\\app.exe"), Unknown); // No marker.
        EXPECT_EQ(at({"\\rs\\target\\Cargo.toml"}, "\\rs\\target\\app.exe"), Unknown); // Marker inside, not beside.
        EXPECT_EQ(at({"\\cs\\App.sln"}, "\\cs\\bin64\\x\\app.dll"), Build); // Globs on both sides.
        EXPECT_EQ(at({"\\cs\\App.sln"}, "\\cs\\src\\bin\\app.dll"), Unknown); // The folder's parent holds no marker.
        EXPECT_EQ(at({"\\cs\\App.sln"}, "\\cs\\bin", FileDirectory), Dir);
        EXPECT_EQ(at({"\\cs\\App.sln"}, "\\cs\\bin"), Unknown); // A file named like the folder is not in it.
        EXPECT_EQ(at({"\\py\\env\\pyvenv.cfg"}, "\\py\\env\\Lib\\x.dll"), Deps); // Markers only: in or below.
        EXPECT_EQ(at({"\\py\\env\\pyvenv.cfg"}, "\\py\\other\\x.dll"), Unknown);
        EXPECT_EQ(at({"\\p\\.git"}, "\\p\\a\\b\\c.cpp"), Source);
        EXPECT_EQ(at({"\\.git"}, "\\anything\\c.cpp"), Source); // A marker in the root covers the volume.
        EXPECT_EQ(at({}, "\\p\\a\\c.cpp"), Unknown);
        EXPECT_EQ(at({"\\p\\.git"}, "\\p\\.git"), Unknown); // The marker itself has no source extension.
    }

    TEST(RuleEngine, BadMarkerRulesAreRefused) {
        const auto refused = [](Rule r) {
            RuleSet set = markerRules();
            set.rules.push_back(std::move(r));
            EXPECT_THROW(RuleEngine{set}, RuleError);
        };
        refused(marked(Temp, {}, {"out"})); // Folders need markers.
        refused(marked(Temp, {"a\\b"})); // Markers are one name.
        refused(marked(Temp, {"x"}, {"**"})); // So are folders.
        std::vector<std::string> many;
        for (int i = 0; i < 65; ++i) {
            many.push_back(std::format("m{}", i));
        }
        refused(marked(Temp, many)); // More marker names than mask bits.
        RuleSet set = markerRules();
        for (int i = 0; i < 64; ++i) {
            set.rules.push_back(marked(Temp, {"x"}));
        }
        EXPECT_THROW(RuleEngine{set}, RuleError); // More marker rules than mask bits.
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

    // Expects classify() to give every file of a snapshot what classifyPath() gives its path, with `entries` as the
    // volume's other entries (only the markers among them matter).
    void expectSnapshotEqualsPaths(const RuleEngine& engine, const Snapshot& s, std::span<const std::uint8_t> churn,
                                   const std::vector<std::string>& entries = {}) {
        InlineExecutor inline_;
        const auto classes = engine.classify(s, {.now = now, .churn = churn}, inline_);
        ASSERT_EQ(classes.size(), s.files.size());
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            const FileFacts facts{.sizeBytes = s.files.sizeBytes(f),
                                  .modified = s.files.times(f).modified,
                                  .flags = s.files.flags(f),
                                  .churn = !churn.empty() && churn[f] != 0};
            ASSERT_EQ(classes[f], engine.classifyPath(s.path(f), facts, now, entries)) << s.path(f);
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

    // A hand-made tree with projects: a Cargo crate, a solution with bin folders, a virtual environment.
    Snapshot projectTree() {
        SnapshotBuilder b({4096, 1000});
        std::map<std::string, FileIndex> dirs;
        dirs[""] = b.addFile({"", noFile, 0, FileDirectory, {}, 5});
        const auto dir = [&](const std::string& parent, const std::string& name) {
            dirs[parent + "\\" + name] = b.addFile({name, dirs.at(parent), 0, FileDirectory, {}, 0});
        };
        const auto file = [&](const std::string& parent, const std::string& name) {
            b.addFile({name, dirs.at(parent), 10, 0, {.created = 0, .modified = now - day, .accessed = 0}, 0});
        };
        dir("", "rs");
        dir("\\rs", "target");
        dir("\\rs\\target", "debug");
        dir("\\rs", "src");
        dir("", "cs");
        dir("\\cs", "bin64");
        dir("\\cs", "src");
        dir("\\cs\\src", "bin");
        dir("", "env");
        dir("\\env", "Lib");
        dir("", "plain");
        dir("\\plain", "target");
        file("\\rs\\target\\debug", "app.exe");
        file("\\rs\\src", "main.rs");
        file("\\rs", "Cargo.toml"); // Added after the folders: order must not matter.
        file("\\cs", "App.sln");
        file("\\cs\\bin64", "app.dll");
        file("\\cs\\src\\bin", "app.dll");
        file("\\env\\Lib", "x.dll");
        file("\\env", "pyvenv.cfg");
        file("\\plain\\target", "app.exe");
        file("\\plain", "loose.cpp");
        return std::move(b).build();
    }

    TEST(RuleEngine, SnapshotMarkersEqualPathMarkers) {
        const RuleEngine engine(markerRules());
        const Snapshot s = projectTree();
        std::vector<std::string> entries;
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            entries.push_back(s.path(f));
        }
        expectSnapshotEqualsPaths(engine, s, {}, entries);
        InlineExecutor inline_;
        const auto classes = engine.classify(s, {.now = now, .churn = {}}, inline_);
        std::map<std::string, ClassId> byPath;
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            byPath[s.path(f)] = classes[f];
        }
        EXPECT_EQ(byPath["\\rs\\target\\debug\\app.exe"], Build);
        EXPECT_EQ(byPath["\\rs\\src\\main.rs"], Source);
        EXPECT_EQ(byPath["\\cs\\bin64\\app.dll"], Build);
        EXPECT_EQ(byPath["\\cs\\src\\bin\\app.dll"], Unknown);
        EXPECT_EQ(byPath["\\env\\Lib\\x.dll"], Deps);
        EXPECT_EQ(byPath["\\plain\\target\\app.exe"], Unknown);
        EXPECT_EQ(byPath["\\plain\\loose.cpp"], Unknown);
    }

    TEST(RuleEngine, SyntheticMarkersMatchPathsForAnyExecutor) {
        SynthOptions o;
        o.files = 3000;
        o.filesPerDirectory = 8;
        o.geometry = {4096, 1ULL << 22U};
        const Snapshot s = generateSyntheticVolume(o);
        RuleSet set;
        set.classNames = names();
        set.rules = {marked(Build, {"f1*.cpp"}, {"dir2*"}), marked(Deps, {"f2*.dll"}),
                     marked(Source, {"f4*"}, {"dir3*", "dir5?"}, {"jpg"})};
        const RuleEngine engine(set);
        std::vector<std::string> entries; // The names the markers can match, to keep the path side fast.
        for (FileIndex f = 0; f < s.files.size(); ++f) {
            const std::string_view name = s.names.name(s.files.name(f));
            if (name.starts_with("f1") || name.starts_with("f2") || name.starts_with("f4")) {
                entries.push_back(s.path(f));
            }
        }
        expectSnapshotEqualsPaths(engine, s, {}, entries);
        InlineExecutor inline_;
        services::ThreadPool pool(5);
        const auto serial = engine.classify(s, {.now = now, .churn = {}}, inline_);
        EXPECT_EQ(serial, engine.classify(s, {.now = now, .churn = {}}, pool));
        std::map<ClassId, std::size_t> counts;
        for (const ClassId c : serial) {
            ++counts[c];
        }
        EXPECT_GT(counts[Build], 0U); // Every kind of marker rule fires.
        EXPECT_GT(counts[Deps], 0U);
        EXPECT_GT(counts[Source], 0U);
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
