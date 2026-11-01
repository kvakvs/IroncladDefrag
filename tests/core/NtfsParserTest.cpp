// MFT record parser (T060): update sequence fixups, run lists, attributes, attribute lists and names, on handcrafted
// records, on records captured from the `special` test scenario (tests/data/ntfs, made with
// tools/testvol/capture-mft-records.ps1), and on corrupted copies of both, which must never crash.

#include "NtfsRecordBuilder.h"

#include "core/ntfs/AttributeList.h"
#include "core/ntfs/Fixups.h"
#include "core/ntfs/MftRecord.h"
#include "core/ntfs/NtfsText.h"
#include "core/ntfs/RunList.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numeric>
#include <random>

namespace icd::core::ntfs { namespace {

    using test::encodeRuns;
    using test::RecordBuilder;
    using test::RunSpec;

    // Bytes from a list of values.
    std::vector<std::byte> bytes(std::initializer_list<int> values) {
        std::vector<std::byte> out;
        for (const int v : values) {
            out.push_back(static_cast<std::byte>(v));
        }
        return out;
    }

    // A captured record from tests/data/ntfs.
    std::vector<std::byte> captured(const std::string& name) {
        const auto path = std::filesystem::path(ICD_TEST_DATA_DIR) / "ntfs" / (name + ".bin");
        std::ifstream in(path, std::ios::binary);
        EXPECT_TRUE(in.good()) << path;
        const std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<std::byte> out(raw.size());
        std::ranges::transform(raw, out.begin(), [](char c) { return static_cast<std::byte>(c); });
        return out;
    }

    // Parses a captured record that must be valid.
    ParsedRecord parseCaptured(const std::string& name, std::uint64_t number) {
        auto raw = captured(name);
        ParsedRecord record;
        EXPECT_EQ(parseRecord(raw, number, record), RecordStatus::Ok) << name;
        return record;
    }

    // The attribute of a type and name, or null.
    const Attribute* find(const ParsedRecord& r, AttributeType type, const std::string& name = {}) {
        const auto it =
            std::ranges::find_if(r.attributes, [&](const Attribute& a) { return a.type == type && a.name == name; });
        return it == r.attributes.end() ? nullptr : &*it;
    }

    // Clusters on disk (holes left out).
    std::uint64_t allocated(const std::vector<DataRun>& runs) {
        return std::accumulate(runs.begin(), runs.end(), std::uint64_t{0},
                               [](std::uint64_t sum, const DataRun& r) { return sum + (r.hole() ? 0 : r.length); });
    }

    // --- Fixups ---------------------------------------------------------------------------------------------------

    TEST(NtfsFixups, RestoresBlockEnds) {
        auto raw = RecordBuilder(42).fileName(makeReference(5, 5), u"a.txt").build();
        ASSERT_EQ(load<std::uint16_t>(raw, 510), 7); // Stamped with the update sequence number.
        ASSERT_TRUE(applyFixups(raw));
        EXPECT_EQ(load<std::uint16_t>(raw, 510), 0); // The real bytes are back.
        EXPECT_EQ(load<std::uint16_t>(raw, 1022), 0);
    }

    TEST(NtfsFixups, RejectsTornBlock) {
        auto raw = RecordBuilder(42).build();
        raw[1022] = std::byte{0x55}; // The second block was not written with the first.
        const auto before = raw;
        EXPECT_FALSE(applyFixups(raw));
        EXPECT_EQ(raw, before);
    }

    TEST(NtfsFixups, RejectsMalformedArray) {
        auto raw = RecordBuilder(42).build();
        auto wrongCount = raw;
        wrongCount[6] = std::byte{2}; // One block covered, two present.
        EXPECT_FALSE(applyFixups(wrongCount));
        auto outside = raw;
        outside[4] = std::byte{0xFE};
        outside[5] = std::byte{0x01}; // Array past the first block's end.
        EXPECT_FALSE(applyFixups(outside));
        std::vector<std::byte> small(100);
        EXPECT_FALSE(applyFixups(small));
    }

    // --- Run lists ------------------------------------------------------------------------------------------------

    TEST(NtfsRunList, DecodesDeltasHolesAndBigValues) {
        const auto list = encodeRuns({{0x40000, 16}, {-1, 32}, {0x100, 1}, {0x7'0000'0000, 0x12'3456}});
        std::vector<DataRun> runs;
        ASSERT_TRUE(decodeRunList(list, 0, 16 + 32 + 1 + 0x12'3456 - 1, runs));
        const std::vector<DataRun> expected{
            {0, 0x40000, 16}, {16, -1, 32}, {48, 0x100, 1}, {49, 0x7'0000'0000, 0x12'3456}};
        EXPECT_EQ(runs, expected);
    }

    TEST(NtfsRunList, ContinuesAtStartVcnAndAppends) {
        std::vector<DataRun> runs{{0, 1, 1}};
        ASSERT_TRUE(decodeRunList(encodeRuns({{500, 10}}), 1000, 1009, runs));
        ASSERT_EQ(runs.size(), 2U);
        EXPECT_EQ(runs[1], (DataRun{1000, 500, 10}));
    }

    TEST(NtfsRunList, EmptyAttributeHasNoRuns) {
        std::vector<DataRun> runs;
        EXPECT_TRUE(decodeRunList(bytes({0}), 0, ~std::uint64_t{0}, runs));
        EXPECT_TRUE(runs.empty());
    }

    TEST(NtfsRunList, RejectsMalformedLists) {
        const std::vector<std::pair<const char*, std::vector<std::byte>>> cases{
            {"no terminator", bytes({0x11, 0x10, 0x20})},
            {"zero length", bytes({0x11, 0x00, 0x20, 0})},
            {"length size 9", bytes({0x19, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0})},
            {"delta size 9", bytes({0x91, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0})},
            {"negative LCN", bytes({0x11, 0x10, 0x80, 0})},
            {"cut off", bytes({0x21, 0x10})},
            {"zero length size", bytes({0x10, 0x20, 0})},
        };
        for (const auto& [what, list] : cases) {
            std::vector<DataRun> runs{{0, 1, 1}};
            EXPECT_FALSE(decodeRunList(list, 0, 15, runs)) << what;
            EXPECT_EQ(runs.size(), 1U) << what << ": output restored";
        }
    }

    TEST(NtfsRunList, RejectsWrongCoverage) {
        std::vector<DataRun> runs;
        EXPECT_FALSE(decodeRunList(encodeRuns({{100, 10}}), 0, 10, runs)); // Claims 11 clusters.
        EXPECT_FALSE(decodeRunList(encodeRuns({{100, 10}}), 0, 8, runs)); // Claims 9.
        EXPECT_TRUE(runs.empty());
    }

    TEST(NtfsRunList, RejectsLcnOverflow) {
        // +2^62 twice and once more passes int64 max.
        const auto big = bytes({0x81, 1, 0, 0, 0, 0, 0, 0, 0, 0x40});
        std::vector<std::byte> list;
        for (int i = 0; i < 3; ++i) {
            list.insert(list.end(), big.begin(), big.end());
        }
        list.push_back(std::byte{0});
        std::vector<DataRun> runs;
        EXPECT_FALSE(decodeRunList(list, 0, 2, runs));
    }

    // --- Names ----------------------------------------------------------------------------------------------------

    TEST(NtfsText, ConvertsBmpSurrogatePairsAndLoneSurrogates) {
        EXPECT_EQ(utf16ToUtf8(test::utf16(u"aé日")), "a\xc3\xa9\xe6\x97\xa5");
        EXPECT_EQ(utf16ToUtf8(test::utf16(u"\U0001F600")), "\xf0\x9f\x98\x80");
        EXPECT_EQ(utf16ToUtf8(test::utf16(std::u16string{u'x', char16_t(0xD800), u'y'})), "x\xef\xbf\xbdy");
        EXPECT_EQ(utf16ToUtf8(test::utf16(std::u16string{char16_t(0xDC00)})), "\xef\xbf\xbd");
        EXPECT_EQ(utf16ToUtf8(bytes({'a', 0, 'b'})), "a"); // Odd byte ignored.
    }

    // --- Attribute lists ------------------------------------------------------------------------------------------

    // One attribute list entry as bytes.
    std::vector<std::byte> listEntry(AttributeType type, std::uint64_t vcn, std::uint64_t reference, std::uint16_t id,
                                     const std::u16string& name) {
        std::vector<std::byte> e(26);
        const auto put = [&](std::size_t at, auto v) { std::memcpy(e.data() + at, &v, sizeof(v)); };
        put(0, static_cast<std::uint32_t>(type));
        e[6] = static_cast<std::byte>(name.size());
        e[7] = static_cast<std::byte>(26);
        put(8, vcn);
        put(16, reference);
        put(24, id);
        const auto text = test::utf16(name);
        e.insert(e.end(), text.begin(), text.end());
        e.resize((e.size() + 7) / 8 * 8);
        put(4, static_cast<std::uint16_t>(e.size()));
        return e;
    }

    TEST(NtfsAttributeList, ParsesEntries) {
        auto value = listEntry(AttributeType::Data, 0, makeReference(300, 2), 3, u"");
        const auto second = listEntry(AttributeType::Data, 0x1560, makeReference(301, 2), 0, u"big");
        value.insert(value.end(), second.begin(), second.end());
        std::vector<AttributeListEntry> entries;
        ASSERT_TRUE(parseAttributeList(value, entries));
        ASSERT_EQ(entries.size(), 2U);
        EXPECT_EQ(entries[0], (AttributeListEntry{AttributeType::Data, 0, makeReference(300, 2), 3, ""}));
        EXPECT_EQ(entries[1], (AttributeListEntry{AttributeType::Data, 0x1560, makeReference(301, 2), 0, "big"}));
    }

    TEST(NtfsAttributeList, RejectsMalformedEntries) {
        auto value = listEntry(AttributeType::Data, 0, 1, 0, u"name");
        std::vector<AttributeListEntry> entries;
        auto zeroLength = value;
        zeroLength[4] = zeroLength[5] = std::byte{0};
        EXPECT_FALSE(parseAttributeList(zeroLength, entries));
        auto longName = value;
        longName[6] = std::byte{200};
        EXPECT_FALSE(parseAttributeList(longName, entries));
        auto cut = value;
        cut.resize(cut.size() - 8);
        EXPECT_FALSE(parseAttributeList(cut, entries));
        EXPECT_TRUE(entries.empty());
    }

    // --- Handcrafted records --------------------------------------------------------------------------------------

    TEST(NtfsRecord, ParsesFileWithNamesStreamsAndRuns) {
        const std::uint64_t parent = makeReference(39, 3);
        auto raw = RecordBuilder(1234, 9)
                       .standard(100, 200, 300, dosSparse)
                       .fileName(parent, u"Long File Name.txt", NameSpace::Win32)
                       .fileName(parent, u"LONGFI~1.TXT", NameSpace::Dos)
                       .nonResident(AttributeType::Data, u"", 0, 47, encodeRuns({{5000, 16}, {-1, 16}, {9000, 16}}),
                                    48 * 4096, attributeSparse)
                       .resident(AttributeType::Data, u"tag", test::utf16(u"hello"))
                       .build();
        ParsedRecord r;
        ASSERT_EQ(parseRecord(raw, 1234, r), RecordStatus::Ok);
        EXPECT_EQ(r.reference(), makeReference(1234, 9));
        EXPECT_TRUE(r.inUse());
        EXPECT_FALSE(r.directory());
        EXPECT_TRUE(r.isBase());
        ASSERT_TRUE(r.standard);
        EXPECT_EQ(r.standard->created, 100);
        EXPECT_EQ(r.standard->modified, 200);
        EXPECT_EQ(r.standard->accessed, 300);
        EXPECT_EQ(r.standard->attributes, dosSparse);
        ASSERT_EQ(r.names.size(), 2U);
        EXPECT_EQ(r.names[0].name, "Long File Name.txt");
        EXPECT_EQ(r.names[0].parentReference, parent);
        EXPECT_FALSE(r.names[0].dosAlias());
        EXPECT_TRUE(r.names[1].dosAlias());
        const Attribute* data = find(r, AttributeType::Data);
        ASSERT_NE(data, nullptr);
        EXPECT_TRUE(data->nonResident);
        EXPECT_EQ(data->flags & attributeSparse, attributeSparse);
        EXPECT_EQ(data->dataSize, 48U * 4096);
        EXPECT_EQ(data->runs, (std::vector<DataRun>{{0, 5000, 16}, {16, -1, 16}, {32, 9000, 16}}));
        const Attribute* tag = find(r, AttributeType::Data, "tag");
        ASSERT_NE(tag, nullptr);
        EXPECT_FALSE(tag->nonResident);
        EXPECT_EQ(tag->dataSize, 10U);
        EXPECT_TRUE(tag->value.empty()); // $DATA values are not kept.
    }

    TEST(NtfsRecord, ParsesDirectoryAndExtensionRecords) {
        auto dir = RecordBuilder(77, 1, recordInUse | recordDirectory)
                       .fileName(makeReference(5, 5), u"photos")
                       .nonResident(AttributeType::IndexAllocation, u"$I30", 0, 3, encodeRuns({{700, 4}}), 4 * 4096)
                       .resident(AttributeType::Bitmap, u"$I30", bytes({0x0F, 0, 0, 0, 0, 0, 0, 0}))
                       .build();
        ParsedRecord r;
        ASSERT_EQ(parseRecord(dir, 77, r), RecordStatus::Ok);
        EXPECT_TRUE(r.directory());
        const Attribute* index = find(r, AttributeType::IndexAllocation, "$I30");
        ASSERT_NE(index, nullptr);
        EXPECT_EQ(index->runs, (std::vector<DataRun>{{0, 700, 4}}));
        const Attribute* bitmap = find(r, AttributeType::Bitmap, "$I30");
        ASSERT_NE(bitmap, nullptr);
        EXPECT_EQ(bitmap->value.size(), 8U);

        auto ext = RecordBuilder(78, 2)
                       .base(makeReference(1234, 9))
                       .nonResident(AttributeType::Data, u"", 48, 63, encodeRuns({{20000, 16}}), 0)
                       .build();
        ASSERT_EQ(parseRecord(ext, 78, r), RecordStatus::Ok);
        EXPECT_FALSE(r.isBase());
        EXPECT_EQ(r.baseReference, makeReference(1234, 9));
        ASSERT_EQ(r.attributes.size(), 1U);
        EXPECT_EQ(r.attributes[0].startVcn, 48U);
        EXPECT_EQ(r.attributes[0].runs, (std::vector<DataRun>{{48, 20000, 16}}));
    }

    TEST(NtfsRecord, ReportsHeaderProblems) {
        ParsedRecord r;
        std::vector<std::byte> empty(1024);
        EXPECT_EQ(parseRecord(empty, 1, r), RecordStatus::Empty);
        auto baad = RecordBuilder(1).build();
        std::memcpy(baad.data(), "BAAD", 4);
        EXPECT_EQ(parseRecord(baad, 1, r), RecordStatus::BadSignature);
        auto torn = RecordBuilder(1).build();
        torn[510] = std::byte{0x99};
        EXPECT_EQ(parseRecord(torn, 1, r), RecordStatus::BadFixup);
        auto other = RecordBuilder(2).build();
        EXPECT_EQ(parseRecord(other, 1, r), RecordStatus::BadHeader); // Record 2 read where record 1 should be.
        std::vector<std::byte> tiny(20);
        EXPECT_EQ(parseRecord(tiny, 1, r), RecordStatus::BadHeader);
    }

    TEST(NtfsRecord, ReportsAttributeProblems) {
        // A run list that claims more VCNs than it maps.
        auto badRuns = RecordBuilder(9).nonResident(AttributeType::Data, u"", 0, 99, encodeRuns({{10, 5}}), 0).build();
        ParsedRecord r;
        EXPECT_EQ(parseRecord(badRuns, 9, r), RecordStatus::BadAttribute);
        // A name that runs past the value.
        auto badName = RecordBuilder(9).fileName(5, u"abc").build();
        const std::size_t nameLength = 0x38 + 24 + 64; // First attribute + resident header + offset 64.
        badName[nameLength] = std::byte{200};
        EXPECT_EQ(parseRecord(badName, 9, r), RecordStatus::BadAttribute);
        // An attribute length past the record's used bytes.
        auto badLength = RecordBuilder(9).fileName(5, u"abc").build();
        badLength[0x38 + 5] = std::byte{0x10};
        EXPECT_EQ(parseRecord(badLength, 9, r), RecordStatus::BadAttribute);
    }

    // --- Captured records (special scenario, 4 KB clusters) -------------------------------------------------------

    TEST(NtfsCaptured, MftRecordMapsItself) {
        const ParsedRecord r = parseCaptured("mft", 0);
        ASSERT_EQ(r.names.size(), 1U);
        EXPECT_EQ(r.names[0].name, "$MFT");
        EXPECT_EQ(recordOf(r.names[0].parentReference), rootRecord);
        const Attribute* data = find(r, AttributeType::Data);
        ASSERT_NE(data, nullptr);
        ASSERT_FALSE(data->runs.empty());
        EXPECT_EQ(data->runs[0].lcn, 0x40000); // fsutil fsinfo ntfsinfo: Mft Start Lcn.
        EXPECT_GE(data->dataSize, 3U << 20U); // Mft Valid Data Length 3.25 MB.
        const Attribute* bitmap = find(r, AttributeType::Bitmap);
        ASSERT_NE(bitmap, nullptr);
        EXPECT_TRUE(bitmap->nonResident);
    }

    TEST(NtfsCaptured, RootIsDirectoryWithIndex) {
        const ParsedRecord r = parseCaptured("root", rootRecord);
        EXPECT_TRUE(r.directory());
        ASSERT_FALSE(r.names.empty());
        EXPECT_EQ(r.names[0].name, ".");
        EXPECT_EQ(recordOf(r.names[0].parentReference), rootRecord);
        ASSERT_NE(find(r, AttributeType::IndexAllocation, "$I30"), nullptr);
    }

    TEST(NtfsCaptured, ResidentFileHasNoRuns) {
        const ParsedRecord r = parseCaptured("resident", 40);
        ASSERT_EQ(r.names.size(), 1U);
        EXPECT_EQ(r.names[0].name, "r0.txt");
        EXPECT_EQ(recordOf(r.names[0].parentReference), 39U);
        const Attribute* data = find(r, AttributeType::Data);
        ASSERT_NE(data, nullptr);
        EXPECT_FALSE(data->nonResident);
        EXPECT_EQ(data->dataSize, 157U);
    }

    TEST(NtfsCaptured, CompressedFileHasUnitsAndHoles) {
        const ParsedRecord r = parseCaptured("compressed", 242);
        EXPECT_EQ(r.names[0].name, "text-0.log");
        ASSERT_TRUE(r.standard);
        EXPECT_NE(r.standard->attributes & dosCompressed, 0U);
        const Attribute* data = find(r, AttributeType::Data);
        ASSERT_NE(data, nullptr);
        EXPECT_NE(data->flags & attributeCompressionMask, 0);
        EXPECT_EQ(data->compressionUnit, 4); // 16-cluster units.
        EXPECT_EQ(data->dataSize, 8388608U);
        EXPECT_TRUE(std::ranges::any_of(data->runs, [](const DataRun& run) { return run.hole(); }));
        EXPECT_LT(allocated(data->runs), 8388608U / 4096); // Text compresses.
    }

    TEST(NtfsCaptured, SparseFileHasHoles) {
        const ParsedRecord r = parseCaptured("sparse", 246);
        EXPECT_NE(r.standard->attributes & dosSparse, 0U);
        const Attribute* data = find(r, AttributeType::Data);
        ASSERT_NE(data, nullptr);
        EXPECT_NE(data->flags & attributeSparse, 0);
        EXPECT_EQ(data->dataSize, 1073741824U);
        EXPECT_LT(allocated(data->runs), 1073741824U / 4096 / 2);
    }

    TEST(NtfsCaptured, HardLinksHaveThreeNames) {
        const ParsedRecord r = parseCaptured("hardlinks", 248);
        std::vector<std::string> names;
        for (const FileName& n : r.names) {
            if (!n.dosAlias()) {
                names.push_back(n.name);
            }
        }
        std::ranges::sort(names);
        EXPECT_EQ(names, (std::vector<std::string>{"hard-1.bin", "hard-2.bin", "target.bin"}));
    }

    TEST(NtfsCaptured, ReparsePoints) {
        const ParsedRecord link = parseCaptured("symlink", 250);
        EXPECT_NE(link.standard->attributes & dosReparsePoint, 0U);
        const ParsedRecord junction = parseCaptured("junction", 252);
        EXPECT_TRUE(junction.directory());
        EXPECT_NE(junction.standard->attributes & dosReparsePoint, 0U);
    }

    TEST(NtfsCaptured, NamedStreams) {
        const ParsedRecord r = parseCaptured("streams", 254);
        EXPECT_EQ(r.names[0].name, "host.txt");
        const Attribute* main = find(r, AttributeType::Data);
        const Attribute* big = find(r, AttributeType::Data, "big");
        const Attribute* tag = find(r, AttributeType::Data, "tag");
        ASSERT_TRUE(main && big && tag);
        EXPECT_EQ(main->dataSize, 300U);
        EXPECT_TRUE(big->nonResident);
        EXPECT_EQ(big->dataSize, 3145728U);
        EXPECT_EQ(allocated(big->runs), 3145728U / 4096);
        EXPECT_FALSE(tag->nonResident);
        EXPECT_EQ(tag->dataSize, 40U);
    }

    TEST(NtfsCaptured, UnicodeName) {
        const ParsedRecord r = parseCaptured("unicode", 270);
        EXPECT_EQ(r.names[0].name, "\xd1\x84\xd0\xbe\xd1\x82\xd0\xbe-\xe6\x97\xa5\xe6\x9c\xac.txt"); // фото-日本.txt
    }

    TEST(NtfsCaptured, EncryptedFile) {
        const ParsedRecord r = parseCaptured("efs", 278);
        EXPECT_NE(r.standard->attributes & dosEncrypted, 0U);
        EXPECT_NE(find(r, AttributeType::Data)->flags & attributeEncrypted, 0);
    }

    TEST(NtfsCaptured, LargeDirectoryIndex) {
        const ParsedRecord r = parseCaptured("bigdir", 279);
        EXPECT_TRUE(r.directory());
        const Attribute* index = find(r, AttributeType::IndexAllocation, "$I30");
        ASSERT_NE(index, nullptr);
        EXPECT_GT(allocated(index->runs), 1U);
    }

    // The 1,324-fragment file: its $DATA spreads over five extension records listed in a resident attribute list.
    TEST(NtfsCaptured, FragmentedFileSpansExtensionRecords) {
        const ParsedRecord base = parseCaptured("fragmented", 257);
        const Attribute* list = find(base, AttributeType::AttributeList);
        ASSERT_NE(list, nullptr);
        std::vector<AttributeListEntry> entries;
        ASSERT_TRUE(parseAttributeList(list->value, entries));
        std::vector<Attribute> segments;
        for (const Attribute& a : base.attributes) {
            if (a.type == AttributeType::Data) {
                segments.push_back(a);
            }
        }
        for (int i = 1; i <= 5; ++i) {
            auto raw = captured("fragmented-ext" + std::to_string(i));
            ParsedRecord ext;
            ASSERT_EQ(parseRecord(raw, load<std::uint32_t>(raw, 0x2C), ext), RecordStatus::Ok);
            EXPECT_EQ(recordOf(ext.baseReference), 257U);
            const bool listed = std::ranges::any_of(
                entries, [&](const AttributeListEntry& e) { return recordOf(e.recordReference) == ext.recordNumber; });
            EXPECT_TRUE(listed) << "record " << ext.recordNumber;
            for (const Attribute& a : ext.attributes) {
                segments.push_back(a);
            }
        }
        std::ranges::sort(segments, {}, &Attribute::startVcn);
        std::uint64_t vcn = 0;
        std::size_t runs = 0;
        for (const Attribute& s : segments) {
            EXPECT_EQ(s.startVcn, vcn); // Segments continue each other.
            vcn = s.lastVcn + 1;
            runs += s.runs.size();
        }
        EXPECT_EQ(vcn, 100663296U / 4096); // 96 MB in 4 KB clusters.
        EXPECT_GE(runs, 1324U);
    }

    // --- Corrupt input --------------------------------------------------------------------------------------------

    // Flips random bytes of valid records many times; parsing must return some status without crashing or reading
    // out of bounds (the debug STL checks every span access).
    TEST(NtfsCorrupt, RandomDamageNeverCrashes) {
        std::vector<std::vector<std::byte>> samples;
        for (const char* name : {"mft", "root", "compressed", "streams", "fragmented", "bigdir", "hardlinks"}) {
            samples.push_back(captured(name));
        }
        std::mt19937 random(12345);
        ParsedRecord r;
        std::size_t ok = 0;
        for (int round = 0; round < 20000; ++round) {
            auto raw = samples[static_cast<std::size_t>(round) % samples.size()];
            const int flips = 1 + static_cast<int>(random() % 8);
            for (int f = 0; f < flips; ++f) {
                raw[random() % raw.size()] = static_cast<std::byte>(random());
            }
            ok += parseRecord(raw, load<std::uint32_t>(raw, 0x2C), r) == RecordStatus::Ok ? 1 : 0;
        }
        EXPECT_GT(ok, 0U); // Some damage hits unused bytes and the record still parses.
    }

    TEST(NtfsCorrupt, TruncatedRecordsNeverCrash) {
        const auto full = captured("streams");
        ParsedRecord r;
        for (std::size_t size = 0; size < full.size(); size += 7) {
            std::vector<std::byte> part(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
            EXPECT_NE(parseRecord(part, 254, r), RecordStatus::Ok) << size;
        }
    }

    TEST(NtfsCorrupt, RandomRunListsNeverCrash) {
        std::mt19937 random(99);
        std::vector<DataRun> runs;
        for (int round = 0; round < 20000; ++round) {
            std::vector<std::byte> list(1 + random() % 40);
            for (auto& b : list) {
                b = static_cast<std::byte>(random());
            }
            runs.clear();
            (void)decodeRunList(list, random() % 100, random() % 1000, runs);
        }
        SUCCEED();
    }

}} // namespace icd::core::ntfs
