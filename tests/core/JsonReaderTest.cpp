// JSON reader (T071): values, escapes, member order, kind checks and errors with positions.

#include "core/format/Json.h"
#include "core/format/JsonReader.h"

#include <gtest/gtest.h>

#include <string>

namespace icd::core { namespace {

    TEST(JsonReader, ReadsEveryKindOfValue) {
        const JsonValue v = parseJson(R"({"a": [1, -2.5, 3e2], "b": {"t": true, "f": false, "n": null}, "s": "x"})");
        const auto& a = v.at("a").asArray();
        ASSERT_EQ(a.size(), 3U);
        EXPECT_EQ(a[0].asUnsigned(), 1U);
        EXPECT_DOUBLE_EQ(a[1].asNumber(), -2.5);
        EXPECT_DOUBLE_EQ(a[2].asNumber(), 300.0);
        EXPECT_TRUE(v.at("b").at("t").asBool());
        EXPECT_FALSE(v.at("b").at("f").asBool());
        EXPECT_TRUE(v.at("b").at("n").isNull());
        EXPECT_EQ(v.at("s").asString(), "x");
        EXPECT_EQ(v.find("missing"), nullptr);
        EXPECT_EQ(v.asObject()[1].first, "b"); // File order.
        EXPECT_TRUE(parseJson(" [ ] ").asArray().empty());
        EXPECT_TRUE(parseJson("{}").asObject().empty());
        EXPECT_EQ(parseJson("\xEF\xBB\xBF\"bom\"").asString(), "bom");
    }

    TEST(JsonReader, DecodesEscapes) {
        EXPECT_EQ(parseJson(R"("a\"b\\c\/d\n\t")").asString(), "a\"b\\c/d\n\t");
        EXPECT_EQ(parseJson(R"("\u00e9\u20ac")").asString(), "\xC3\xA9\xE2\x82\xAC");
        EXPECT_EQ(parseJson(R"("\ud83d\ude00")").asString(), "\xF0\x9F\x98\x80"); // Surrogate pair.
        EXPECT_EQ(parseJson("\"caf\xC3\xA9\"").asString(), "caf\xC3\xA9"); // UTF-8 passes through.
    }

    TEST(JsonReader, RoundTripsTheWriter) {
        JsonWriter w;
        w.beginObject().key("name").value("q\"uote\\").key("n").value(std::uint64_t{123456789}).endObject();
        const JsonValue v = parseJson(w.str());
        EXPECT_EQ(v.at("name").asString(), "q\"uote\\");
        EXPECT_EQ(v.at("n").asUnsigned(), 123456789U);
    }

    TEST(JsonReader, RefusesMalformedInput) {
        for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{\"a\":1,}", "01", "+1", ".5", "1.", "tru", "\"abc",
                                "\"a\nb\"", "\"\\x\"", "\"\\ud800\"", "\"\\u12\"", "[1] 2", "{1: 2}", "nul"}) {
            EXPECT_THROW((void)parseJson(bad), JsonError) << bad;
        }
        std::string deep(65, '[');
        deep += std::string(65, ']');
        EXPECT_THROW((void)parseJson(deep), JsonError);
        std::string ok(64, '[');
        ok += std::string(64, ']');
        EXPECT_NO_THROW((void)parseJson(ok));
    }

    TEST(JsonReader, ErrorsSayWhere) {
        try {
            (void)parseJson("{\n  \"a\": 1,\n  \"b\" 2\n}");
            FAIL();
        }
        catch (const JsonError& e) {
            EXPECT_NE(std::string(e.what()).find("line 3, column 7"), std::string::npos) << e.what();
        }
    }

    TEST(JsonReader, KindChecks) {
        const JsonValue v = parseJson(R"({"n": 1.5, "neg": -1, "s": "x"})");
        EXPECT_THROW((void)v.at("s").asNumber(), JsonError);
        EXPECT_THROW((void)v.at("n").asUnsigned(), JsonError);
        EXPECT_THROW((void)v.at("neg").asUnsigned(), JsonError);
        EXPECT_THROW((void)v.at("n").asString(), JsonError);
        EXPECT_THROW((void)v.at("missing"), JsonError);
        EXPECT_THROW((void)v.at("s").find("x"), JsonError);
    }

}} // namespace icd::core
