// Settings file (per-drive strategy memory, Fast budget multiplier, manual caps): JSON round trip, refused documents,
// lenient entries, drive keys, the store's load / save / damaged-file behaviour in a temporary directory, and caps
// from saved settings applied by AppServices now and after a restart.

#include "core/strategy/Presets.h"
#include "services/AppServices.h"
#include "services/settings/Settings.h"
#include "services/settings/SettingsStore.h"
#include "services/workflow/FakeResults.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace icd::services { namespace {

    // A volume GUID name as real drives report it.
    const std::string volumeD = "\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\";

    // Settings with two remembered drives.
    Settings sample() {
        Settings settings;
        settings.driveStrategies[volumeD] = {"photos", EffortLevel::Maximum};
        settings.driveStrategies["E:\\"] = {"defrag-only", EffortLevel::Moderate};
        return settings;
    }

    // The sample with a non-default multiplier and manual caps.
    Settings tuned() {
        Settings settings = sample();
        settings.fastBudgetMultiplier = 4;
        settings.resources = {.workerThreads = 3, .memoryBudgetBytes = 5ULL << 30U};
        settings.mapColors.preset = core::ColorPreset::Dark;
        settings.mapColors.custom[core::ColorPreset::Dark] = {{"class.photos", core::rgb(1, 2, 255)}};
        settings.mapColors.custom[core::ColorPreset::Standard] = {{"free", core::rgb(255, 255, 255)}};
        return settings;
    }

    // A fresh, empty directory for one test.
    std::filesystem::path freshDirectory(const std::string& name) {
        const auto dir = std::filesystem::temp_directory_path() / ("icd-settings-test-" + name);
        std::filesystem::remove_all(dir);
        return dir;
    }

    // Writes text to a file.
    void writeFile(const std::filesystem::path& file, const std::string& text) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }

    TEST(Settings, RoundTripsThroughJson) {
        const Settings settings = sample();
        const std::string json = settingsJson(settings);
        EXPECT_NE(json.find("\"version\": 2"), std::string::npos);
        EXPECT_NE(json.find("\"maximum\""), std::string::npos);
        EXPECT_EQ(parseSettings(json), settings);
        EXPECT_EQ(parseSettings(settingsJson({})), Settings{});
        EXPECT_EQ(parseSettings(settingsJson(tuned())), tuned());
    }

    TEST(Settings, ReadsVersionOneFiles) {
        const Settings settings =
            parseSettings(R"({"version": 1, "drives": {"E:\\": {"strategy": "defrag-only", "effort": "moderate"}}})");
        EXPECT_EQ(settings.driveStrategies.at("E:\\"), (DriveStrategy{"defrag-only", EffortLevel::Moderate}));
        EXPECT_EQ(settings.fastBudgetMultiplier, 1U);
        EXPECT_EQ(settings.resources, core::ResourceSettings{});
    }

    TEST(Settings, UnusableMultiplierAndCapsKeepDefaults) {
        for (const char* bad : {"3", "0", "-2", "2.5", "\"4\"", "null"}) {
            const std::string json = std::format(
                R"({{"version": 2, "fastBudgetMultiplier": {0}, "resources": {{"workerThreads": {0}, "memoryBudgetBytes": "x"}}}})",
                bad);
            const Settings settings = parseSettings(json);
            EXPECT_EQ(settings.fastBudgetMultiplier, 1U) << bad;
            EXPECT_EQ(settings.resources.memoryBudgetBytes, 0U) << bad;
        }
        EXPECT_EQ(
            parseSettings(R"({"version": 2, "resources": {"workerThreads": 4294967296}})").resources.workerThreads, 0U);
        EXPECT_EQ(parseSettings(R"({"version": 2, "resources": 7})").resources, core::ResourceSettings{});
        for (const std::uint32_t multiplier : fastBudgetMultipliers()) {
            const std::string json = std::format(R"({{"version": 2, "fastBudgetMultiplier": {}}})", multiplier);
            EXPECT_EQ(parseSettings(json).fastBudgetMultiplier, multiplier);
        }
    }

    TEST(Settings, MapColors) {
        EXPECT_EQ(colorText(core::rgb(1, 0xAB, 255)), "#01ABFF");
        EXPECT_EQ(colorFromText("#01abff"), core::rgb(1, 0xAB, 255));
        for (const char* bad : {"", "#", "01ABFF", "#01ABF", "#01ABFFF", "#GGGGGG", "#-1ABFF", "# 1ABFF"}) {
            EXPECT_FALSE(colorFromText(bad)) << bad;
        }
        const Settings settings = parseSettings(R"({"version": 2, "mapColors": {"preset": "colorblind", "custom": {
            "colorblind": {"free": "#000000", "age.1": "red", "size.2": 7},
            "neon": {"free": "#FFFFFF"}}}})");
        EXPECT_EQ(settings.mapColors.preset, core::ColorPreset::ColorblindSafe);
        ASSERT_EQ(settings.mapColors.custom.size(), 1U);
        EXPECT_EQ(settings.mapColors.custom.at(core::ColorPreset::ColorblindSafe),
                  (core::ColorScheme::Custom{{"free", core::rgb(0, 0, 0)}}));
        EXPECT_EQ(parseSettings(R"({"version": 2, "mapColors": {"preset": "neon"}})").mapColors.preset,
                  core::ColorPreset::Standard);

        const core::ColorScheme scheme = colorSchemeOf(tuned().mapColors);
        EXPECT_EQ(scheme.preset(), core::ColorPreset::Dark);
        EXPECT_EQ(scheme.color("class.photos"), core::rgb(1, 2, 255));
        EXPECT_EQ(scheme.color("free"),
                  core::presetColor(core::ColorPreset::Dark, "free")); // Standard's edit stays there.
    }

    TEST(Settings, RefusesUnusableDocuments) {
        EXPECT_THROW((void)parseSettings(""), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 1"), SettingsError);
        EXPECT_THROW((void)parseSettings("[]"), SettingsError);
        EXPECT_THROW((void)parseSettings("{}"), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 3}"), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 0}"), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": \"1\"}"), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 1, \"drives\": []}"), SettingsError);
        EXPECT_EQ(parseSettings("{\"version\": 1}"), Settings{});
    }

    TEST(Settings, SkipsUnusableDriveEntries) {
        const Settings settings = parseSettings(R"({
            "version": 1,
            "future": true,
            "drives": {
                "A:\\": {"strategy": "video", "effort": "fast", "extra": 3},
                "B:\\": {"strategy": "video", "effort": "turbo"},
                "C:\\": {"effort": "fast"},
                "D:\\": {"strategy": 5, "effort": "fast"},
                "E:\\": "photos"
            }
        })");
        ASSERT_EQ(settings.driveStrategies.size(), 1U);
        EXPECT_EQ(settings.driveStrategies.at("A:\\"), (DriveStrategy{"video", EffortLevel::Fast}));
    }

    TEST(Settings, EffortSpellings) {
        for (const EffortLevel level : {EffortLevel::Fast, EffortLevel::Moderate, EffortLevel::Maximum}) {
            EXPECT_EQ(effortFromId(effortId(level)), level);
        }
        EXPECT_FALSE(effortFromId("Fast"));
        EXPECT_FALSE(effortFromId(""));
    }

    TEST(Settings, DriveKeyPrefersVolumeGuid) {
        core::DriveInfo drive;
        drive.root = "D:\\";
        EXPECT_EQ(driveSettingsKey(drive), "D:\\");
        drive.layout.volumeName = volumeD;
        EXPECT_EQ(driveSettingsKey(drive), volumeD);
    }

    TEST(Settings, PresetIdsAreUniqueAndFound) {
        const auto& presets = core::strategy::builtinPresets();
        for (std::size_t index = 0; index < presets.size(); ++index) {
            EXPECT_FALSE(presets.at(index).id.empty());
            EXPECT_EQ(core::strategy::builtinPresetIndex(presets.at(index).id), index);
        }
        EXPECT_FALSE(core::strategy::builtinPresetIndex("no-such-preset"));
    }

    TEST(SettingsStore, MissingFileGivesDefaults) {
        const auto dir = freshDirectory("missing");
        const SettingsStore store(dir / "settings.json");
        EXPECT_EQ(store.current(), Settings{});
        EXPECT_FALSE(std::filesystem::exists(store.path()));
    }

    TEST(SettingsStore, RemembersAcrossRestarts) {
        const auto dir = freshDirectory("restart");
        const auto file = dir / "nested" / "settings.json";
        {
            SettingsStore store(file);
            EXPECT_TRUE(store.rememberDriveStrategy(volumeD, {"photos", EffortLevel::Maximum}));
            EXPECT_TRUE(store.rememberDriveStrategy("E:\\", {"defrag-only", EffortLevel::Moderate}));
            EXPECT_TRUE(store.rememberDriveStrategy("E:\\", {"defrag-only", EffortLevel::Moderate})); // Unchanged.
        }
        SettingsStore reopened(file);
        EXPECT_EQ(reopened.current(), sample());
        EXPECT_EQ(reopened.driveStrategy(volumeD), (DriveStrategy{"photos", EffortLevel::Maximum}));
        EXPECT_FALSE(reopened.driveStrategy("F:\\"));
        EXPECT_TRUE(reopened.rememberDriveStrategy(volumeD, {"video", EffortLevel::Fast}));
        EXPECT_EQ(SettingsStore(file).driveStrategy(volumeD), (DriveStrategy{"video", EffortLevel::Fast}));
        EXPECT_FALSE(std::filesystem::exists(dir / "nested" / "settings.json.tmp"));
    }

    TEST(SettingsStore, DamagedFileIsKeptAsideAndReplaced) {
        const auto dir = freshDirectory("damaged");
        const auto file = dir / "settings.json";
        writeFile(file, "{\"version\": 1, \"drives\": {");
        SettingsStore store(file);
        EXPECT_EQ(store.current(), Settings{});
        EXPECT_TRUE(std::filesystem::exists(dir / "settings.json.bad"));
        EXPECT_TRUE(store.rememberDriveStrategy(volumeD, {"archive", EffortLevel::Fast}));
        EXPECT_EQ(SettingsStore(file).driveStrategy(volumeD), (DriveStrategy{"archive", EffortLevel::Fast}));
    }

    TEST(SettingsStore, UnwritableFileKeepsValuesForTheRun) {
        const auto dir = freshDirectory("unwritable");
        std::filesystem::create_directories(dir / "settings.json"); // A directory where the file should be.
        SettingsStore store(dir / "settings.json");
        EXPECT_FALSE(store.rememberDriveStrategy(volumeD, {"video", EffortLevel::Moderate}));
        EXPECT_EQ(store.driveStrategy(volumeD), (DriveStrategy{"video", EffortLevel::Moderate}));
    }

    TEST(SettingsStore, TunedValuesSurviveRestarts) {
        const auto dir = freshDirectory("tuned");
        const auto file = dir / "settings.json";
        EXPECT_TRUE(SettingsStore(file).update(tuned()));
        EXPECT_EQ(SettingsStore(file).current(), tuned());
    }

    // Manual caps saved in one run size the pool of the next one, and a change applies without a restart.
    TEST(AppServicesSettings, ManualCapsApplyNowAndAfterRestart) {
        const auto dir = freshDirectory("caps");
        const auto file = dir / "settings.json";
        std::size_t capacity = 0;
        {
            AppServices services(DriveSource::Fake, file);
            capacity = services.pool().capacity();
            EXPECT_NE(services.caps().workerReason, core::CapReason::Manual);
            Settings next = services.settings().current();
            next.resources = {.workerThreads = 1, .memoryBudgetBytes = 1ULL << 20U};
            EXPECT_TRUE(services.applySettings(next));
            EXPECT_EQ(services.caps().workerThreads, 1U);
            EXPECT_EQ(services.pool().concurrency(), 1U);
            EXPECT_EQ(services.caps().memoryBudgetBytes, 1ULL << 20U);
            EXPECT_EQ(services.caps().memoryReason, core::CapReason::Manual);
        }
        AppServices restarted(DriveSource::Fake, file);
        EXPECT_EQ(restarted.caps().workerThreads, 1U);
        EXPECT_EQ(restarted.pool().concurrency(), 1U);
        EXPECT_EQ(restarted.caps().memoryBudgetBytes, 1ULL << 20U);
        EXPECT_EQ(restarted.pool().capacity(), capacity); // Room for a higher manual cap without a new pool.
        Settings automatic = restarted.settings().current();
        automatic.resources = {};
        EXPECT_TRUE(restarted.applySettings(automatic));
        EXPECT_NE(restarted.caps().workerReason, core::CapReason::Manual);
        EXPECT_EQ(restarted.pool().concurrency(), restarted.caps().workerThreads);
    }

}} // namespace icd::services
