// Zone script model (T080): JSON round trips, defaults, and every validation rule with a clear message.

#include "core/format/ByteSize.h"
#include "core/snapshot/FileTable.h"
#include "core/strategy/ZoneScript.h"

#include <gtest/gtest.h>

#include <string>

namespace icd::core::strategy { namespace {

    // The catalog every test validates against.
    const classify::Catalog& catalog() { return classify::builtinCatalog(); }

    // A script that sets every field of the model at least once.
    ZoneScript fullScript() {
        ZoneScript s{.id = "test", .name = "Test strategy", .description = "Every field set."};
        Zone dirs{.name = "Directories",
                  .anchor = Anchor::Front,
                  .order = ZoneOrder::DirectoryName,
                  .action = ZoneAction::Sort,
                  .fragmentOk = std::nullopt};
        dirs.select.classes = {"directories"};
        dirs.select.flagsAll = FileDirectory;
        dirs.gapAfter.freePercent = 1.5;
        Zone dev{.name = "Sources", .order = ZoneOrder::DirectoryName, .action = ZoneAction::Sort};
        dev.select = {.classes = {"source", "repository"},
                      .paths = {"**/src/**"},
                      .extensions = {"cpp", "h"},
                      .minSize = 1,
                      .maxSize = 64ULL << 20U,
                      .minAgeDays = 2,
                      .maxAgeDays = 400,
                      .flagsNone = FileSparse | FileCompressed,
                      .churn = classify::ChurnTest::Stable};
        dev.gapAfter = {.volumePercent = 5, .classMultiple = 2, .classes = {"build", "temp"}};
        Zone churn{.name = "Churn", .action = ZoneAction::KeepInPlace};
        churn.select.churn = classify::ChurnTest::Churning;
        Zone rest{.name = "Rest", .order = ZoneOrder::Size, .action = ZoneAction::FastFill, .fragmentOk = 1ULL << 20U};
        rest.gapAfter.bytes = 3ULL << 30U;
        Zone video{.name = "Video", .anchor = Anchor::Back, .action = ZoneAction::PackToBack};
        video.select.classes = {"video"};
        video.orderByLevel[2] = ZoneOrder::Age;
        video.orderByLevel[0] = ZoneOrder::None;
        s.zones = {dirs, dev, churn, rest, video};
        return s;
    }

    // The messages of the issues found in a script.
    std::vector<std::string> issuesOf(const ZoneScript& s) {
        std::vector<std::string> out;
        for (const ScriptIssue& issue : validateScript(s, catalog())) {
            out.push_back(describeIssue(issue, s));
        }
        return out;
    }

    // The error text parseZoneScript() gives for a document.
    std::string parseError(std::string_view json) {
        try {
            (void)parseZoneScript(json, catalog());
        }
        catch (const ZoneScriptError& e) {
            return e.what();
        }
        return {};
    }

    // Whether text contains part.
    bool has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

    TEST(ZoneScript, FullScriptRoundTrips) {
        const ZoneScript s = fullScript();
        EXPECT_TRUE(issuesOf(s).empty()) << (issuesOf(s).empty() ? "" : issuesOf(s).front());
        const std::string json = zoneScriptJson(s);
        const ZoneScript back = parseZoneScript(json, catalog());
        EXPECT_EQ(back, s);
        EXPECT_EQ(zoneScriptJson(back), json);
        EXPECT_TRUE(has(json, "\"fragmentOk\": \"whole\""));
        EXPECT_TRUE(has(json, "\"maxSize\": \"64 MB\""));
        EXPECT_TRUE(has(json, "\"bytes\": \"3 GB\""));
    }

    TEST(ZoneScript, DefaultsAreLeftOutAndRestored) {
        const std::string json = R"({"version": 1, "id": "x", "name": "X",
            "zones": [{"name": "All", "action": "fast-fill"}]})";
        const ZoneScript s = parseZoneScript(json, catalog());
        ASSERT_EQ(s.zones.size(), 1U);
        const Zone& z = s.zones[0];
        EXPECT_EQ(z.anchor, Anchor::AfterPrevious);
        EXPECT_EQ(z.order, ZoneOrder::None);
        EXPECT_EQ(z.fragmentOk, Zone::defaultFragmentOk);
        EXPECT_TRUE(z.gapAfter.empty());
        EXPECT_EQ(z.select, ZoneSelector{});
        EXPECT_FALSE(has(zoneScriptJson(s), "select"));
        EXPECT_FALSE(has(zoneScriptJson(s), "fragmentOk"));
    }

    TEST(ZoneScript, OrderByLevelOverridesTheOrder) {
        const ZoneScript s = fullScript();
        const Zone& video = s.zones.back();
        EXPECT_EQ(video.orderAt(Effort::Fast), ZoneOrder::None);
        EXPECT_EQ(video.orderAt(Effort::Moderate), ZoneOrder::None);
        EXPECT_EQ(video.orderAt(Effort::Maximum), ZoneOrder::Age);
        EXPECT_EQ(s.zones[1].orderAt(Effort::Fast), ZoneOrder::DirectoryName);
    }

    TEST(ZoneScript, SizesAcceptBytesAndUnits) {
        EXPECT_EQ(parseSizeSpec("4096"), 4096U);
        EXPECT_EQ(parseSizeSpec("64 MB"), 64ULL << 20U);
        EXPECT_EQ(parseSizeSpec("2GB"), 2ULL << 30U);
        EXPECT_FALSE(parseSizeSpec("64 mb"));
        EXPECT_FALSE(parseSizeSpec("big"));
        EXPECT_FALSE(parseSizeSpec("99999999999 TB"));
        EXPECT_EQ(sizeSpecText(64ULL << 20U), "64 MB");
        EXPECT_EQ(sizeSpecText(1000), "1000 B");
        EXPECT_EQ(sizeSpecText(1536), "1536 B");
        EXPECT_EQ(sizeSpecText(3072), "3 KB");
        EXPECT_EQ(parseSizeSpec(sizeSpecText(123456789)), 123456789U);
    }

    TEST(ZoneScript, ReadErrorsNameTheZoneAndKey) {
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "fast-fill", "colour": 1}]})"),
                        "zone 1: unknown key \"colour\""));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "fast-fill"}, {"name": "B", "action": "shuffle"}]})"),
                        "zone 2: unknown action \"shuffle\""));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "sort", "order": "random"}]})"),
                        "unknown order \"random\""));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "fast-fill", "fragmentOk": "lots"}]})"),
                        "bad size \"lots\""));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "fast-fill", "select": {"flags": ["shiny"]}}]})"),
                        "unknown file flag \"shiny\""));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "fast-fill", "orderByLevel": {"slow": "age"}}]})"),
                        "unknown effort level \"slow\""));
        EXPECT_TRUE(has(parseError(R"({"version": 2, "id": "x", "name": "X", "zones": []})"), "version 2"));
        EXPECT_TRUE(has(parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [)"), "line"));
        // Every broken zone is reported, not only the first.
        const std::string two = parseError(R"({"version": 1, "id": "x", "name": "X", "zones": [
            {"name": "A", "action": "nope"}, {"name": "B", "action": "nope"}]})");
        EXPECT_TRUE(has(two, "zone 1:"));
        EXPECT_TRUE(has(two, "zone 2:"));
    }

    TEST(ZoneScript, ValidationFindsUnknownClassesAndBadPatterns) {
        ZoneScript s = fullScript();
        s.zones[1].select.classes.push_back("fotos");
        s.zones[1].select.paths.emplace_back("a/x**");
        s.zones[1].gapAfter.classes.push_back("nothing");
        const auto issues = issuesOf(s);
        ASSERT_EQ(issues.size(), 3U);
        EXPECT_TRUE(has(issues[0], "zone 2 \"Sources\": unknown class \"fotos\""));
        EXPECT_TRUE(has(issues[2], "unknown class \"nothing\""));
    }

    TEST(ZoneScript, ValidationChecksRanges) {
        ZoneScript s = fullScript();
        s.zones[1].select.minSize = 10;
        s.zones[1].select.maxSize = 10;
        s.zones[1].select.minAgeDays = 9;
        s.zones[1].select.maxAgeDays = 3;
        s.zones[1].select.flagsAll = FileSparse;
        s.zones[0].gapAfter.freePercent = 120;
        s.zones[3].gapAfter.classMultiple = 2; // Without classes.
        const auto issues = issuesOf(s);
        ASSERT_EQ(issues.size(), 5U);
        EXPECT_TRUE(has(issues[0], "between 0 and 100"));
        EXPECT_TRUE(has(issues[1], "minSize must be below maxSize"));
        EXPECT_TRUE(has(issues[2], "minAgeDays"));
        EXPECT_TRUE(has(issues[3], "both required and excluded"));
        EXPECT_TRUE(has(issues[4], "classMultiple needs classes"));
    }

    TEST(ZoneScript, ValidationChecksAnchorsAndActions) {
        ZoneScript s = fullScript();
        s.zones[3].anchor = Anchor::Front; // Not the first placed zone.
        EXPECT_TRUE(has(issuesOf(s).at(0), "only the first placed zone can be anchored at the front"));

        s = fullScript();
        s.zones.push_back({.name = "After video", .action = ZoneAction::FastFill});
        EXPECT_TRUE(has(issuesOf(s).at(0), "zones after a back zone must be back zones too"));

        s = fullScript();
        s.zones[4].anchor = Anchor::AfterPrevious; // pack-to-back without the back anchor.
        EXPECT_TRUE(has(issuesOf(s).at(0), "pack-to-back zones and only they"));

        s = fullScript();
        s.zones[3].anchor = Anchor::Back; // Back anchor on a fast-fill zone.
        EXPECT_FALSE(issuesOf(s).empty());

        s = fullScript();
        s.zones[1].order = ZoneOrder::None;
        EXPECT_TRUE(has(issuesOf(s).at(0), "a sort zone needs an order"));

        s = fullScript();
        s.zones[2].gapAfter.bytes = 1; // A keep-in-place zone has no place for a gap.
        EXPECT_TRUE(has(issuesOf(s).at(0), "cannot have a gap"));

        s = fullScript();
        s.zones[2].anchor = Anchor::Front; // Unplaced zones ignore anchors.
        EXPECT_TRUE(issuesOf(s).empty());
    }

    TEST(ZoneScript, ValidationChecksNames) {
        ZoneScript s = fullScript();
        s.zones[3].name = "Sources";
        s.zones[2].name.clear();
        const auto issues = issuesOf(s);
        ASSERT_EQ(issues.size(), 2U);
        EXPECT_TRUE(has(issues[0], "zone 3: a zone needs a name"));
        EXPECT_TRUE(has(issues[1], "zone name \"Sources\" is used twice"));

        ZoneScript empty{.id = "", .name = "E"};
        const auto none = issuesOf(empty);
        ASSERT_EQ(none.size(), 2U);
        EXPECT_TRUE(has(none[0], "needs an id and a name"));
        EXPECT_TRUE(has(none[1], "at least one zone"));
    }

    TEST(ZoneScript, ErrorListsEveryIssue) {
        ZoneScript s = fullScript();
        s.zones[1].select.classes = {"a", "b"};
        const ZoneScriptError error(validateScript(s, catalog()));
        EXPECT_EQ(error.issues().size(), 2U);
        EXPECT_TRUE(has(error.what(), "zone 2: unknown class \"a\"\n  zone 2: unknown class \"b\""));
    }

    TEST(ZoneScript, TableTextsRoundTrip) {
        const GapSpec churn{.volumePercent = 5, .classMultiple = 2, .classes = {"build", "temp"}};
        EXPECT_EQ(gapText(churn), "5% volume, 2x build+temp");
        EXPECT_EQ(parseGapText(gapText(churn)), churn);
        EXPECT_EQ(gapText({.freePercent = 1.5, .bytes = 1ULL << 30U}), "1.5% free, 1 GB");
        EXPECT_EQ(parseGapText("1.5% free, 1 GB"), (GapSpec{.freePercent = 1.5, .bytes = 1ULL << 30U}));
        EXPECT_EQ(gapText({}), "none");
        EXPECT_EQ(parseGapText("none"), GapSpec{});
        EXPECT_FALSE(parseGapText("lots of room"));
        EXPECT_FALSE(parseGapText("2x "));
        EXPECT_EQ(fragmentOkText(std::nullopt), "whole");
        const auto whole = parseFragmentOkText("whole");
        ASSERT_TRUE(whole.has_value());
        EXPECT_FALSE(whole->has_value());
        const auto size = parseFragmentOkText(" 64 MB ");
        ASSERT_TRUE(size.has_value());
        EXPECT_EQ(*size, std::optional<std::uint64_t>(64ULL << 20U));
        EXPECT_FALSE(parseFragmentOkText("big"));
        EXPECT_EQ(parseIdsText(" photos, ,video "), (std::vector<std::string>{"photos", "video"}));
        EXPECT_EQ(idsText({"photos", "video"}), "photos, video");
        EXPECT_EQ(actionNamed("pack-to-back"), ZoneAction::PackToBack);
        EXPECT_EQ(orderNamed("age"), ZoneOrder::Age);
        EXPECT_EQ(anchorNamed("back"), Anchor::Back);
        EXPECT_FALSE(actionNamed("shuffle"));
    }

    TEST(ZoneScript, EnumKeysAreStable) {
        EXPECT_EQ(effortKey(Effort::Maximum), "maximum");
        EXPECT_EQ(anchorKey(Anchor::AfterPrevious), "after-previous");
        EXPECT_EQ(orderKey(ZoneOrder::DirectoryName), "directory+name");
        EXPECT_EQ(actionKey(ZoneAction::PackToBack), "pack-to-back");
        EXPECT_TRUE(fullScript().zones[0].placed());
        EXPECT_FALSE(fullScript().zones[2].placed());
    }

}} // namespace icd::core::strategy
