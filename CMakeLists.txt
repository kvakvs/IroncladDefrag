cmake_minimum_required(VERSION 3.28)
project(IroncladDefrag VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "IroncladDefrag builds only with MSVC for Windows x64.")
endif()

# Only Debug and Release exist; wx ships DLLs for exactly these two.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/MP) # Ninja already compiles files in parallel.
endif()

# Pinned wxWidgets version; tools/fetch-wx.ps1 pins the same version and its archive hashes.
set(ICD_WX_VERSION "3.3.4")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(IcdCompilerOptions)
include(IcdWxWidgets)

# Version header generated from project(VERSION), shared by code and resource scripts.
configure_file(src/core/Version.h.in "${CMAKE_BINARY_DIR}/generated/core/Version.h" @ONLY)

# The built-in class catalog (data/classes.json) compiled into icd_core, so no data file is looked up at run time.
set(ICD_CATALOG_SOURCE "${CMAKE_BINARY_DIR}/generated/core/classify/BuiltinCatalog.cpp")
add_custom_command(
    OUTPUT "${ICD_CATALOG_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${CMAKE_SOURCE_DIR}/data/classes.json -DOUTPUT=${ICD_CATALOG_SOURCE}
            -DNAMESPACE=icd::core::classify -DFUNCTION=builtinCatalogJson -P ${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake
    DEPENDS data/classes.json cmake/EmbedText.cmake
    COMMENT "Embedding data/classes.json"
    VERBATIM)

# --- icd_core: wx-free, Win32-free model and algorithms -------------------------------------------
add_library(icd_core STATIC
    src/core/Version.h.in
    src/core/classify/AccessTimeHeuristic.cpp
    src/core/classify/AccessTimeHeuristic.h
    src/core/classify/Catalog.cpp
    src/core/classify/Catalog.h
    src/core/classify/RuleEngine.cpp
    src/core/classify/RuleEngine.h
    src/core/exec/Executor.cpp
    src/core/exec/Executor.h
    src/core/format/ByteSize.cpp
    src/core/format/ByteSize.h
    src/core/format/Count.cpp
    src/core/format/Count.h
    src/core/format/DriveText.cpp
    src/core/format/DriveText.h
    src/core/format/Json.cpp
    src/core/format/Json.h
    src/core/format/JsonReader.cpp
    src/core/format/JsonReader.h
    src/core/format/Duration.cpp
    src/core/format/Duration.h
    src/core/job/CancelToken.cpp
    src/core/job/CancelToken.h
    src/core/job/PauseGate.cpp
    src/core/job/PauseGate.h
    src/core/job/Progress.cpp
    src/core/job/Progress.h
    src/core/job/StageClock.cpp
    src/core/job/StageClock.h
    src/core/log/Log.h
    src/core/log/LogFormat.cpp
    src/core/log/Logger.cpp
    src/core/map/BinSummary.cpp
    src/core/map/BinSummary.h
    src/core/map/ColorModes.cpp
    src/core/map/ColorModes.h
    src/core/map/Grid.cpp
    src/core/map/Grid.h
    src/core/map/HitTest.cpp
    src/core/map/HitTest.h
    src/core/map/MapData.cpp
    src/core/map/MapData.h
    src/core/map/Overview.cpp
    src/core/map/Overview.h
    src/core/map/Palette.cpp
    src/core/map/Palette.h
    src/core/map/Pyramid.cpp
    src/core/map/Pyramid.h
    src/core/map/Ruler.cpp
    src/core/map/Ruler.h
    src/core/map/Selection.cpp
    src/core/map/Selection.h
    src/core/map/TileRasterizer.cpp
    src/core/map/TileRasterizer.h
    src/core/map/Viewport.cpp
    src/core/map/Viewport.h
    src/core/map/World.cpp
    src/core/map/World.h
    src/core/model/DriveInfo.cpp
    src/core/model/DriveInfo.h
    src/core/model/VolumeLayout.h
    src/core/ntfs/AttributeList.cpp
    src/core/ntfs/AttributeList.h
    src/core/ntfs/Fixups.cpp
    src/core/ntfs/Fixups.h
    src/core/ntfs/MftLayout.cpp
    src/core/ntfs/MftLayout.h
    src/core/ntfs/MftRecord.cpp
    src/core/ntfs/MftRecord.h
    src/core/ntfs/NtfsFormat.h
    src/core/ntfs/NtfsText.cpp
    src/core/ntfs/NtfsText.h
    src/core/ntfs/RunList.cpp
    src/core/ntfs/RunList.h
    src/core/planner/CandidateFilter.cpp
    src/core/planner/CandidateFilter.h
    src/core/planner/DefragOnly.cpp
    src/core/planner/DefragOnly.h
    src/core/planner/FastFill.cpp
    src/core/planner/FastFill.h
    src/core/planner/HoleSet.cpp
    src/core/planner/HoleSet.h
    src/core/planner/LowerBound.cpp
    src/core/planner/LowerBound.h
    src/core/planner/MakeRoom.cpp
    src/core/planner/MakeRoom.h
    src/core/planner/MoveOrdering.cpp
    src/core/planner/MoveOrdering.h
    src/core/planner/PackToBack.cpp
    src/core/planner/PackToBack.h
    src/core/planner/Placement.cpp
    src/core/planner/Placement.h
    src/core/planner/Plan.cpp
    src/core/planner/Plan.h
    src/core/planner/SortZone.cpp
    src/core/planner/SortZone.h
    src/core/planner/TargetLayout.cpp
    src/core/planner/TargetLayout.h
    src/core/planner/ZoneAssignment.cpp
    src/core/planner/Validator.cpp
    src/core/planner/Validator.h
    src/core/planner/ZoneAssignment.h
    src/core/resources/MemoryPlanner.cpp
    src/core/resources/MemoryPlanner.h
    src/core/resources/ResourceCaps.cpp
    src/core/resources/ResourceCaps.h
    src/core/snapshot/BitmapToRuns.cpp
    src/core/snapshot/BitmapToRuns.h
    src/core/snapshot/DirectoryTree.cpp
    src/core/snapshot/DirectoryTree.h
    src/core/snapshot/ExtentIndex.cpp
    src/core/snapshot/ExtentIndex.h
    src/core/snapshot/FileTable.h
    src/core/snapshot/FileSearch.cpp
    src/core/snapshot/FileSearch.h
    src/core/snapshot/FreeRunList.cpp
    src/core/snapshot/FreeRunList.h
    src/core/snapshot/NamePool.cpp
    src/core/snapshot/NamePool.h
    src/core/snapshot/ScanReconcile.cpp
    src/core/snapshot/ScanReconcile.h
    src/core/snapshot/SnapshotStats.cpp
    src/core/snapshot/SnapshotStats.h
    src/core/snapshot/Snapshot.cpp
    src/core/snapshot/Snapshot.h
    src/core/snapshot/SnapshotBuilder.cpp
    src/core/snapshot/SnapshotBuilder.h
    src/core/snapshot/SnapshotIO.cpp
    src/core/snapshot/SnapshotIO.h
    src/core/snapshot/SnapshotPatch.cpp
    src/core/snapshot/SnapshotPatch.h
    src/core/snapshot/Stripes.cpp
    src/core/snapshot/Stripes.h
    src/core/snapshot/Types.h
    src/core/strategy/ZoneScript.cpp
    src/core/strategy/ZoneScript.h
    src/core/synth/SynthRandom.h
    src/core/util/Crc32.h
    src/core/util/LargeArray.h
    src/core/util/LruCache.h
    src/core/synth/SyntheticVolume.cpp
    src/core/synth/SyntheticVolume.h
    data/classes.json
    "${ICD_CATALOG_SOURCE}"
)
target_include_directories(icd_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")
icd_target_defaults(icd_core)

# --- icd_platform: narrow Win32 wrappers ---------------------------------------------------------
add_library(icd_platform STATIC
    src/platform/win/ChildProcess.cpp
    src/platform/win/ChildProcess.h
    src/platform/win/Console.cpp
    src/platform/win/LastAccessSetting.cpp
    src/platform/win/LastAccessSetting.h
    src/platform/win/Compression.cpp
    src/platform/win/Compression.h
    src/platform/win/Console.h
    src/platform/win/Handle.h
    src/platform/win/LogSinks.cpp
    src/platform/win/LogSinks.h
    src/platform/win/MappedMemory.cpp
    src/platform/win/MappedMemory.h
    src/platform/win/MftReader.cpp
    src/platform/win/MftReader.h
    src/platform/win/SystemResources.cpp
    src/platform/win/SystemResources.h
    src/platform/win/Privileges.cpp
    src/platform/win/Privileges.h
    src/platform/win/RetrievalPointers.cpp
    src/platform/win/RetrievalPointers.h
    src/platform/win/StorageProperties.cpp
    src/platform/win/StorageProperties.h
    src/platform/win/TextConvert.cpp
    src/platform/win/TextConvert.h
    src/platform/win/UsnJournal.cpp
    src/platform/win/UsnJournal.h
    src/platform/win/VolumeBitmap.cpp
    src/platform/win/VolumeBitmap.h
    src/platform/win/Volumes.cpp
    src/platform/win/Volumes.h
    src/platform/win/WinError.cpp
    src/platform/win/WinError.h
)
target_link_libraries(icd_platform PUBLIC icd_core shell32 ole32 cabinet)
icd_target_defaults(icd_platform)

# --- icd_services: jobs, thread pool, orchestration ----------------------------------------------
add_library(icd_services STATIC
    src/services/activity/ChurnStats.cpp
    src/services/activity/ChurnStats.h
    src/services/AppInfo.cpp
    src/services/AppInfo.h
    src/services/AppLogging.cpp
    src/services/AppLogging.h
    src/services/AppServices.cpp
    src/services/AppServices.h
    src/services/drives/FakeDriveProvider.cpp
    src/services/drives/FakeDriveProvider.h
    src/services/drives/IDriveProvider.h
    src/services/drives/SystemDriveProvider.cpp
    src/services/drives/SystemDriveProvider.h
    src/services/exec/ThreadPool.cpp
    src/services/exec/ThreadPool.h
    src/services/job/FakeJob.cpp
    src/services/job/FakeJob.h
    src/services/job/JobRunner.cpp
    src/services/job/JobRunner.h
    src/services/scan/AnalysisJob.cpp
    src/services/scan/AnalysisJob.h
    src/services/scan/AnalysisReport.cpp
    src/services/scan/AnalysisReport.h
    src/services/scan/Classification.cpp
    src/services/scan/Classification.h
    src/services/scan/ApiScanner.cpp
    src/services/scan/ApiScanner.h
    src/services/scan/MftMerge.cpp
    src/services/scan/MftMerge.h
    src/services/scan/MftRows.cpp
    src/services/scan/MftRows.h
    src/services/scan/MftScanner.cpp
    src/services/scan/MftScanner.h
    src/services/scan/MftSecondPass.cpp
    src/services/scan/MftSecondPass.h
    src/services/scan/ScanCompare.cpp
    src/services/scan/ScanCompare.h
    src/services/scan/ScanRules.cpp
    src/services/scan/ScanRules.h
    src/services/scan/ScanStats.h
    src/services/settings/Settings.cpp
    src/services/settings/Settings.h
    src/services/settings/SettingsStore.cpp
    src/services/settings/SettingsStore.h
    src/services/snapshot/MappedStorage.cpp
    src/services/snapshot/MappedStorage.h
    src/services/snapshot/SnapshotStore.cpp
    src/services/snapshot/SnapshotStore.h
    src/services/snapshot/UsnCatchUp.cpp
    src/services/snapshot/UsnCatchUp.h
    src/services/trim/TrimJob.cpp
    src/services/trim/TrimJob.h
    src/services/workflow/FakeResults.cpp
    src/services/workflow/FakeResults.h
    src/services/workflow/SyntheticAnalysis.cpp
    src/services/workflow/SyntheticAnalysis.h
    src/services/workflow/Workflow.cpp
    src/services/workflow/Workflow.h
)
target_link_libraries(icd_services PUBLIC icd_core icd_platform)
icd_target_defaults(icd_services)

# --- IroncladDefrag: wxWidgets GUI ---------------------------------------------------------------
add_executable(IroncladDefrag WIN32
    src/main.cpp
    src/precompiled.h
    src/ui/App.cpp
    src/ui/Accelerators.cpp
    src/ui/Accelerators.h
    src/ui/App.h
    src/ui/Commands.cpp
    src/ui/Commands.h
    src/ui/MainFrame.cpp
    src/ui/MainFrame.h
    src/ui/MainMenu.cpp
    src/ui/MainMenu.h
    src/ui/WorkflowBinding.cpp
    src/ui/WorkflowBinding.h
    src/ui/bridge/JobEvents.cpp
    src/ui/bridge/JobEvents.h
    src/ui/dialogs/AboutDialog.cpp
    src/ui/dialogs/AboutDialog.h
    src/ui/dialogs/GoToFileDialog.cpp
    src/ui/dialogs/GoToFileDialog.h
    src/ui/dialogs/ShortcutsDialog.cpp
    src/ui/dialogs/ShortcutsDialog.h
    src/ui/map/D2DRenderer.cpp
    src/ui/map/D2DRenderer.h
    src/ui/map/Legend.cpp
    src/ui/map/Legend.h
    src/ui/map/MapCanvas.cpp
    src/ui/map/MapCanvas.h
    src/ui/map/MapHover.cpp
    src/ui/map/MapHover.h
    src/ui/map/MapNavigator.cpp
    src/ui/map/MapNavigator.h
    src/ui/map/MapSelection.cpp
    src/ui/map/MapSelection.h
    src/ui/map/Minimap.cpp
    src/ui/map/Minimap.h
    src/ui/map/Ruler.cpp
    src/ui/map/Ruler.h
    src/ui/panels/DrivesPanel.cpp
    src/ui/panels/DrivesPanel.h
    src/ui/panels/BottomPanel.cpp
    src/ui/panels/BottomPanel.h
    src/ui/panels/MapPanel.cpp
    src/ui/panels/MapPanel.h
    src/ui/panels/PlanPage.cpp
    src/ui/panels/PlanPage.h
    src/ui/panels/StrategyPage.cpp
    src/ui/panels/StrategyPage.h
    src/ui/panels/SummaryPage.cpp
    src/ui/panels/SummaryPage.h
    res/IroncladDefrag.manifest
    res/IroncladDefrag.rc
)
target_link_libraries(IroncladDefrag PRIVATE icd_services wx::wx d2d1)
# The manifest file carries requestedExecutionLevel; stop the linker adding its own asInvoker entry.
target_link_options(IroncladDefrag PRIVATE /MANIFESTUAC:NO)
target_precompile_headers(IroncladDefrag PRIVATE src/precompiled.h)
icd_target_defaults(IroncladDefrag)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT IroncladDefrag)

# Copy the wx DLLs of the active configuration (debug or release set) next to the exe.
add_custom_command(TARGET IroncladDefrag POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:IroncladDefrag> $<TARGET_FILE_DIR:IroncladDefrag>
    COMMAND_EXPAND_LISTS
    COMMENT "Copying wxWidgets DLLs for $<CONFIG>")

# --- icd-cli: console front end ------------------------------------------------------------------
add_executable(icd-cli
    src/cli/commands/Analyze.cpp
    src/cli/commands/Analyze.h
    src/cli/commands/CheckCaps.cpp
    src/cli/commands/CheckCaps.h
    src/cli/commands/CheckCatchUp.cpp
    src/cli/commands/CheckCatchUp.h
    src/cli/commands/Churn.cpp
    src/cli/commands/Churn.h
    src/cli/commands/CliCommon.cpp
    src/cli/commands/CliCommon.h
    src/cli/commands/CompareScanners.cpp
    src/cli/commands/CompareScanners.h
    src/cli/main.cpp
)
target_link_libraries(icd-cli PRIVATE icd_services)
icd_target_defaults(icd-cli)

# --- synth-bench: synthetic snapshot build time and memory (plan.md 6.5) ------------------------
add_executable(synth-bench tools/bench/synth-bench.cpp)
target_link_libraries(synth-bench PRIVATE icd_services psapi)
icd_target_defaults(synth-bench)

# --- scan-bench: plan.md 6.5 scan and catch-up targets on a scale volume, the Phase 4 exit check (T067) -----------
add_executable(scan-bench tools/bench/scan-bench.cpp)
target_link_libraries(scan-bench PRIVATE icd_services)
icd_target_defaults(scan-bench)

# --- catchup-bench: the catch-up finish at 20 M files on a synthetic snapshot, patch against full finish (T069) ------
add_executable(catchup-bench tools/bench/catchup-bench.cpp)
target_link_libraries(catchup-bench PRIVATE icd_services)
icd_target_defaults(catchup-bench)

# --- map-bench: drive map performance targets (plan.md 7.5), the Phase 2 exit check (T033) ----------
# Compiles the map control's sources directly: the bench drives the real MapCanvas in a window.
add_executable(map-bench
    tools/bench/map-bench.cpp
    src/ui/Accelerators.cpp
    src/ui/Commands.cpp
    src/ui/map/D2DRenderer.cpp
    src/ui/map/MapCanvas.cpp
    src/ui/map/MapHover.cpp
    src/ui/map/MapNavigator.cpp
    src/ui/map/MapSelection.cpp
    src/ui/map/Minimap.cpp
)
target_link_libraries(map-bench PRIVATE icd_services wx::wx d2d1)
icd_target_defaults(map-bench)

# --- gen-scenario: seeded test volume scenarios (plan.md section 12, T041) ------------------------
add_executable(gen-scenario
    tools/testvol/gen-scenario/FileOps.cpp
    tools/testvol/gen-scenario/FileOps.h
    tools/testvol/gen-scenario/Scenarios.cpp
    tools/testvol/gen-scenario/Scenarios.h
    tools/testvol/gen-scenario/main.cpp
)
target_link_libraries(gen-scenario PRIVATE icd_platform advapi32)
icd_target_defaults(gen-scenario)

# --- make-scalevol: scale test volumes with allocated, never written files (plan.md section 12, T063a) ------------
add_executable(make-scalevol tools/testvol/make-scalevol/main.cpp)
target_link_libraries(make-scalevol PRIVATE icd_platform advapi32)
icd_target_defaults(make-scalevol)

# --- map-render-spike: map composition at 4K with wx Direct2D, raw Direct2D and GDI (T026) --------
add_executable(map-render-spike tools/spike/map-render-spike/main.cpp)
target_link_libraries(map-render-spike PRIVATE icd_services wx::wx d2d1)
icd_target_defaults(map-render-spike)

# --- Tests ---------------------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)

# --- Quality gate (quality.bat): Lizard everywhere, clang-tidy only with a compile database -------
# check-* targets look at changed files and includers of changed headers; check-*-all at all of src/.
foreach(scope IN ITEMS changed all)
    set(suffix "")
    if(scope STREQUAL "all")
        set(suffix "-all")
    endif()
    add_custom_target(check-complexity${suffix}
        COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckComplexity.cmake"
        COMMENT "Checking cyclomatic complexity with Lizard (${scope} files)"
        USES_TERMINAL VERBATIM)
    add_custom_target(check-quality${suffix})
    add_dependencies(check-quality${suffix} check-complexity${suffix})
    if(CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_GENERATOR MATCHES "Ninja")
        add_custom_target(check-clang-tidy${suffix}
            COMMAND "${CMAKE_COMMAND}" "-DQUALITY_SCOPE=${scope}" "-DQUALITY_BUILD_DIR=${PROJECT_BINARY_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/CheckClangTidy.cmake"
            COMMENT "Checking diagnostics and cognitive complexity with clang-tidy (${scope} files)"
            USES_TERMINAL VERBATIM)
        add_dependencies(check-quality${suffix} check-clang-tidy${suffix})
    endif()
endforeach()
