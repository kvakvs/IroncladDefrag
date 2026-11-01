// Volume enumeration against what Windows' own tools report (T042): every NTFS volume with a drive letter
// is compared with "fsutil fsinfo ntfsinfo". Needs administrator rights for the NTFS data; skipped without.

#include "platform/win/TextConvert.h"
#include "platform/win/Volumes.h"
#include "services/drives/SystemDriveProvider.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

namespace {

    using icd::platform::VolumeRecord;

    // "Key : value" lines of fsutil's output, keys trimmed.
    std::map<std::string, std::string> fsutilNtfsInfo(wchar_t letter) {
        const std::string command = std::string("fsutil fsinfo ntfsinfo ") + static_cast<char>(letter) + ":";
        std::map<std::string, std::string> values;
        FILE* pipe = _popen(command.c_str(), "rt");
        if (pipe == nullptr) {
            return values;
        }
        char line[512];
        while (std::fgets(line, sizeof(line), pipe) != nullptr) {
            const std::string text(line);
            const auto colon = text.find(" :");
            if (colon == std::string::npos) {
                continue;
            }
            std::string key = text.substr(0, colon);
            key.erase(key.find_last_not_of(' ') + 1);
            std::string value = text.substr(colon + 2);
            value.erase(0, value.find_first_not_of(' '));
            values[key] = value.substr(0, value.find_first_of(" \r\n"));
        }
        _pclose(pipe);
        return values;
    }

    // fsutil numbers: "0x00c0000" is hex, "488,185,855" decimal with separators.
    std::uint64_t parseNumber(std::string text) {
        if (text.starts_with("0x")) {
            return std::stoull(text.substr(2), nullptr, 16);
        }
        std::erase(text, ',');
        return std::stoull(text);
    }

    // The enumerated volume mounted at a drive letter, if any.
    bool isLetterRoot(const VolumeRecord& v) {
        return !v.mountPaths.empty() && v.mountPaths.front().size() == 3 && v.mountPaths.front()[1] == L':';
    }

    TEST(Volumes, ListsTheWindowsDriveWithItsDisk) {
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        const auto volumes = icd::platform::enumerateVolumes();
        ASSERT_FALSE(volumes.empty());
        bool found = false;
        for (const VolumeRecord& v : volumes) {
            ASSERT_FALSE(v.mountPaths.empty());
            EXPECT_FALSE(v.fileSystem.empty());
            if (v.mountPaths.front()[0] != windows[0]) {
                continue;
            }
            found = true;
            EXPECT_EQ(v.fileSystem, L"NTFS");
            EXPECT_GT(v.totalBytes, 0U);
            EXPECT_GE(v.layout.bytesPerCluster, 512U);
            ASSERT_TRUE(v.layout.diskNumber.has_value());
            EXPECT_GT(v.layout.partitionOffset, 0U);
            EXPECT_GE(v.layout.diskBytes, v.layout.partitionOffset + v.totalBytes);
        }
        EXPECT_TRUE(found);
        for (std::size_t i = 1; i < volumes.size(); ++i) {
            EXPECT_LT(volumes[i - 1].mountPaths.front(), volumes[i].mountPaths.front());
        }
    }

    TEST(Volumes, NtfsDataMatchesFsutil) {
        if (!icd::platform::isElevated()) {
            GTEST_SKIP() << "needs administrator rights";
        }
        int compared = 0;
        for (const VolumeRecord& v : icd::platform::enumerateVolumes()) {
            if (v.fileSystem != L"NTFS" || !isLetterRoot(v)) {
                continue;
            }
            SCOPED_TRACE(icd::platform::toUtf8(v.mountPaths.front()));
            ASSERT_TRUE(v.layout.ntfs.has_value()) << icd::platform::toUtf8(v.problem);
            const auto info = fsutilNtfsInfo(v.mountPaths.front()[0]);
            ASSERT_FALSE(info.empty());
            const auto& ntfs = *v.layout.ntfs;
            EXPECT_EQ(ntfs.serialNumber, parseNumber(info.at("NTFS Volume Serial Number")));
            EXPECT_EQ(std::to_string(ntfs.majorVersion) + "." + std::to_string(ntfs.minorVersion),
                      info.at("NTFS Version"));
            EXPECT_EQ(v.layout.totalClusters, parseNumber(info.at("Total Clusters")));
            EXPECT_EQ(v.layout.bytesPerSector, parseNumber(info.at("Bytes Per Sector")));
            EXPECT_EQ(v.layout.bytesPerCluster, parseNumber(info.at("Bytes Per Cluster")));
            EXPECT_EQ(ntfs.bytesPerFileRecord, parseNumber(info.at("Bytes Per FileRecord Segment")));
            EXPECT_EQ(ntfs.mftStartLcn, parseNumber(info.at("Mft Start Lcn")));
            EXPECT_EQ(ntfs.mft2StartLcn, parseNumber(info.at("Mft2 Start Lcn")));
            EXPECT_EQ(ntfs.mftZoneStart, parseNumber(info.at("Mft Zone Start")));
            EXPECT_EQ(ntfs.mftZoneEnd, parseNumber(info.at("Mft Zone End")));
            EXPECT_EQ(ntfs.totalReserved, parseNumber(info.at("Total Reserved Clusters")));
            ++compared;
        }
        EXPECT_GT(compared, 0);
    }

    TEST(SystemDriveProvider, ConvertsVolumeRecords) {
        VolumeRecord v;
        v.volumeName = L"\\\\?\\Volume{1234}\\";
        v.mountPaths = {L"E:\\", L"C:\\mnt\\e\\"};
        v.label = L"\u0424\u043e\u0442\u043e"; // Non-ASCII label survives the UTF-8 conversion.
        v.fileSystem = L"NTFS";
        v.totalBytes = 1000;
        v.freeBytes = 400;
        v.layout.bytesPerCluster = 4096;
        const auto drive = icd::services::toDriveInfo(v, false);
        EXPECT_EQ(drive.root, "E:\\");
        EXPECT_EQ(drive.label, "\xD0\xA4\xD0\xBE\xD1\x82\xD0\xBE");
        EXPECT_EQ(drive.usedBytes(), 600U);
        EXPECT_EQ(drive.layout.volumeName, "\\\\?\\Volume{1234}\\");
        EXPECT_TRUE(drive.badges.adminNeeded); // NTFS without NTFS data and not elevated.
        EXPECT_FALSE(icd::services::toDriveInfo(v, true).badges.adminNeeded);
    }

} // namespace
