# Common compiler settings for every IroncladDefrag target.

# Applies the project's warning level, language mode and Windows defines to one target.
function(icd_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    target_compile_options(${target} PRIVATE
        /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /external:W0)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endfunction()
