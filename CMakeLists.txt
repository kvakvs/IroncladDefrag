cmake_minimum_required(VERSION 4.0)
project(IroncladDefrag)

set(CMAKE_CXX_STANDARD 23)

# Enable parallel compilation
if(MSVC)
    add_compile_options(/MP)
endif()

add_executable(IroncladDefrag WIN32 main.cpp
        model/FileMetadata.cpp
        model/FileMetadata.h
        model/FragmentMap.cpp
        model/FragmentMap.h
        model/FreeSpaceMap.cpp
        model/FreeSpaceMap.h
        model/DiskGeometry.cpp
        model/DiskGeometry.h
        model/Units.h)

# Set Unicode build
target_compile_definitions(IroncladDefrag PRIVATE UNICODE _UNICODE)
