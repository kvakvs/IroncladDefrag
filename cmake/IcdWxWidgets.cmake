# Imported targets for the prebuilt wxWidgets MSVC x64 DLL build in 3rdparty/wxWidgets.
#
# Defines wx::base, wx::core (SHARED IMPORTED, Debug and Release) and wx::wx (everything the GUI uses).
# Libraries are linked explicitly instead of through wx's #pragma auto-linking, so CMake knows the
# runtime DLLs of each configuration and $<TARGET_RUNTIME_DLLS:...> copies exactly those.

set(ICD_WX_ROOT "${CMAKE_SOURCE_DIR}/3rdparty/wxWidgets")
set(_icd_wx_stamp "${ICD_WX_ROOT}/icd-wx-stamp.txt")

if(NOT EXISTS "${_icd_wx_stamp}")
    message(FATAL_ERROR
        "wxWidgets not found in ${ICD_WX_ROOT}.\n"
        "Run: powershell -ExecutionPolicy Bypass -File tools/fetch-wx.ps1 (build.bat does this for you).")
endif()

file(STRINGS "${_icd_wx_stamp}" _icd_wx_stamp_version REGEX "^version=" LIMIT_COUNT 1)
if(NOT _icd_wx_stamp_version STREQUAL "version=${ICD_WX_VERSION}")
    message(FATAL_ERROR
        "3rdparty/wxWidgets has '${_icd_wx_stamp_version}', but CMake expects ${ICD_WX_VERSION}.\n"
        "Run tools/fetch-wx.ps1 again, or update ICD_WX_VERSION and the pins in that script together.")
endif()

# Name parts: libraries use "33" (major+minor), DLLs use "334" (full version without dots).
string(REPLACE "." ";" _icd_wx_parts "${ICD_WX_VERSION}")
list(GET _icd_wx_parts 0 _icd_wx_major)
list(GET _icd_wx_parts 1 _icd_wx_minor)
list(GET _icd_wx_parts 2 _icd_wx_release)
set(_icd_wx_lib_ver "${_icd_wx_major}${_icd_wx_minor}")
set(_icd_wx_dll_ver "${_icd_wx_major}${_icd_wx_minor}${_icd_wx_release}")
set(_icd_wx_libdir "${ICD_WX_ROOT}/lib/vc14x_x64_dll")

# Creates wx::<name> for one wx library. kind is "base" or "msw"; name is "base", "core", "adv", ...
function(_icd_wx_import name kind)
    if(name STREQUAL "base")
        set(_lib "wxbase${_icd_wx_lib_ver}u")
        set(_libd "wxbase${_icd_wx_lib_ver}ud")
        set(_dll "wxbase${_icd_wx_dll_ver}u_vc14x_x64")
        set(_dlld "wxbase${_icd_wx_dll_ver}ud_vc14x_x64")
    else()
        set(_lib "wx${kind}${_icd_wx_lib_ver}u_${name}")
        set(_libd "wx${kind}${_icd_wx_lib_ver}ud_${name}")
        set(_dll "wx${kind}${_icd_wx_dll_ver}u_${name}_vc14x_x64")
        set(_dlld "wx${kind}${_icd_wx_dll_ver}ud_${name}_vc14x_x64")
    endif()
    foreach(_file "${_lib}.lib" "${_libd}.lib" "${_dll}.dll" "${_dlld}.dll")
        if(NOT EXISTS "${_icd_wx_libdir}/${_file}")
            message(FATAL_ERROR "wxWidgets file missing: ${_icd_wx_libdir}/${_file}")
        endif()
    endforeach()
    add_library(wx::${name} SHARED IMPORTED GLOBAL)
    set_target_properties(wx::${name} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_IMPLIB_DEBUG "${_icd_wx_libdir}/${_libd}.lib"
        IMPORTED_LOCATION_DEBUG "${_icd_wx_libdir}/${_dlld}.dll"
        IMPORTED_IMPLIB_RELEASE "${_icd_wx_libdir}/${_lib}.lib"
        IMPORTED_LOCATION_RELEASE "${_icd_wx_libdir}/${_dll}.dll"
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
        MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
endfunction()

_icd_wx_import(base base)
_icd_wx_import(core msw)

# Everything a wx GUI target needs: headers, the per-config setup.h, DLL import macros, libraries.
# Only include/ (not include/msvc) is on the path, so wx's #pragma comment(lib) auto-linking is off.
add_library(wx::wx INTERFACE IMPORTED GLOBAL)
target_include_directories(wx::wx SYSTEM INTERFACE
    "${ICD_WX_ROOT}/include"
    "$<IF:$<CONFIG:Debug>,${_icd_wx_libdir}/mswud,${_icd_wx_libdir}/mswu>")
target_compile_definitions(wx::wx INTERFACE
    WXUSINGDLL
    __WXMSW__)
target_link_libraries(wx::wx INTERFACE
    wx::core wx::base
    comctl32 rpcrt4 winmm advapi32 shell32 ole32 oleaut32 uuid uxtheme version shlwapi oleacc ws2_32)
