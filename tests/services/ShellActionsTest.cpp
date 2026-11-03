// Full paths for the map's "Show in Explorer" and "Copy path": a volume root and a snapshot path join with exactly one
// backslash, for drive letters and mount folders alike.

#include "services/ShellActions.h"

#include <gtest/gtest.h>

namespace icd::services { namespace {

    TEST(ShellActions, VolumePathJoinsRootAndPath) {
        EXPECT_EQ(volumePath("X:\\", "\\Photos\\a.jpg"), "X:\\Photos\\a.jpg");
        EXPECT_EQ(volumePath("X:", "\\Photos\\a.jpg"), "X:\\Photos\\a.jpg");
        EXPECT_EQ(volumePath("C:\\Mount\\Data\\", "\\a.jpg"), "C:\\Mount\\Data\\a.jpg");
        EXPECT_EQ(volumePath("X:\\", "Photos"), "X:\\Photos");
        EXPECT_EQ(volumePath("X:\\", "\\"), "X:\\"); // The root directory itself.
    }

}} // namespace icd::services
