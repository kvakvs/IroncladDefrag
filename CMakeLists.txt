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

# --- icd_core: wx-free, Win32-free model and algorithms -------------------------------------------
add_library(icd_core STATIC
    src/core/Version.h.in
    src/core/exec/Executor.cpp
    src/core/exec/Executor.h
    src/core/format/ByteSize.cpp
    src/core/format/ByteSize.h
    src/core/format/Count.cpp
    src/core/format/Count.h
    src/core/format/DriveText.cpp
    src/core/format/DriveText.h
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
    src/core/model/DriveInfo.cpp
    src/core/model/DriveInfo.h
    src/core/resources/ResourceCaps.cpp
    src/core/resources/ResourceCaps.h
)
target_include_directories(icd_core PUBLIC src "${CMAKE_BINARY_DIR}/generated")
icd_target_defaults(icd_core)

# --- icd_platform: narrow Win32 wrappers ---------------------------------------------------------
add_library(icd_platform STATIC
    src/platform/win/LogSinks.cpp
    src/platform/win/LogSinks.h
    src/platform/win/SystemResources.cpp
    src/platform/win/SystemResources.h
    src/platform/win/WinError.cpp
    src/platform/win/WinError.h
)
target_link_libraries(icd_platform PUBLIC icd_core shell32 ole32)
icd_target_defaults(icd_platform)

# --- icd_services: jobs, thread pool, orchestration ----------------------------------------------
add_library(icd_services STATIC
    src/services/AppInfo.cpp
    src/services/AppInfo.h
    src/services/AppLogging.cpp
    src/services/AppLogging.h
    src/services/AppServices.cpp
    src/services/AppServices.h
    src/services/drives/FakeDriveProvider.cpp
    src/services/drives/FakeDriveProvider.h
    src/services/drives/IDriveProvider.h
    src/services/exec/ThreadPool.cpp
    src/services/exec/ThreadPool.h
    src/services/job/FakeJob.cpp
    src/services/job/FakeJob.h
    src/services/job/JobRunner.cpp
    src/services/job/JobRunner.h
    src/services/workflow/FakeResults.cpp
    src/services/workflow/FakeResults.h
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
    src/ui/panels/DrivesPanel.cpp
    src/ui/panels/DrivesPanel.h
    src/ui/panels/BottomPanel.cpp
    src/ui/panels/BottomPanel.h
    src/ui/panels/MapPlaceholder.cpp
    src/ui/panels/MapPlaceholder.h
    src/ui/panels/PlanPage.cpp
    src/ui/panels/PlanPage.h
    src/ui/panels/StrategyPage.cpp
    src/ui/panels/StrategyPage.h
    src/ui/panels/SummaryPage.cpp
    src/ui/panels/SummaryPage.h
    res/IroncladDefrag.manifest
    res/IroncladDefrag.rc
)
target_link_libraries(IroncladDefrag PRIVATE icd_services wx::wx)
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
    src/cli/main.cpp
)
target_link_libraries(icd-cli PRIVATE icd_services)
icd_target_defaults(icd-cli)

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
