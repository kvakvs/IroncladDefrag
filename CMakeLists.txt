cmake_minimum_required(VERSION 4.0)
project(IroncladDefrag)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Windows subsystem
set(CMAKE_WIN32_EXECUTABLE ON)
add_compile_options(/MP)

# Path to unpacked WinUI 3 NuGet
set(WINDOWSAPPSDK_VER "2.0.250930001-experimental1")
set(WINDOWSAPPSDK_DIR "$ENV{USERPROFILE}/.nuget/packages/microsoft.windowsappsdk/${WINDOWSAPPSDK_VER}")
include_directories(${WINDOWSAPPSDK_DIR}/include)
link_directories(${WINDOWSAPPSDK_DIR}/lib/x64)

set(WINUI_SDK_DIR "${CMAKE_SOURCE_DIR}/external/Microsoft.WindowsAppSDK.${WINDOWSAPPSDK_VER}/build/native")

# --- Detect Windows SDK manually ---
set(WIN_SDK_DIR "C:/Program Files (x86)/Windows Kits/10")

# Include directories (shared, ucrt, um)
set(WIN_SDK_INCLUDE_DIRS
        "${WIN_SDK_DIR}/Include/10.0.19041.0/um"
        "${WIN_SDK_DIR}/Include/10.0.19041.0/ucrt"
        "${WIN_SDK_DIR}/Include/10.0.19041.0/shared"
        "${WIN_SDK_DIR}/Include/10.0.19041.0/winrt"
)

# Library directories
set(WIN_SDK_LIB_DIRS
        "${WIN_SDK_DIR}/Lib/10.0.19041.0/um/x64"
        "${WIN_SDK_DIR}/Lib/10.0.19041.0/ucrt/x64"
)

# For generated/ directory run powershell ./generate-winrt.ps1
include_directories(${WIN_SDK_INCLUDE_DIRS})
link_directories(${WIN_SDK_LIB_DIRS})

add_executable(IroncladDefrag WIN32
        main.cpp
        model/FileMetadata.cpp
        model/FileMetadata.h
        model/FragmentMap.cpp
        model/FragmentMap.h
        model/FreeSpaceMap.cpp
        model/FreeSpaceMap.h
        model/DiskGeometry.cpp
        model/DiskGeometry.h
        model/Units.h
        precompiled.h
        precompiled.cpp
        ui/MainWindow.h
        ui/App.h
        ui/App.cpp
        ui/MainWindow.cpp)

# Enable precompiled headers
target_precompile_headers(IroncladDefrag PRIVATE precompiled.h)

# Include directories
include_directories(
        ${CMAKE_SOURCE_DIR}/generated
        ${WINUI_SDK_DIR}/include
        ${WindowsSDK_INCLUDE_DIRS}
)
# Library directories
link_directories(
        ${WINUI_SDK_DIR}/lib/x64
)

# Set library directories
target_link_directories(IroncladDefrag PRIVATE
        "${APPSDK_LIB_DIR}"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Lib/${WINDOWS_SDK_VERSION}um/${APPSDK_ARCH}"
)

# Link required libraries
target_link_libraries(IroncladDefrag PRIVATE
        WindowsApp.lib
        Microsoft.UI.Xaml.lib
        OneCoreUAP.lib
        runtimeobject.lib
        #------
        user32.lib
        gdi32.lib
        comdlg32.lib
        d2d1.lib
        dwrite.lib
        dxgi.lib
)
# Ensure manifest embedding for Windows App SDK runtime
add_custom_command(TARGET IroncladDefrag POST_BUILD
        COMMAND mt.exe -manifest "${WINUI_SDK_DIR}/bin/x64/Microsoft.WindowsAppRuntime.Manifest" -outputresource:$<TARGET_FILE:IroncladDefrag>;#1
)

#
## Enable C++/WinRT
#target_compile_options(IroncladDefrag PRIVATE
#        /await:strict
#        /EHsc
#        /Zc:__cplusplus
#)
## Set necessary definitions
#target_compile_definitions(IroncladDefrag PRIVATE
#        UNICODE
#        _UNICODE
#        WINRT_LEAN_AND_MEAN
#        WIN32_LEAN_AND_MEAN
#)