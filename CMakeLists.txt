cmake_minimum_required(VERSION 4.0)
project(IroncladDefrag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Windows subsystem
set(CMAKE_WIN32_EXECUTABLE ON)
add_compile_options(/MP)

include_directories(
        ${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets/include
        ${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets/include/msvc
        ${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets/lib/vc14x_x64_dll
)
link_directories(
        ${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets/lib/vc14x_x64_dll
)

add_executable(IroncladDefrag WIN32
        src/precompiled.h
        src/precompiled.cpp
        src/main.cpp
        src/analysis/DriveAnalysisService.cpp
        src/analysis/DriveAnalysisService.h
        src/analysis/FakeAnalysisService.cpp
        src/analysis/FakeAnalysisService.h
        src/app/ApplicationController.cpp
        src/app/ApplicationController.h
        src/app/BackgroundJob.cpp
        src/app/BackgroundJob.h
        src/classification/FileClassifier.cpp
        src/classification/FileClassifier.h
        src/execution/MoveExecutor.cpp
        src/execution/MoveExecutor.h
        src/model/DomainTypes.h
        src/model/FileMetadata.cpp
        src/model/FileMetadata.h
        src/model/FragmentMap.cpp
        src/model/FragmentMap.h
        src/model/FreeSpaceMap.cpp
        src/model/FreeSpaceMap.h
        src/model/DiskGeometry.cpp
        src/model/DiskGeometry.h
        src/model/Units.h
        src/optimization/OptimizationSettingsSerializer.cpp
        src/optimization/OptimizationSettingsSerializer.h
        src/optimization/MovePlanner.cpp
        src/optimization/MovePlanner.h
        src/optimization/PlacementPlanner.cpp
        src/optimization/PlacementPlanner.h
        src/optimization/ProfileCatalog.cpp
        src/optimization/ProfileCatalog.h
        src/platform/windows/DriveEnumerator.cpp
        src/platform/windows/DriveEnumerator.h
        src/platform/windows/UniqueHandle.cpp
        src/platform/windows/UniqueHandle.h
        src/platform/windows/VolumeMoveOperations.cpp
        src/platform/windows/VolumeMoveOperations.h
        src/platform/windows/VolumeQueries.cpp
        src/platform/windows/VolumeQueries.h
        src/support/Logger.cpp
        src/support/Logger.h
        src/ui/App.cpp
        src/ui/App.h
        src/ui/DriveMapPanel.cpp
        src/ui/DriveMapPanel.h
        src/ui/DriveAnalysisPage.cpp
        src/ui/DriveAnalysisPage.h
        src/ui/MovePlanDialog.cpp
        src/ui/MovePlanDialog.h
        src/ui/ProfileSettingsDialog.cpp
        src/ui/ProfileSettingsDialog.h
        src/ui/MainFrame.cpp
        src/ui/MainFrame.h
)
target_precompile_headers(IroncladDefrag PRIVATE src/precompiled.h)

# Link wxWidgets libraries
target_link_libraries(IroncladDefrag PRIVATE
        # Debug libraries (used in Debug configuration)
        debug wxbase33ud.lib
        debug wxmsw33ud_core.lib
        debug wxmsw33ud_adv.lib
        debug wxpngd.lib
        debug wxzlibd.lib
        debug wxtiffd.lib
        debug wxjpegd.lib
        debug wxexpatd.lib

        # Release libraries (used in Release configuration)
        optimized wxbase33u.lib
        optimized wxmsw33u_core.lib
        optimized wxmsw33u_adv.lib
        optimized wxpng.lib
        optimized wxzlib.lib
        optimized wxtiff.lib
        optimized wxjpeg.lib
        optimized wxexpat.lib

        # Windows system libraries
        comctl32
        rpcrt4
        winmm
        advapi32
        shell32
        wsock32
)
#target_link_libraries(IroncladDefrag PRIVATE
#        $<$<CONFIG:Debug>:wxbase33ud.lib>
#        $<$<CONFIG:Debug>:wxmsw33ud_core.lib>
#        $<$<CONFIG:Release>:wxbase33u.lib>
#        $<$<CONFIG:Release>:wxmsw33u_core.lib>
#)

# Enable C++/WinRT
target_compile_options(IroncladDefrag PRIVATE
        /await:strict
        /EHsc
        /Zc:__cplusplus
)
# Set necessary definitions
target_compile_definitions(IroncladDefrag PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        #--- wxWidgets settings ---
        WXUSINGDLL
        wxMSVC_VERSION_ABI_COMPAT
)

# Copy wxWidgets DLLs to build directory
# CMAKE_COMMAND -E copy_if_different only copies if source is newer than destination
set(WXWIDGETS_DLL_DIR "${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets/lib/vc14x_x64_dll")

# Debug DLLs
set(WXWIDGETS_DEBUG_DLLS
        wxbase331ud_vc14x_x64.dll
        wxmsw331ud_core_vc14x_x64.dll
        wxmsw331ud_adv_vc14x_x64.dll
)

# Release DLLs
set(WXWIDGETS_RELEASE_DLLS
        wxbase331u_vc14x_x64.dll
        wxmsw331u_core_vc14x_x64.dll
        wxmsw331u_adv_vc14x_x64.dll
)

# Copy DLLs based on configuration
foreach(dll ${WXWIDGETS_DEBUG_DLLS})
    add_custom_command(TARGET IroncladDefrag POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WXWIDGETS_DLL_DIR}/${dll}"
            "$<TARGET_FILE_DIR:IroncladDefrag>"
            COMMENT "Copying ${dll} to build directory (if needed)"
            VERBATIM
    )
endforeach()

# Note: For multi-config generators (Visual Studio, Xcode), you can use generator expressions
# to copy different DLLs based on build configuration:
# add_custom_command(TARGET IroncladDefrag POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E $<IF:$<CONFIG:Debug>,echo "Debug",echo "Release">
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         "$<$<CONFIG:Debug>:${WXWIDGETS_DLL_DIR}/wxbase331ud_vc14x_x64.dll>$<$<CONFIG:Release>:${WXWIDGETS_DLL_DIR}/wxbase331u_vc14x_x64.dll>"
#         "$<TARGET_FILE_DIR:IroncladDefrag>"
# )
