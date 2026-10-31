# Common compiler settings for every IroncladDefrag target. The main compiler is clang-cl; MSVC also works.

# Applies the project's warning level, language mode and Windows defines to one target.
function(icd_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /external:W0)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # Standard conformance switches that clang already follows by default.
        target_compile_options(${target} PRIVATE /permissive- /Zc:__cplusplus /Zc:preprocessor)
    endif()
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endfunction()
