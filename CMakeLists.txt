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
        src/model/FileMetadata.cpp
        src/model/FileMetadata.h
        src/model/FragmentMap.cpp
        src/model/FragmentMap.h
        src/model/FreeSpaceMap.cpp
        src/model/FreeSpaceMap.h
        src/model/DiskGeometry.cpp
        src/model/DiskGeometry.h
        src/model/Units.h
        src/ui/App.cpp
        src/ui/App.h)
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