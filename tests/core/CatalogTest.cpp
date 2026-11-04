// Built-in class catalog (T071, T076a): one test per class of plan.md 8.3-8.6 with typical and edge-case paths,
// project markers per ecosystem with their false positives, and catalog files that must be refused.

#include "core/classify/Catalog.h"

#include <gtest/gtest.h>

#include <format>
#include <set>
#include <string>
#include <vector>

namespace icd::core::classify { namespace {

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // The "now" of the tests.
    constexpr std::int64_t now = 1000 * day;

    // One megabyte.
    constexpr std::uint64_t mb = 1ULL << 20U;

    // The built-in rules, compiled once.
    const RuleEngine& engine() {
        static const RuleEngine e(builtinCatalog().rules);
        return e;
    }

    // The class id of a file under the built-in catalog, on a volume whose other entries are `entries`.
    std::string classIn(const std::vector<std::string>& entries, std::string_view path, std::uint64_t size = 4096,
                        std::uint16_t flags = 0, bool churn = false) {
        const FileFacts facts{.sizeBytes = size, .modified = now - day, .flags = flags, .churn = churn};
        return builtinCatalog().classes[engine().classifyPath(path, facts, now, entries)].id;
    }

    // The class id of a file on a volume without project markers.
    std::string classOf(std::string_view path, std::uint64_t size = 4096, std::uint16_t flags = 0, bool churn = false) {
        return classIn({}, path, size, flags, churn);
    }

    TEST(Catalog, BuiltInParsesWithEveryClassReachable) {
        const Catalog& c = builtinCatalog();
        EXPECT_EQ(c.classes.size(), 17U);
        std::set<ClassId> targets{c.rules.fallback};
        std::set<std::uint32_t> colors;
        for (const Rule& r : c.rules.rules) {
            targets.insert(r.target);
        }
        for (const ClassInfo& info : c.classes) {
            colors.insert(info.color);
            EXPECT_FALSE(info.description.empty()) << info.id;
        }
        EXPECT_EQ(targets.size(), c.classes.size()); // Every class is some rule's target (or the fallback).
        EXPECT_EQ(colors.size(), c.classes.size()); // Every class has its own color.
        EXPECT_EQ(c.find("photos"), ClassId{14});
        EXPECT_FALSE(c.find("nope"));
    }

    TEST(Catalog, Unknown) {
        EXPECT_EQ(classOf("\\Documents\\report.docx"), "unknown");
        EXPECT_EQ(classOf("\\data\\blob"), "unknown");
    }

    TEST(Catalog, NtfsMetadata) {
        EXPECT_EQ(classOf("\\$MFT", mb, FileMetadata | FileUnmovable), "metadata");
        EXPECT_EQ(classOf("\\$Extend\\$UsnJrnl", mb, FileMetadata), "metadata");
        EXPECT_EQ(classOf("\\$Extend\\x.pack", mb, FileMetadata), "metadata"); // Before every other rule.
    }

    TEST(Catalog, Directories) {
        EXPECT_EQ(classOf("\\src", 0, FileDirectory), "directories");
        EXPECT_EQ(classOf("\\repo\\node_modules", 0, FileDirectory), "directories");
        EXPECT_EQ(classOf("\\Pictures\\holiday.jpg", 0, FileDirectory), "directories"); // A folder named like a file.
        EXPECT_EQ(classIn({"\\rs\\Cargo.toml"}, "\\rs\\target", 0, FileDirectory), "directories");
    }

    TEST(Catalog, SourceAndConfigInsideProjects) {
        const std::vector<std::string> git{"\\proj\\.git"};
        EXPECT_EQ(classIn(git, "\\proj\\src\\main.cpp"), "source");
        EXPECT_EQ(classIn(git, "\\proj\\src\\deep\\er\\still.h"), "source"); // Anywhere below the project.
        EXPECT_EQ(classIn(git, "\\proj\\CMakeLists.txt"), "source");
        EXPECT_EQ(classIn(git, "\\proj\\Makefile"), "source");
        EXPECT_EQ(classIn(git, "\\proj\\.gitignore"), "source");
        EXPECT_EQ(classIn(git, "\\proj\\App.SLN"), "source"); // Case-insensitive extension.
        EXPECT_EQ(classIn(git, "\\proj\\web\\app.ts", 8 * 1024), "source"); // TypeScript, not a transport stream.
        EXPECT_EQ(classIn(git, "\\proj\\src\\cache\\lru.cpp"), "source"); // A source folder named "cache".
        EXPECT_EQ(classIn({"\\rs\\Cargo.toml"}, "\\rs\\src\\main.rs"), "source");
        EXPECT_EQ(classIn({"\\ex\\mix.exs"}, "\\ex\\lib\\app.ex"), "source");
        EXPECT_EQ(classIn({"\\erl\\rebar.config"}, "\\erl\\src\\app.erl"), "source");
        EXPECT_EQ(classIn({"\\App\\App.csproj"}, "\\App\\Program.cs"), "source"); // "*.*proj" marker.
        EXPECT_EQ(classIn({"\\a\\.git", "\\b\\x.txt"}, "\\b\\x.txt"), "unknown"); // A sibling's project.
        // Outside a project the same types are not development files.
        EXPECT_EQ(classOf("\\Program Files\\App\\resources\\config.json"), "unknown");
        EXPECT_EQ(classOf("\\Program Files\\App\\ui\\main.js"), "unknown");
        EXPECT_EQ(classOf("\\Documents\\notes.txt"), "unknown");
        EXPECT_EQ(classOf("\\Downloads\\snippet.cpp"), "unknown");
        EXPECT_EQ(classOf("\\Python312\\Lib\\os.py"), "unknown");
    }

    TEST(Catalog, RepositoryData) {
        EXPECT_EQ(classOf("\\repo\\.git\\objects\\pack\\pack-1a2b.pack", 300 * mb), "repository");
        EXPECT_EQ(classOf("\\repo\\.git\\objects\\pack\\pack-1a2b.idx"), "repository");
        EXPECT_EQ(classOf("\\repo\\.git\\objects\\pack\\tmp_pack_x"), "unknown"); // Not a pack yet.
        EXPECT_EQ(classOf("\\backup\\objects\\pack\\a.pack"), "unknown"); // Not inside .git.
    }

    TEST(Catalog, Dependencies) {
        EXPECT_EQ(classOf("\\app\\node_modules\\react\\index.js"), "dependencies");
        EXPECT_EQ(classOf("\\app\\node_modules\\a\\node_modules\\b\\lib\\x.js"), "dependencies");
        EXPECT_EQ(classOf("\\Users\\me\\.nuget\\packages\\newtonsoft.json\\13.0.1\\lib\\x.dll"), "dependencies");
        EXPECT_EQ(classOf("\\py\\venv\\Lib\\site-packages\\numpy\\core.py"), "dependencies");
        EXPECT_EQ(classOf("\\Users\\me\\.cargo\\registry\\src\\serde\\lib.rs"), "dependencies");
        EXPECT_EQ(classIn({"\\app\\package.json"}, "\\app\\node_modules_old\\index.js"), "source"); // Exact name only.
        // Virtual environments by their pyvenv.cfg, whatever the folder is called.
        EXPECT_EQ(classIn({"\\py\\env\\pyvenv.cfg"}, "\\py\\env\\Scripts\\python.exe"), "dependencies");
        EXPECT_EQ(classOf("\\py\\env\\Scripts\\python.exe"), "unknown");
        EXPECT_EQ(classIn({"\\conda\\envs\\ml\\conda-meta"}, "\\conda\\envs\\ml\\DLLs\\x.pyd"), "dependencies");
        // Vendored code and fetched dependencies next to their manifests.
        EXPECT_EQ(classIn({"\\go\\go.mod"}, "\\go\\vendor\\github.com\\x\\y.go"), "dependencies");
        EXPECT_EQ(classIn({"\\php\\composer.json"}, "\\php\\vendor\\autoload.php"), "dependencies");
        EXPECT_EQ(classOf("\\Program Files\\Tool\\vendor\\lib.dll"), "unknown");
        EXPECT_EQ(classIn({"\\ex\\mix.exs"}, "\\ex\\deps\\jason\\lib\\jason.ex"), "dependencies");
        EXPECT_EQ(classIn({"\\cs\\App.sln"}, "\\cs\\packages\\Moq.4.0\\lib\\Moq.dll"), "dependencies");
    }

    TEST(Catalog, BuildFoldersNeedTheirMarkers) {
        // Rust, Maven and friends: target/ next to the manifest; without one it is not build output.
        EXPECT_EQ(classIn({"\\rs\\Cargo.toml"}, "\\rs\\target\\debug\\app.exe", 5 * mb), "build");
        EXPECT_EQ(classIn({"\\jv\\pom.xml"}, "\\jv\\target\\classes\\Main.class"), "build");
        EXPECT_EQ(classOf("\\Games\\target\\level.dat"), "unknown");
        EXPECT_EQ(classIn({"\\rs\\README.md"}, "\\rs\\target\\debug\\app.exe"), "unknown");
        EXPECT_EQ(classIn({"\\rs\\sub\\Cargo.toml"}, "\\rs\\target\\x.rlib"), "unknown"); // Marker one level down.
        // Elixir, Erlang, OCaml.
        EXPECT_EQ(classIn({"\\ex\\mix.exs"}, "\\ex\\_build\\dev\\lib\\app\\ebin\\Elixir.App.beam"), "build");
        EXPECT_EQ(classIn({"\\erl\\rebar.config"}, "\\erl\\_build\\default\\lib\\app\\ebin\\app.app"), "build");
        EXPECT_EQ(classIn({"\\erl\\Makefile"}, "\\erl\\ebin\\app.app"), "build");
        EXPECT_EQ(classIn({"\\ml\\dune-project"}, "\\ml\\_build\\default\\main.exe"), "build");
        EXPECT_EQ(classOf("\\Docs\\_build\\html\\index.html"), "unknown");
        // Node.
        EXPECT_EQ(classIn({"\\web\\package.json"}, "\\web\\dist\\main.js"), "build");
        EXPECT_EQ(classIn({"\\web\\package.json"}, "\\web\\.next\\cache\\webpack\\0.pack"), "build");
        EXPECT_EQ(classIn({"\\web\\package.json"}, "\\web\\.svelte-kit\\output\\x.js"), "build");
        EXPECT_EQ(classOf("\\Program Files\\App\\dist\\main.js"), "unknown");
        // Python.
        EXPECT_EQ(classIn({"\\py\\pyproject.toml"}, "\\py\\build\\lib\\pkg\\mod.py"), "build");
        EXPECT_EQ(classIn({"\\py\\setup.py"}, "\\py\\pkg.egg-info\\PKG-INFO"), "build");
        EXPECT_EQ(classIn({"\\py\\setup.cfg"}, "\\py\\dist\\pkg-1.0.whl"), "build");
        // Gradle.
        EXPECT_EQ(classIn({"\\kt\\build.gradle.kts"}, "\\kt\\build\\classes\\Main.class"), "build");
        EXPECT_EQ(classIn({"\\kt\\settings.gradle"}, "\\kt\\.gradle\\8.5\\fileHashes.bin"), "build");
        // CMake and Meson.
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\build\\app.exe"), "build");
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\build-release\\lib\\a.lib"), "build");
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\cmake-build-debug\\x.o"), "build");
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\out\\build\\x64-Debug\\a.exe"), "build");
        EXPECT_EQ(classIn({"\\mn\\meson.build"}, "\\mn\\builddir\\a.exe"), "build");
        EXPECT_EQ(classOf("\\Games\\build\\data.pak"), "unknown");
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\build2\\a.exe"), "build");
        EXPECT_EQ(classIn({"\\cm\\CMakeLists.txt"}, "\\cm\\builders\\gen.py"), "source"); // Not a build tree.
        // MSBuild.
        EXPECT_EQ(classIn({"\\cs\\App\\App.csproj"}, "\\cs\\App\\bin\\Debug\\net8.0\\App.dll"), "build");
        EXPECT_EQ(classIn({"\\cs\\App\\App.csproj"}, "\\cs\\App\\obj\\project.assets.json"), "build");
        EXPECT_EQ(classIn({"\\cpp\\app.sln"}, "\\cpp\\x64\\Release\\app.exe", 5 * mb), "build");
        EXPECT_EQ(classIn({"\\cpp\\app.vcxproj"}, "\\cpp\\Debug\\app.ilk"), "build");
        EXPECT_EQ(classOf("\\Program Files\\App\\bin\\app.exe"), "unknown");
        EXPECT_EQ(classOf("\\Program Files\\App\\x64\\driver.sys"), "unknown");
        EXPECT_EQ(classOf("\\Photos\\Release\\party.docx"), "unknown");
        EXPECT_EQ(classIn({"\\cs\\App.csproj"}, "\\cs\\binaries\\tool.exe"), "unknown"); // Whole folder names.
        // Haskell, Zig, Swift, Bazel, Dart, Xcode.
        EXPECT_EQ(classIn({"\\hs\\stack.yaml"}, "\\hs\\.stack-work\\dist\\x.o"), "build");
        EXPECT_EQ(classIn({"\\hs\\app.cabal"}, "\\hs\\dist-newstyle\\cache\\plan.json"), "build");
        EXPECT_EQ(classIn({"\\zg\\build.zig"}, "\\zg\\zig-out\\bin\\app.exe"), "build");
        EXPECT_EQ(classIn({"\\zg\\build.zig"}, "\\zg\\.zig-cache\\o\\x\\a.o"), "build");
        EXPECT_EQ(classIn({"\\sw\\Package.swift"}, "\\sw\\.build\\debug\\App"), "build");
        EXPECT_EQ(classIn({"\\bz\\MODULE.bazel"}, "\\bz\\bazel-out\\k8-fastbuild\\bin\\app"), "build");
        EXPECT_EQ(classIn({"\\dt\\pubspec.yaml"}, "\\dt\\.dart_tool\\package_config.json"), "build");
        EXPECT_EQ(classIn({"\\ios\\App.xcodeproj"}, "\\ios\\DerivedData\\App\\Build\\x.o"), "build");
    }

    TEST(Catalog, BuildFoldersWithoutMarkers) {
        EXPECT_EQ(classOf("\\anywhere\\__pycache__\\mod.cpython-312.pyc"), "build");
        EXPECT_EQ(classOf("\\py\\.pytest_cache\\v\\cache\\nodeids"), "build");
        EXPECT_EQ(classOf("\\py\\.mypy_cache\\3.12\\x.json"), "build");
        EXPECT_EQ(classOf("\\py\\.ruff_cache\\0.4\\x"), "build");
        EXPECT_EQ(classOf("\\py\\.tox\\py312\\log\\x.txt"), "build");
        EXPECT_EQ(classOf("\\nb\\.ipynb_checkpoints\\a-checkpoint.ipynb"), "build");
        EXPECT_EQ(classOf("\\proj\\CMakeFiles\\3.30\\CMakeCCompiler.cmake"), "build");
        EXPECT_EQ(classOf("\\infra\\.terraform\\providers\\x.exe"), "build");
        EXPECT_EQ(classOf("\\ng\\.angular\\cache\\18\\x.json"), "build");
        EXPECT_EQ(classOf("\\proj\\.vs\\proj\\v17\\Browse.VC.db"), "build");
    }

    TEST(Catalog, BuildFilesOnlyInsideProjects) {
        const std::vector<std::string> git{"\\proj\\.git"};
        for (const char* name : {"main.obj", "main.o", "app.pch", "std.ifc", "m.pcm", "x.gch", "app.ilk", "app.exp",
                                 "a.tlog", "Main.class", "mod.pyc", "app.beam", "Main.hi", "Main.dyn_o", "m.cmi",
                                 "m.cmx", "tsconfig.tsbuildinfo", "app.pdb", "app.debug"}) {
            EXPECT_EQ(classIn(git, std::string("\\proj\\src\\") + name), "build") << name;
            EXPECT_EQ(classOf(std::string("\\Program Files\\App\\") + name), "unknown") << name;
        }
        EXPECT_EQ(classIn(git, "\\proj\\web\\app.js.map"), "build"); // Source maps.
        EXPECT_EQ(classIn(git, "\\proj\\web\\site.css.map"), "build");
        EXPECT_EQ(classIn(git, "\\proj\\App.dSYM\\Contents\\Info.plist"), "build");
        EXPECT_EQ(classOf("\\Games\\maps\\level1.map"), "unknown"); // A game map is not a source map.
        EXPECT_EQ(classIn(git, "\\proj\\lib\\zlib.lib"), "unknown"); // Built or shipped: no guess.
        EXPECT_EQ(classIn(git, "\\proj\\lib\\libz.a"), "unknown");
        EXPECT_EQ(classIn(git, "\\proj\\src\\main.d"), "unknown"); // D source or make dependencies: no guess.
    }

    TEST(Catalog, Temp) {
        EXPECT_EQ(classOf("\\work\\file.tmp"), "temp");
        EXPECT_EQ(classOf("\\work\\~$report.docx"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Temp\\setup.log"), "temp"); // Temp folders beat logs.
        EXPECT_EQ(classOf("\\Windows\\Temp\\x.etl"), "temp");
        EXPECT_EQ(classOf("\\repo\\.git\\objects\\ab\\cdef0123456789"), "temp"); // Loose objects churn.
        EXPECT_EQ(classOf("\\home\\.cache\\pip\\x.whl"), "temp");
        EXPECT_EQ(classOf("\\data\\blob", 4096, 0, true), "temp"); // Churn from the journal.
        EXPECT_EQ(classOf("\\work\\template.docx"), "unknown");
        EXPECT_EQ(classOf("\\work\\temperature.csv"), "unknown");
        // Editor and merge leftovers, partial downloads.
        for (const char* name : {"main.cpp~", "#main.c#", ".#main.c", "a.c.orig", "a.c.rej", ".DS_Store",
                                 "setup.exe.crdownload", "movie.mkv.part", "x.partial"}) {
            EXPECT_EQ(classOf(std::string("\\work\\") + name), "temp") << name;
        }
        // Browser and app caches.
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Google\\Chrome\\User Data\\Default\\Cache\\Cache_Data\\f_01"),
                  "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Roaming\\Code\\Cache\\Cache_Data\\data_1"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Roaming\\Code\\Code Cache\\js\\index"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Roaming\\Slack\\GPUCache\\data_0"), "temp");
        EXPECT_EQ(classOf("\\ProgramData\\NVIDIA Corporation\\NV_Cache\\ShaderCache\\x.bin"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Microsoft\\Windows\\INetCache\\IE\\x.htm"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Mozilla\\Firefox\\Profiles\\p\\cache2\\entries\\A1"), "temp");
    }

    TEST(Catalog, Logs) {
        EXPECT_EQ(classOf("\\logs\\app.log"), "logs");
        EXPECT_EQ(classOf("\\logs\\app.log", 4096, 0, true), "logs"); // Churning or not.
        EXPECT_EQ(classOf("\\srv\\nginx\\access.log.1"), "logs"); // Rotated.
        EXPECT_EQ(classOf("\\srv\\nginx\\access.log.12"), "logs");
        EXPECT_EQ(classOf("\\Windows\\System32\\winevt\\Logs\\System.evtx", 20 * mb), "logs");
        EXPECT_EQ(classOf("\\perf\\boot.etl", 200 * mb), "logs");
        EXPECT_EQ(classIn({"\\proj\\.git"}, "\\proj\\debug.log"), "logs"); // Inside projects too.
        EXPECT_EQ(classOf("\\srv\\nginx\\access.log.gz"), "archives"); // Compressed old logs are archives.
        EXPECT_EQ(classOf("\\work\\logbook.docx"), "unknown");
    }

    TEST(Catalog, DumpsAnywhereProgramsRun) {
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\CrashDumps\\app.exe.1234.dmp", 300 * mb), "dumps");
        EXPECT_EQ(classOf("\\Windows\\MEMORY.DMP", 4000 * mb), "dumps");
        EXPECT_EQ(classOf("\\Windows\\Minidump\\110424-12345-01.dmp"), "dumps");
        EXPECT_EQ(classOf("\\Windows\\LiveKernelReports\\WATCHDOG\\x.dmp"), "dumps");
        EXPECT_EQ(classOf("\\Games\\Foo\\crash.mdmp", 50 * mb), "dumps");
        EXPECT_EQ(classOf("\\Tools\\heap.hdmp", 900 * mb), "dumps");
        EXPECT_EQ(classOf("\\srv\\app\\erl_crash.dump", 20 * mb), "dumps");
        EXPECT_EQ(classOf("\\srv\\app\\hs_err_pid4242.log"), "dumps"); // A JVM crash report, not a log.
        EXPECT_EQ(classOf("\\srv\\app\\java_pid4242.hprof", 2000 * mb), "dumps");
        EXPECT_EQ(classOf("\\cygwin\\home\\a.exe.stackdump"), "dumps");
        EXPECT_EQ(classOf("\\ProgramData\\Microsoft\\Windows\\WER\\ReportArchive\\AppCrash_x\\Report.wer"), "dumps");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Slack\\Crashpad\\reports\\abc.dmp"), "dumps");
        EXPECT_EQ(classOf("\\home\\me\\core", 64 * mb), "dumps");
        EXPECT_EQ(classIn({"\\rs\\Cargo.toml"}, "\\rs\\target\\debug\\crash.dmp", 50 * mb), "dumps"); // Beats build.
        EXPECT_EQ(classIn({"\\rs\\Cargo.toml"}, "\\rs\\src\\core", 4096), "unknown"); // A small "core" file.
        EXPECT_EQ(classOf("\\src\\core", 0, FileDirectory), "directories");
    }

    TEST(Catalog, Archives) {
        EXPECT_EQ(classOf("\\downloads\\tools.zip", 50 * mb), "archives");
        EXPECT_EQ(classOf("\\downloads\\src.tar.gz"), "archives");
        EXPECT_EQ(classOf("\\downloads\\BIG.7Z", 4096 * mb), "archives");
        EXPECT_EQ(classIn({"\\proj\\CMakeLists.txt"}, "\\proj\\build\\out.zip"), "build"); // Build folders win.
    }

    TEST(Catalog, DiscImages) {
        EXPECT_EQ(classOf("\\iso\\Windows.iso", 5000 * mb), "disc-images");
        EXPECT_EQ(classOf("\\games\\disc.bin", 700 * mb), "disc-images");
        EXPECT_EQ(classOf("\\games\\disc.cue"), "disc-images");
        EXPECT_EQ(classIn({"\\fw\\fw.vcxproj"}, "\\fw\\bin\\firmware.bin"), "build"); // A project's bin folder.
        EXPECT_EQ(classOf("\\fw\\bin\\firmware.bin"), "disc-images"); // Without a project it is an image.
    }

    TEST(Catalog, VmDisks) {
        EXPECT_EQ(classOf("\\VMs\\dev.vhdx", 60000 * mb), "vm-disks");
        EXPECT_EQ(classOf("\\VMs\\linux\\disk.vmdk"), "vm-disks");
        EXPECT_EQ(classOf("\\VMs\\snap.AVHDX"), "vm-disks");
        EXPECT_EQ(classOf("\\qemu\\img.qcow2"), "vm-disks");
    }

    TEST(Catalog, Backups) {
        EXPECT_EQ(classOf("\\backup\\db.bak", 2000 * mb), "backups");
        EXPECT_EQ(classOf("\\images\\install.wim"), "backups");
        EXPECT_EQ(classOf("\\Temp\\old.bak"), "temp"); // Temp folders win.
    }

    TEST(Catalog, Photos) {
        EXPECT_EQ(classOf("\\Pictures\\2024\\IMG_0001.CR3", 30 * mb), "photos");
        EXPECT_EQ(classOf("\\Pictures\\2024\\IMG_0001.xmp"), "photos"); // Sidecars stay with their photos.
        EXPECT_EQ(classOf("\\Pictures\\2024\\IMG_0002.JPG", 8 * mb), "photos");
        EXPECT_EQ(classOf("\\work\\poster.psd", 3000 * mb), "photos"); // Size does not matter.
    }

    TEST(Catalog, CatalogsAndThumbnails) {
        EXPECT_EQ(classOf("\\Pictures\\Lightroom\\Lightroom Catalog.lrcat", 500 * mb), "catalogs");
        EXPECT_EQ(classOf("\\Pictures\\Lightroom\\Lightroom Catalog Previews.lrdata\\0\\01\\x.lrprev"), "catalogs");
        EXPECT_EQ(classOf("\\Pictures\\2024\\Thumbs.db"), "catalogs");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Microsoft\\Windows\\Explorer\\thumbcache_256.db"), "catalogs");
        EXPECT_EQ(classOf("\\Pictures\\Previews.lrdata\\a.jpg"), "catalogs"); // Previews are JPEGs, still catalogs.
    }

    TEST(Catalog, Video) {
        EXPECT_EQ(classOf("\\Videos\\holiday.mp4", 1500 * mb), "video");
        EXPECT_EQ(classOf("\\clips\\short.mp4", 3 * mb), "video"); // Any size.
        for (const char* name : {"a.MOV", "b.webm", "c.m4v", "d.avi", "e.wmv", "f.flv", "g.mpg", "h.3gp", "i.mkv"}) {
            EXPECT_EQ(classOf(std::string("\\Videos\\") + name, 2 * mb), "video") << name;
        }
        // Discs copied to the drive: DVD and DVD-VR, HD DVD and Blu-ray streams.
        for (const char* path :
             {"\\Movie\\VIDEO_TS\\VTS_01_1.VOB", "\\Rec\\DVD_RTAV\\VR_MOVIE.VRO", "\\HD\\HVDVD_TS\\FEATURE_1.EVO",
              "\\BD\\BDMV\\STREAM\\00001.m2ts", "\\BD3D\\BDMV\\STREAM\\SSIF\\00001.ssif"}) {
            EXPECT_EQ(classOf(path, 1000 * mb), "video") << path;
        }
        EXPECT_EQ(classOf("\\TV\\recording.ts", 4000 * mb), "video"); // A large .ts is a transport stream.
        EXPECT_EQ(classOf("\\AVCHD\\BDMV\\STREAM\\00000.MTS", 50 * mb), "video"); // Camcorder clip.
        EXPECT_EQ(classOf("\\Camera\\MOV001.MOD", 50 * mb), "video");
        EXPECT_EQ(classIn({"\\web\\package.json"}, "\\web\\module.mts", 8 * 1024), "source"); // TypeScript.
        EXPECT_EQ(classOf("\\TV\\clip.ts", 8 * 1024), "unknown"); // A small .ts outside a project: no guess.
        EXPECT_EQ(classOf("\\proj\\go.mod", 2 * 1024), "unknown"); // A Go module file, not a camcorder clip.
        EXPECT_EQ(classOf("\\Memes\\loop.gif", 10 * mb), "video"); // A large GIF is an animation.
        EXPECT_EQ(classOf("\\web\\icon.gif", 10 * mb - 1), "unknown"); // A small one is just an image.
    }

    // A catalog with one class and one rule, changed by `edit` to test errors.
    std::string catalogText(std::string_view classes, std::string_view rules, std::string_view version = "1",
                            std::string_view extra = "") {
        return std::format(R"({{"version": {}, {}"classes": [{}], "fallback": "a", "rules": [{}]}})", version, extra,
                           classes, rules);
    }

    TEST(Catalog, ReadsMarkersAndProjects) {
        const std::string good = R"({"id": "a", "name": "A", "color": "#112233"})";
        const Catalog c = parseCatalog(catalogText(
            good, R"({"name": "r", "class": "a", "inProject": true, "markers": ["x.cfg"], "folders": ["out"]})", "1",
            R"("projectMarkers": [".git", "*.sln"], )"));
        ASSERT_EQ(c.rules.rules.size(), 1U);
        EXPECT_EQ(c.rules.rules[0].markers, (std::vector<std::string>{"x.cfg", ".git", "*.sln"}));
        EXPECT_EQ(c.rules.rules[0].folders, (std::vector<std::string>{"out"}));
    }

    TEST(Catalog, RefusesBadCatalogs) {
        const std::string good = R"({"id": "a", "name": "A", "color": "#112233"})";
        EXPECT_NO_THROW((void)parseCatalog(catalogText(good, R"({"name": "r", "class": "a", "minSize": "2 GB"})")));
        const auto refused = [&](std::string_view classes, std::string_view rules, std::string_view version = "1") {
            EXPECT_THROW((void)parseCatalog(catalogText(classes, rules, version)), CatalogError) << rules;
        };
        refused(good, R"({"name": "r", "class": "b"})"); // Unknown class.
        refused(good, R"({"name": "r", "class": "a", "flags": ["shiny"]})"); // Unknown flag.
        refused(good, R"({"name": "r", "class": "a", "minSize": "2 parsecs"})"); // Bad size.
        refused(good, R"({"name": "r", "class": "a", "churn": "sometimes"})"); // Bad churn.
        refused(good, R"({"name": "r", "class": "a", "paths": ["x**"]})"); // Bad pattern.
        refused(good, R"({"name": "r", "class": "a", "extension": ["x"]})"); // Misspelt key.
        refused(good, R"({"name": "r", "class": "a", "folders": ["out"]})"); // Folders without markers.
        refused(good, R"({"name": "r", "class": "a", "markers": ["a/b"]})"); // A marker of two names.
        refused(good, R"({"name": "r", "class": "a", "markers": ["**"]})"); // Not a name.
        refused(good, R"({"name": "r", "class": "a", "inProject": true})"); // No projectMarkers in the catalog.
        refused(R"({"id": "a", "name": "A", "color": "112233"})", R"({"name": "r", "class": "a"})"); // Bad color.
        refused(good + "," + good, R"({"name": "r", "class": "a"})"); // Duplicate class.
        refused(good, R"({"name": "r", "class": "a"})", "2"); // Other version.
        EXPECT_THROW((void)parseCatalog("{"), CatalogError); // Bad JSON.
    }

}} // namespace icd::core::classify
