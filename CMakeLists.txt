cmake_minimum_required(VERSION 4.0)
project(IroncladDefrag)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Windows subsystem
set(CMAKE_WIN32_EXECUTABLE ON)
add_compile_options(/MP)

# Restore NuGet packages
find_program(NUGET_EXE nuget)
if(NOT NUGET_EXE)
    message(WARNING "NuGet not found. Attempting to use system paths.")
endif()

# Set NuGet packages path
set(NUGET_PACKAGES_DIR "${CMAKE_SOURCE_DIR}/packages")

# Try to find CppWinRT in NuGet packages
if(EXISTS "${NUGET_PACKAGES_DIR}")
    file(GLOB CPPWINRT_PACKAGE "${NUGET_PACKAGES_DIR}/Microsoft.Windows.CppWinRT.*")
    file(GLOB WINDOWSAPPSDK_PACKAGE "${NUGET_PACKAGES_DIR}/Microsoft.WindowsAppSDK.*")

    if(CPPWINRT_PACKAGE)
        list(GET CPPWINRT_PACKAGE 0 CPPWINRT_DIR)
        message(STATUS "Found CppWinRT: ${CPPWINRT_DIR}")
    endif()

    if(WINDOWSAPPSDK_PACKAGE)
        list(GET WINDOWSAPPSDK_PACKAGE 0 WINDOWSAPPSDK_DIR)
        message(STATUS "Found WindowsAppSDK: ${WINDOWSAPPSDK_DIR}")
    endif()
endif()

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
        model/Units.h)

# Manually specify Windows SDK and WinRT include paths
target_include_directories(IroncladDefrag PRIVATE
#        "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt"
#        "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}winrt"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Include/$ENV{WindowsSDKVersion}"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Include/$ENV{WindowsSDKVersion}cppwinrt"
)

# Link WinRT and WinUI libraries
target_link_libraries(IroncladDefrag PRIVATE
        WindowsApp )

# Enable C++/WinRT
target_compile_options(IroncladDefrag PRIVATE
        /await:strict
        /EHsc )
# Set Unicode build
target_compile_definitions(IroncladDefrag PRIVATE UNICODE _UNICODE)
