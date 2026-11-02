// Settings file (per-drive strategy memory): JSON round trip, refused documents, lenient drive entries, drive keys,
// and the store's load / save / damaged-file behaviour in a temporary directory.

#include "core/strategy/Presets.h"
#include "services/settings/Settings.h"
#include "services/settings/SettingsStore.h"
#include "services/workflow/FakeResults.h"

#include <gtest/gtest.h>

#include <filesystem>
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
        EXPECT_NE(json.find("\"version\": 1"), std::string::npos);
        EXPECT_NE(json.find("\"maximum\""), std::string::npos);
        EXPECT_EQ(parseSettings(json), settings);
        EXPECT_EQ(parseSettings(settingsJson({})), Settings{});
    }

    TEST(Settings, RefusesUnusableDocuments) {
        EXPECT_THROW((void)parseSettings(""), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 1"), SettingsError);
        EXPECT_THROW((void)parseSettings("[]"), SettingsError);
        EXPECT_THROW((void)parseSettings("{}"), SettingsError);
        EXPECT_THROW((void)parseSettings("{\"version\": 2}"), SettingsError);
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

}} // namespace icd::services
