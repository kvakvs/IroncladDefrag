// USN enumeration on real volumes (T046): every file and directory a directory walk finds on a test volume must
// be enumerated with the same file reference, name and parent; the journal state is read where one exists.
// Needs administrator rights.

#include "platform/win/Handle.h"
#include "platform/win/TextConvert.h"
#include "platform/win/UsnJournal.h"
#include "platform/win/Volumes.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace {

    using icd::platform::UsnFileEntry;

    // What the directory walk saw of one entry.
    struct WalkedEntry {
        std::uint64_t reference = 0; // From GetFileInformationByHandle.
        std::uint64_t parent = 0; // Reference of the directory it was found in.
        std::string name; // Name in that directory.
        std::uint32_t links = 0; // Hard link count; names and parents are compared only for single links.
    };

    // File reference and link count of a path, without following reparse points.
    bool identify(const std::wstring& path, std::uint64_t& reference, std::uint32_t& links) {
        const icd::platform::UniqueHandle h(
            CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!h || GetFileInformationByHandle(h.get(), &info) == FALSE) {
            return false;
        }
        reference = (std::uint64_t{info.nFileIndexHigh} << 32U) | info.nFileIndexLow;
        links = info.nNumberOfLinks;
        return true;
    }

    // Walks a directory tree (not into reparse points) and records every entry.
    void walk(const std::wstring& dir, std::uint64_t dirReference, std::vector<WalkedEntry>& out) {
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE) {
            return;
        }
        do {
            const std::wstring name = data.cFileName;
            if (name == L"." || name == L".." || name == L"System Volume Information") {
                continue;
            }
            const std::wstring path = dir + L"\\" + name;
            WalkedEntry entry{.parent = dirReference, .name = icd::platform::toUtf8(name)};
            if (!identify(path, entry.reference, entry.links)) {
                continue;
            }
            out.push_back(entry);
            const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (directory && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                walk(path, entry.reference, out);
            }
        }
        while (FindNextFileW(find, &data) != FALSE);
        FindClose(find);
    }

    TEST(UsnJournal, EnumeratesEveryFileOnTestVolumes) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        int checked = 0;
        for (const auto& v : icd::platform::enumerateVolumes()) {
            if (v.fileSystem != L"NTFS" || !v.label.starts_with(L"ICD-TEST")) {
                continue;
            }
            // Scenario volumes only: walking a million-file scale volume takes far too long for a unit test, and the
            // scanner cross-check (check-scanners.ps1 -ScaleDrive) covers it.
            constexpr std::uint64_t maxRecords = 100'000;
            const auto& ntfs = v.layout.ntfs;
            if (ntfs && ntfs->bytesPerFileRecord != 0 &&
                ntfs->mftValidDataLength / ntfs->bytesPerFileRecord > maxRecords) {
                continue;
            }
            const std::wstring root = v.mountPaths.front();
            SCOPED_TRACE(icd::platform::toUtf8(root));
            const auto volume = icd::platform::openVolume(v.volumeName, GENERIC_READ);
            ASSERT_TRUE(volume.valid());
            std::unordered_map<std::uint64_t, UsnFileEntry> enumerated;
            icd::platform::enumerateUsnFiles(volume.get(), [&](std::span<const UsnFileEntry> batch) {
                for (const auto& e : batch) {
                    EXPECT_TRUE(enumerated.emplace(e.fileReference, e).second) << "duplicate " << e.name;
                }
            });
            std::uint64_t rootReference = 0;
            std::uint32_t rootLinks = 0;
            ASSERT_TRUE(identify(L"\\\\?\\" + root, rootReference, rootLinks));
            std::vector<WalkedEntry> walked;
            walk(L"\\\\?\\" + root.substr(0, root.size() - 1), rootReference, walked);
            ASSERT_GT(walked.size(), 10U);
            for (const WalkedEntry& w : walked) {
                const auto it = enumerated.find(w.reference);
                ASSERT_NE(it, enumerated.end()) << w.name;
                if (w.links == 1) {
                    EXPECT_EQ(it->second.name, w.name);
                    EXPECT_EQ(it->second.parentReference, w.parent) << w.name;
                }
            }
            ++checked;
        }
        if (checked == 0) {
            GTEST_SKIP() << "no ICD-TEST volume mounted (tools/testvol/make-testvol.ps1)";
        }
    }

    TEST(UsnJournal, ReadsTheWindowsDriveJournal) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        const auto volume = icd::platform::openVolume(std::wstring(windows, 3), GENERIC_READ);
        ASSERT_TRUE(volume.valid());
        const auto journal = icd::platform::queryUsnJournal(volume.get());
        ASSERT_TRUE(journal.has_value()); // Windows keeps a journal on its own drive.
        EXPECT_NE(journal->journalId, 0U);
        EXPECT_GT(journal->nextUsn, journal->firstUsn);
        EXPECT_GE(journal->firstUsn, journal->lowestValidUsn);
    }

    TEST(UsnJournal, RecordNumbers) {
        EXPECT_EQ(icd::platform::mftRecordOf(0x0005'0000'0000'002AULL), 42U);
        EXPECT_EQ(icd::platform::mftRecordOf(0x0000'0000'0000'0005ULL), 5U);
    }

} // namespace
