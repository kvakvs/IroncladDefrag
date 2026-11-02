// Built-in class catalog (T071): one test per class of plan.md 8.3-8.6 with typical and edge-case paths, and catalog
// files that must be refused.

#include "core/classify/Catalog.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace icd::core::classify { namespace {

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // The "now" of the tests.
    constexpr std::int64_t now = 1000 * day;

    // One megabyte.
    constexpr std::uint64_t mb = 1ULL << 20U;

    // The class id of a file under the built-in catalog.
    std::string classOf(std::string_view path, std::uint64_t size = 4096, std::uint16_t flags = 0, bool churn = false) {
        static const RuleEngine engine(builtinCatalog().rules);
        const FileFacts facts{.sizeBytes = size, .modified = now - day, .flags = flags, .churn = churn};
        return builtinCatalog().classes[engine.classifyPath(path, facts, now)].id;
    }

    TEST(Catalog, BuiltInParsesWithEveryClassReachable) {
        const Catalog& c = builtinCatalog();
        EXPECT_EQ(c.classes.size(), 15U);
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
        EXPECT_EQ(c.find("photos"), ClassId{12});
        EXPECT_FALSE(c.find("nope"));
    }

    TEST(Catalog, Unknown) {
        EXPECT_EQ(classOf("\\Documents\\report.docx"), "unknown");
        EXPECT_EQ(classOf("\\data\\blob"), "unknown");
        EXPECT_EQ(classOf("\\clips\\short.mp4", 50 * mb), "unknown"); // Videos under 100 MB are not "Video".
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
    }

    TEST(Catalog, SourceAndConfig) {
        EXPECT_EQ(classOf("\\proj\\src\\main.cpp"), "source");
        EXPECT_EQ(classOf("\\proj\\CMakeLists.txt"), "source");
        EXPECT_EQ(classOf("\\proj\\Makefile"), "source");
        EXPECT_EQ(classOf("\\proj\\.gitignore"), "source");
        EXPECT_EQ(classOf("\\proj\\App.SLN"), "source"); // Case-insensitive extension.
        EXPECT_EQ(classOf("\\web\\app.ts", 8 * 1024), "source"); // TypeScript, not a transport stream.
        EXPECT_EQ(classOf("\\proj\\build\\gen.cpp"), "build"); // Build folders win.
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
        EXPECT_EQ(classOf("\\app\\node_modules_old\\index.js"), "source"); // Only the exact folder name.
    }

    TEST(Catalog, BuildOutput) {
        EXPECT_EQ(classOf("\\proj\\obj\\Debug\\main.obj"), "build");
        EXPECT_EQ(classOf("\\proj\\x64\\Release\\app.exe", 5 * mb), "build");
        EXPECT_EQ(classOf("\\proj\\src\\main.pdb", 20 * mb), "build"); // By extension anywhere.
        EXPECT_EQ(classOf("\\proj\\.vs\\proj\\v17\\Browse.VC.db"), "build");
        EXPECT_EQ(classOf("\\proj\\CMakeFiles\\3.30\\CMakeCCompiler.cmake"), "build");
        EXPECT_EQ(classOf("\\proj\\binaries\\tool.exe"), "unknown"); // "bin" is a whole folder name.
    }

    TEST(Catalog, Temp) {
        EXPECT_EQ(classOf("\\work\\file.tmp"), "temp");
        EXPECT_EQ(classOf("\\work\\~$report.docx"), "temp");
        EXPECT_EQ(classOf("\\Users\\me\\AppData\\Local\\Temp\\setup.log"), "temp");
        EXPECT_EQ(classOf("\\repo\\.git\\objects\\ab\\cdef0123456789"), "temp"); // Loose objects churn.
        EXPECT_EQ(classOf("\\home\\.cache\\pip\\x.whl"), "temp");
        EXPECT_EQ(classOf("\\logs\\app.log", 4096, 0, true), "temp"); // Churn from the journal.
        EXPECT_EQ(classOf("\\work\\template.docx"), "unknown");
        EXPECT_EQ(classOf("\\work\\temperature.csv"), "unknown");
    }

    TEST(Catalog, Archives) {
        EXPECT_EQ(classOf("\\downloads\\tools.zip", 50 * mb), "archives");
        EXPECT_EQ(classOf("\\downloads\\src.tar.gz"), "archives");
        EXPECT_EQ(classOf("\\downloads\\BIG.7Z", 4096 * mb), "archives");
        EXPECT_EQ(classOf("\\proj\\build\\out.zip"), "build"); // Build folders win.
    }

    TEST(Catalog, DiscImages) {
        EXPECT_EQ(classOf("\\iso\\Windows.iso", 5000 * mb), "disc-images");
        EXPECT_EQ(classOf("\\games\\disc.bin", 700 * mb), "disc-images");
        EXPECT_EQ(classOf("\\games\\disc.cue"), "disc-images");
        EXPECT_EQ(classOf("\\proj\\bin\\firmware.bin"), "build"); // Inside a bin folder it is build output.
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
        EXPECT_EQ(classOf("\\Videos\\exact.mkv", 100 * mb), "video"); // 100 MB is enough.
        EXPECT_EQ(classOf("\\Videos\\almost.mkv", 100 * mb - 1), "unknown");
        EXPECT_EQ(classOf("\\TV\\recording.ts", 4000 * mb), "video"); // A large .ts is a transport stream.
    }

    // A catalog with one class and one rule, changed by `edit` to test errors.
    std::string catalogText(std::string_view classes, std::string_view rules, std::string_view version = "1") {
        return std::format(R"({{"version": {}, "classes": [{}], "fallback": "a", "rules": [{}]}})", version, classes,
                           rules);
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
        refused(R"({"id": "a", "name": "A", "color": "112233"})", R"({"name": "r", "class": "a"})"); // Bad color.
        refused(good + "," + good, R"({"name": "r", "class": "a"})"); // Duplicate class.
        refused(good, R"({"name": "r", "class": "a"})", "2"); // Other version.
        EXPECT_THROW((void)parseCatalog("{"), CatalogError); // Bad JSON.
    }

}} // namespace icd::core::classify
