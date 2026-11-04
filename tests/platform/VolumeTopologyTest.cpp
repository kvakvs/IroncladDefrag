// Setups that block moves (T122): LDM partitions mark dynamic disks, and the BitLocker states match Windows' own view.

#include "platform/win/VolumeTopology.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace icd::platform { namespace {

    // A GPT partition of the given type.
    PartitionType gptOf(const GUID& type) {
        PartitionType p;
        p.gpt = true;
        std::memcpy(p.gptType.data(), &type, sizeof(GUID));
        return p;
    }

    // Only LDM partitions (MBR type 0x42, the two GPT LDM types) make a disk dynamic.
    TEST(VolumeTopology, FindsLdmPartitions) {
        constexpr GUID basicData{0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}};
        constexpr GUID ldmMetadata{0x5808C8AA, 0x7E8F, 0x42E0, {0x85, 0xD2, 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3}};
        constexpr GUID ldmData{0xAF9B60A0, 0x1431, 0x4F62, {0xBC, 0x68, 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD}};
        PartitionType mbrNtfs;
        mbrNtfs.mbrType = 0x07;
        PartitionType mbrLdm;
        mbrLdm.mbrType = 0x42;
        EXPECT_FALSE(hasLdmPartition({}));
        EXPECT_FALSE(hasLdmPartition(std::vector{mbrNtfs, gptOf(basicData)}));
        EXPECT_TRUE(hasLdmPartition(std::vector{mbrNtfs, mbrLdm}));
        EXPECT_TRUE(hasLdmPartition(std::vector{gptOf(basicData), gptOf(ldmMetadata)}));
        EXPECT_TRUE(hasLdmPartition(std::vector{gptOf(ldmData)}));
        // An MBR type byte of 0x42 inside a GPT entry is not an LDM partition.
        PartitionType gptZero = gptOf(basicData);
        gptZero.mbrType = 0x42;
        EXPECT_FALSE(hasLdmPartition(std::vector{gptZero}));
    }

    // Volume keys ignore case, as the GUID paths from FindFirstVolumeW and WMI may differ in it.
    TEST(VolumeTopology, VolumeKeysIgnoreCase) {
        EXPECT_EQ(volumeKey(L"\\\\?\\Volume{AB-cd}\\"), L"\\\\?\\volume{ab-cd}\\");
    }

    // Runs a PowerShell command and returns its output lines.
    std::vector<std::string> powershellLines(const char* command) {
        const std::string line = std::string("powershell -NoProfile -Command \"") + command + "\"";
        FILE* pipe = _popen(line.c_str(), "rt");
        std::vector<std::string> lines;
        if (pipe == nullptr) {
            return lines;
        }
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string text = buffer;
            text.erase(text.find_last_not_of("\r\n ") + 1);
            if (!text.empty()) {
                lines.push_back(text);
            }
        }
        _pclose(pipe);
        return lines;
    }

    // Every physical disk answers the layout query, and every volume Windows lists for BitLocker has the same state
    // (administrator rights; this machine has basic disks only, so each is expected to be basic).
    TEST(VolumeTopology, MatchesWindows) {
        for (const std::string& id : powershellLines("Get-PhysicalDisk | ForEach-Object { $_.DeviceId }")) {
            const auto dynamic = isDynamicDisk(static_cast<std::uint32_t>(std::stoul(id)));
            ASSERT_TRUE(dynamic.has_value()) << "disk " << id;
            const auto style = powershellLines(
                ("(Get-Disk -Number " + id + ").PartitionStyle; @(Get-Partition -DiskNumber " + id +
                 " -ErrorAction SilentlyContinue | Where-Object { $_.MbrType -eq 0x42 -or $_.GptType -match "
                 "'5808c8aa|af9b60a0' }).Count")
                    .c_str());
            ASSERT_EQ(style.size(), 2u) << "disk " << id;
            EXPECT_EQ(*dynamic, style[1] != "0") << "disk " << id;
        }
        const auto expected = powershellLines(
            "Get-CimInstance -Namespace root/CIMV2/Security/MicrosoftVolumeEncryption -ClassName "
            "Win32_EncryptableVolume | ForEach-Object { '{0}|{1}' -f $_.DeviceID, $_.ConversionStatus }");
        const auto found = encryptedVolumes();
        EXPECT_EQ(found.size(), expected.size());
        for (const std::string& row : expected) {
            const std::size_t bar = row.find('|');
            const std::string id = row.substr(0, bar);
            const auto at = found.find(volumeKey(std::wstring(id.begin(), id.end())));
            ASSERT_NE(at, found.end()) << id;
            EXPECT_EQ(at->second, row.substr(bar + 1) != "0") << id;
        }
    }

}} // namespace icd::platform
