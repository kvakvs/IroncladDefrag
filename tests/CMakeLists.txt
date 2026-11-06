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
    core/AccessTimeHeuristicTest.cpp
    core/ActivityTest.cpp
    core/BitmapToRunsTest.cpp
    core/BitTreeTest.cpp
    core/BudgetTest.cpp
    core/CatalogTest.cpp
    core/ColorModesTest.cpp
    core/CycleGuardTest.cpp
    core/DefragOnlyTest.cpp
    core/DriveInfoTest.cpp
    core/FastFillTest.cpp
    core/FastLevelTest.cpp
    core/FileSearchTest.cpp
    core/GridTest.cpp
    core/HitTestTest.cpp
    core/HeadOrderTest.cpp
    core/HoleSetTest.cpp
    core/JobTypesTest.cpp
    core/JsonReaderTest.cpp
    core/JsonTest.cpp
    core/LogTest.cpp
    core/LevelsTest.cpp
    core/LowerBoundTest.cpp
    core/LruCacheTest.cpp
    core/MoveCallsTest.cpp
    core/MovePiecesTest.cpp
    core/MapUpdateTest.cpp
    core/MftLayoutTest.cpp
    core/MoveOrderingTest.cpp
    core/NtfsParserTest.cpp
    core/NtfsRecordBuilder.h
    core/OptimalityTest.cpp
    core/OverviewTest.cpp
    core/PackToBackTest.cpp
    core/PlanOverlayTest.cpp
    core/PlannerTestUtil.h
    core/presets/PresetsTest.cpp
    core/presets/PresetsTest.cpp
    core/PyramidTest.cpp
    core/ResourceCapsTest.cpp
    core/RuleEngineTest.cpp
    core/RulerTest.cpp
    core/ScanReconcileTest.cpp
    core/SelectionTest.cpp
    core/SmokeTest.cpp
    core/SnapshotPatchTest.cpp
    core/SnapshotStatsTest.cpp
    core/SnapshotTest.cpp
    core/SortZoneTest.cpp
    core/StripesTest.cpp
    core/SyntheticVolumeTest.cpp
    core/TargetLayoutTest.cpp
    core/TileRasterizerTest.cpp
    core/ValidatorTest.cpp
    core/VssAlignTest.cpp
    core/ViewportTest.cpp
    core/WorldTest.cpp
    core/ZoneScriptTest.cpp
    platform/ChildProcessTest.cpp
    platform/FolderWalkTest.cpp
    platform/LastAccessSettingTest.cpp
    platform/OptimizeStatisticsTest.cpp
    platform/LogSinksTest.cpp
    platform/MoveFileTest.cpp
    platform/ScheduledOptimizeTest.cpp
    platform/ShadowCopiesTest.cpp
    platform/StoragePropertiesTest.cpp
    platform/SystemResourcesTest.cpp
    platform/UsnJournalTest.cpp
    platform/VolumeBitmapTest.cpp
    platform/VolumesTest.cpp
    platform/VolumeTopologyTest.cpp
    services/AnalysisJobTest.cpp
    services/AnalysisReportTest.cpp
    services/CatchUpPatchTest.cpp
    services/ChurnStatsTest.cpp
    services/ClassificationTest.cpp
    services/ApiScannerTest.cpp
    services/ExecuteJobTest.cpp
    services/ExecuteReportTest.cpp
    services/ExecutionLogTest.cpp
    services/ExecutorTest.cpp
    services/FakeDriveProviderTest.cpp
    services/FileDefragTest.cpp
    services/JobRunnerTest.cpp
    services/LiveMapTest.cpp
    services/MemoryModesTest.cpp
    services/MftMergeTest.cpp
    services/MftScannerTest.cpp
    services/MftSecondPassTest.cpp
    services/PlanJobTest.cpp
    services/PlanReportTest.cpp
    services/PlannedBed.h
    services/RevalidateTest.cpp
    services/SimDevice.h
    services/SimVolume.h
    services/SettingsTest.cpp
    services/StrategyLibraryTest.cpp
    services/ShellActionsTest.cpp
    services/SnapshotStoreTest.cpp
    services/ThreadPoolTest.cpp
    services/UsnCatchUpTest.cpp
    services/TrimJobTest.cpp
    services/WorkflowTest.cpp
)
target_link_libraries(icd_tests PRIVATE icd_core icd_platform icd_services GTest::gtest_main)
icd_target_defaults(icd_tests)
# Captured test data (MFT records and the like) is read from the source tree.
target_compile_definitions(icd_tests PRIVATE ICD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# Test helpers shared between test directories ("core/NtfsRecordBuilder.h").
target_include_directories(icd_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Tests that read real volumes and disks (administrator rights, mounted ICD-TEST volumes, the Windows drive): slower
# and machine-dependent, so test.bat runs them only on demand ("test.bat Release slow"). Every other test is "fast" and
# runs on each build. Add each new volume test to this list.
set(ICD_SLOW_TESTS
    "UsnJournal.EnumeratesEveryFileOnTestVolumes:UsnJournal.ReadsTheWindowsDriveJournal"
    ":ApiScanner.*:MftScanner.*:AnalysisJob.AnalysesTheSpecialVolume:AnalysisJob.CancelStopsTheScan"
    ":VolumeBitmap.*:Volumes.ListsTheWindowsDriveWithItsDisk:Volumes.NtfsDataMatchesFsutil"
    ":StorageProperties.MatchesGetPhysicalDisk:ShadowCopies.DetectsTheVssScenario:UsnCatchUp.SnapshotDraftsFinishIntoTheSameSnapshot"
    ":ClusterMove.MovesFilesOnTestVolumes:VolumeTopology.MatchesWindows:ScheduledOptimize.ReadsTheWindowsTask")
string(JOIN "" ICD_SLOW_TESTS ${ICD_SLOW_TESTS})
# Runs the exe at test time to list tests, so building never depends on running it.
gtest_discover_tests(icd_tests DISCOVERY_MODE PRE_TEST TEST_FILTER "-${ICD_SLOW_TESTS}" PROPERTIES LABELS fast)
gtest_discover_tests(icd_tests DISCOVERY_MODE PRE_TEST TEST_FILTER "${ICD_SLOW_TESTS}" PROPERTIES LABELS slow)
