// Media detection against Windows' own view (T043): every physical disk Get-PhysicalDisk calls HDD or SSD
// must classify the same way, and virtual disks (the VHDX test bed) as Virtual.

#include "platform/win/StorageProperties.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <sstream>
#include <string>

namespace {

    // One Get-PhysicalDisk row.
    struct WindowsDisk {
        unsigned number = 0; // DeviceId.
        std::string mediaType; // "HDD", "SSD", "Unspecified".
        std::string busType; // "SATA", "NVMe", "File Backed Virtual", ...
    };

    // Asks PowerShell for every physical disk as "id|media|bus" lines.
    std::vector<WindowsDisk> windowsDisks() {
        FILE* pipe = _popen("powershell -NoProfile -Command \"Get-PhysicalDisk | ForEach-Object { "
                            "'{0}|{1}|{2}' -f $_.DeviceId, $_.MediaType, $_.BusType }\"",
                            "rt");
        std::vector<WindowsDisk> disks;
        if (pipe == nullptr) {
            return disks;
        }
        char line[256];
        while (std::fgets(line, sizeof(line), pipe) != nullptr) {
            std::istringstream in(line);
            WindowsDisk disk;
            std::string id;
            if (std::getline(in, id, '|') && std::getline(in, disk.mediaType, '|') && std::getline(in, disk.busType)) {
                disk.number = static_cast<unsigned>(std::stoul(id));
                disk.busType.erase(disk.busType.find_last_not_of("\r\n ") + 1);
                disks.push_back(disk);
            }
        }
        _pclose(pipe);
        return disks;
    }

    TEST(StorageProperties, MatchesGetPhysicalDisk) {
        const auto disks = windowsDisks();
        ASSERT_FALSE(disks.empty());
        for (const WindowsDisk& disk : disks) {
            SCOPED_TRACE("disk " + std::to_string(disk.number) + " " + disk.mediaType + " " + disk.busType);
            const auto facts = icd::platform::queryDiskStorage(disk.number);
            const auto media = icd::core::classifyMedia(facts);
            if (disk.busType == "File Backed Virtual") {
                EXPECT_EQ(media, icd::core::MediaType::Virtual);
            }
            else if (disk.mediaType == "HDD") {
                EXPECT_EQ(media, icd::core::MediaType::Hdd);
            }
            else if (disk.mediaType == "SSD") {
                EXPECT_EQ(media, icd::core::MediaType::Ssd);
            }
            if (disk.busType == "SATA" || disk.busType == "NVMe" || disk.busType == "USB" || disk.busType == "SAS") {
                EXPECT_EQ(facts.bus, disk.busType);
            }
        }
    }

    TEST(StorageProperties, NamesBusTypes) {
        EXPECT_EQ(icd::platform::busTypeName(11), "SATA");
        EXPECT_EQ(icd::platform::busTypeName(17), "NVMe");
        EXPECT_EQ(icd::platform::busTypeName(15), "Virtual");
        EXPECT_EQ(icd::platform::busTypeName(0), "");
        EXPECT_EQ(icd::platform::busTypeName(99), "bus 99");
    }

} // namespace
