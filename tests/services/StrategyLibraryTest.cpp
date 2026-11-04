// Custom strategies (T131): ids from names, save / replace / delete / import / export in a temporary directory, files
// that cannot be used are skipped, lookups cover the built-in presets, and the Phase 10 exit check: a custom strategy
// read back after a restart is the same script and gives the same plan at every level.

#include "services/settings/StrategyLibrary.h"

#include "core/classify/RuleEngine.h"
#include "core/strategy/Presets.h"
#include "core/synth/SyntheticVolume.h"
#include "services/AppServices.h"
#include "services/plan/PlanJob.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace icd::services { namespace {

    // FILETIME ticks per day.
    constexpr std::int64_t day = 864'000'000'000;

    // A fresh, empty directory for one test.
    std::filesystem::path freshDirectory(const std::string& name) {
        const auto dir = std::filesystem::temp_directory_path() / ("icd-strategy-test-" + name);
        std::filesystem::remove_all(dir);
        return dir;
    }

    // Writes text to a file.
    void writeFile(const std::filesystem::path& file, const std::string& text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }

    // The photo preset with the user's edits: photos by size at Maximum and a larger import gap.
    core::strategy::ZoneScript editedPhotos() {
        core::strategy::ZoneScript script = *StrategyLibrary({}).find("photos");
        for (auto& zone : script.zones) {
            if (zone.name == "Photos") {
                zone.orderByLevel.at(static_cast<std::size_t>(core::strategy::Effort::Maximum)) =
                    core::strategy::ZoneOrder::Size;
                zone.gapAfter.freePercent = 5;
            }
        }
        return script;
    }

    TEST(StrategyLibrary, IdsFromNames) {
        EXPECT_EQ(customStrategyId("My photos!"), "custom-my-photos");
        EXPECT_EQ(customStrategyId("  Video -- 2024 "), "custom-video-2024");
        EXPECT_EQ(customStrategyId("\xC3\x9C"
                                   "ber"),
                  "custom-ber");
        EXPECT_EQ(customStrategyId("***"), "custom-strategy");
        EXPECT_TRUE(isCustomStrategyId("custom-x"));
        EXPECT_FALSE(isCustomStrategyId("photos"));
        for (const auto& preset : core::strategy::builtinPresets()) {
            EXPECT_FALSE(isCustomStrategyId(preset.id)) << preset.id; // Built-in and custom ids never collide.
        }
    }

    TEST(StrategyLibrary, MissingDirectoryHasOnlyBuiltIns) {
        const StrategyLibrary library(freshDirectory("missing"));
        EXPECT_TRUE(library.custom().empty());
        EXPECT_EQ(library.all().size(), core::strategy::builtinPresets().size());
        ASSERT_NE(library.find("photos"), nullptr);
        EXPECT_EQ(library.find("photos")->id, "photos");
        EXPECT_EQ(library.find("custom-x"), nullptr);
    }

    TEST(StrategyLibrary, SavesReplacesAndDeletes) {
        const auto dir = freshDirectory("save");
        StrategyLibrary library(dir);
        const std::string id = library.save(editedPhotos(), "My photos").id;
        EXPECT_EQ(id, "custom-my-photos");
        EXPECT_TRUE(std::filesystem::exists(dir / "custom-my-photos.json"));
        EXPECT_EQ(library.find(id)->name, "My photos");
        EXPECT_EQ(library.all().back()->id, id); // Custom strategies come after the built-in ones.

        // The same name replaces in place; a name with the same id gets the next free one.
        core::strategy::ZoneScript changed = editedPhotos();
        changed.description = "changed";
        EXPECT_EQ(library.save(changed, "My photos").id, id);
        EXPECT_EQ(library.custom().size(), 1U);
        EXPECT_EQ(library.find(id)->description, "changed");
        EXPECT_EQ(library.save(editedPhotos(), "My photos!").id, "custom-my-photos-2");
        EXPECT_EQ(library.custom().front().name, "My photos"); // Ordered by name.

        EXPECT_TRUE(library.remove(id));
        EXPECT_FALSE(library.remove(id));
        EXPECT_FALSE(library.remove("photos")); // Built-ins cannot be deleted.
        EXPECT_FALSE(std::filesystem::exists(dir / "custom-my-photos.json"));
        EXPECT_EQ(StrategyLibrary(dir).custom().size(), 1U);
    }

    TEST(StrategyLibrary, InvalidScriptIsNotSaved) {
        const auto dir = freshDirectory("invalid");
        StrategyLibrary library(dir);
        core::strategy::ZoneScript broken = editedPhotos();
        broken.zones.front().select.classes = {"fotos"};
        EXPECT_THROW((void)library.save(broken, "Broken"), StrategyLibraryError);
        EXPECT_TRUE(library.custom().empty());
        EXPECT_FALSE(std::filesystem::exists(dir / "custom-broken.json"));
    }

    TEST(StrategyLibrary, ExportImportRoundTrip) {
        const auto dir = freshDirectory("import");
        StrategyLibrary library(dir / "library");
        const auto file = dir / "out" / "photos.json";
        StrategyLibrary::exportFile(editedPhotos(), file);

        const core::strategy::ZoneScript& first = library.importFile(file);
        EXPECT_EQ(first.name, "Photo storage");
        EXPECT_EQ(first.id, "custom-photo-storage");
        core::strategy::ZoneScript expected = editedPhotos();
        expected.id = first.id;
        EXPECT_EQ(first, expected);
        const core::strategy::ZoneScript& second = library.importFile(file); // Never replaces: a new name.
        EXPECT_EQ(second.name, "Photo storage (2)");
        EXPECT_EQ(second.id, "custom-photo-storage-2");
        EXPECT_EQ(library.custom().size(), 2U);

        writeFile(dir / "bad.json", R"({"version": 1, "id": "x", "name": "Bad", "zones": [
            {"name": "A", "select": {"classes": ["fotos"]}, "action": "fast-fill"}]})");
        try {
            (void)library.importFile(dir / "bad.json");
            FAIL() << "a script with an unknown class was imported";
        }
        catch (const StrategyLibraryError& error) {
            EXPECT_NE(std::string(error.what()).find("fotos"), std::string::npos) << error.what();
        }
        EXPECT_THROW((void)library.importFile(dir / "missing.json"), StrategyLibraryError);
        EXPECT_EQ(library.custom().size(), 2U);
    }

    TEST(StrategyLibrary, UnusableFilesAreSkipped) {
        const auto dir = freshDirectory("skip");
        StrategyLibrary(dir).save(editedPhotos(), "Good");
        const std::string good = core::strategy::zoneScriptJson(*StrategyLibrary(dir).find("custom-good"));
        writeFile(dir / "broken.json", "{\"version\": 1,");
        writeFile(dir / "builtin.json", core::strategy::zoneScriptJson(*StrategyLibrary({}).find("video")));
        writeFile(dir / "zz-duplicate.json", good); // The same id again.
        writeFile(dir / "notes.txt", "not a strategy");
        const StrategyLibrary library(dir);
        ASSERT_EQ(library.custom().size(), 1U);
        EXPECT_EQ(library.custom().front().id, "custom-good");
        EXPECT_EQ(library.find("video"),
                  &core::strategy::builtinPresets().at(*core::strategy::builtinPresetIndex("video")));
    }

    // A request on a synthetic volume for a strategy.
    PlanRequest request(core::strategy::ZoneScript script) {
        core::SynthOptions options;
        options.seed = 131;
        options.files = 3000;
        options.geometry.totalClusters = 1ULL << 20U;
        options.fillPercent = 60;
        options.fragmentedShare = 0.3;
        auto snapshot = std::make_shared<core::Snapshot>(core::generateSyntheticVolume(options));
        PlanRequest r;
        r.script = std::move(script);
        r.now = 20'000 * day;
        const core::classify::RuleEngine engine(core::classify::builtinCatalog().rules);
        core::InlineExecutor serial;
        r.classes = engine.classify(*snapshot, {.now = r.now, .churn = {}}, serial);
        r.snapshot = std::move(snapshot);
        return r;
    }

    // Phase 10 exit: a custom strategy saved through the app's services is found again after a restart (a new
    // AppServices on the same settings file) and plans exactly as the script it was saved from.
    TEST(StrategyLibrary, CustomStrategySurvivesRestartAndGivesTheSamePlan) {
        const auto dir = freshDirectory("restart");
        const auto settingsFile = dir / "settings.json";
        core::strategy::ZoneScript saved;
        {
            AppServices services(DriveSource::Fake, settingsFile);
            saved = services.strategies().save(editedPhotos(), "My photos");
            EXPECT_TRUE(services.settings().rememberDriveStrategy("E:\\", {saved.id, EffortLevel::Maximum}));
        }
        AppServices restarted(DriveSource::Fake, settingsFile);
        const std::optional<DriveStrategy> remembered = restarted.settings().driveStrategy("E:\\");
        ASSERT_TRUE(remembered);
        const core::strategy::ZoneScript* loaded = restarted.strategies().find(remembered->preset);
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(*loaded, saved);

        core::InlineExecutor serial;
        const PlanOutcome before = planLevels(request(saved), serial);
        const PlanOutcome after = planLevels(request(*loaded), serial);
        const PlanOutcome builtin = planLevels(request(*restarted.strategies().find("photos")), serial);
        bool differs = false;
        for (std::size_t level = 0; level < 3; ++level) {
            SCOPED_TRACE(level);
            const auto& a = before.levels[level].result.plan;
            const auto& b = after.levels[level].result.plan;
            EXPECT_TRUE(after.levels[level].issues.empty());
            ASSERT_EQ(a.moves.size(), b.moves.size());
            EXPECT_EQ(a.runs, b.runs);
            for (std::size_t i = 0; i < a.moves.size(); ++i) {
                ASSERT_EQ(std::tie(a.moves[i].file, a.moves[i].zone, a.moves[i].clusters),
                          std::tie(b.moves[i].file, b.moves[i].zone, b.moves[i].clusters))
                    << i;
            }
            differs = differs || builtin.levels[level].result.plan.runs != b.runs;
        }
        EXPECT_TRUE(differs); // The edits matter, so the check above compares the custom strategy, not the preset.
    }

}} // namespace icd::services
