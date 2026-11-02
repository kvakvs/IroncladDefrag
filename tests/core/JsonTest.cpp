// JSON writer (T049): structure, indentation and escaping.

#include "core/format/Json.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace {

    using icd::core::JsonWriter;

    TEST(Json, WritesNestedValues) {
        JsonWriter json;
        json.beginObject()
            .key("name")
            .value("vol")
            .key("count")
            .value(std::uint64_t{3})
            .key("delta")
            .value(std::int64_t{-2})
            .key("ratio")
            .value(0.25)
            .key("ok")
            .value(true)
            .key("none")
            .null()
            .key("list")
            .beginArray()
            .value(std::uint64_t{1})
            .value(std::uint64_t{2})
            .endArray()
            .key("empty")
            .beginArray()
            .endArray()
            .key("inner")
            .beginObject()
            .endObject()
            .endObject();
        EXPECT_EQ(json.str(),
                  "{\n  \"name\": \"vol\",\n  \"count\": 3,\n  \"delta\": -2,\n  \"ratio\": 0.25,\n"
                  "  \"ok\": true,\n  \"none\": null,\n  \"list\": [\n    1,\n    2\n  ],\n"
                  "  \"empty\": [],\n  \"inner\": {}\n}");
    }

    TEST(Json, EscapesStrings) {
        EXPECT_EQ(icd::core::jsonQuote("a\"b\\c"), "\"a\\\"b\\\\c\"");
        EXPECT_EQ(icd::core::jsonQuote("T:\\dir\\file"), "\"T:\\\\dir\\\\file\"");
        EXPECT_EQ(icd::core::jsonQuote("line\nnext\ttab\r"), "\"line\\nnext\\ttab\\r\"");
        EXPECT_EQ(icd::core::jsonQuote(std::string("\x01", 1)), "\"\\u0001\"");
        EXPECT_EQ(icd::core::jsonQuote("\xD1\x84"), "\"\xD1\x84\""); // UTF-8 passes through.
    }

    TEST(Json, NonFiniteNumbersBecomeNull) {
        JsonWriter json;
        json.beginArray().value(std::numeric_limits<double>::infinity()).value(std::nan("")).endArray();
        EXPECT_EQ(json.str(), "[\n  null,\n  null\n]");
    }

    TEST(Json, OneLineWriterHasNoNewlines) {
        icd::core::JsonWriter json(true);
        json.beginObject().key("a").value(std::uint64_t{1}).key("b").beginArray().value("x").null().endArray();
        json.endObject();
        EXPECT_EQ(json.str(), "{\"a\":1,\"b\":[\"x\",null]}");
    }

} // namespace
