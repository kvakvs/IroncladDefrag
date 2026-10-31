# Unit tests: icd_tests (GoogleTest), registered with CTest. Run them with test.bat.
include(FetchContent)
include(GoogleTest)

# Pinned GoogleTest; SYSTEM keeps its headers out of our /W4 /WX warnings.
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM)
set(gtest_force_shared_crt ON CACHE BOOL "Use /MD like the rest of the project" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(icd_tests
    core/DriveInfoTest.cpp
    core/FileSearchTest.cpp
    core/HitTestTest.cpp
    core/JobTypesTest.cpp
    core/LogTest.cpp
    core/LruCacheTest.cpp
    core/OverviewTest.cpp
    core/PyramidTest.cpp
    core/ResourceCapsTest.cpp
    core/RulerTest.cpp
    core/SelectionTest.cpp
    core/SmokeTest.cpp
    core/SnapshotTest.cpp
    core/StripesTest.cpp
    core/SyntheticVolumeTest.cpp
    core/TileRasterizerTest.cpp
    core/ViewportTest.cpp
    core/WorldTest.cpp
    platform/LogSinksTest.cpp
    platform/SystemResourcesTest.cpp
    services/FakeDriveProviderTest.cpp
    services/JobRunnerTest.cpp
    services/ThreadPoolTest.cpp
    services/WorkflowTest.cpp
)
target_link_libraries(icd_tests PRIVATE icd_core icd_platform icd_services GTest::gtest_main)
icd_target_defaults(icd_tests)
# Runs the exe at test time to list tests, so building never depends on running it.
gtest_discover_tests(icd_tests DISCOVERY_MODE PRE_TEST)
