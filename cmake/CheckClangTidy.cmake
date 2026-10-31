# clang-tidy gate over the translation units in src/ (cmake -P, see QualityCommon.cmake).
# Needs QUALITY_BUILD_DIR/compile_commands.json from a Ninja + clang-cl configure (quality.bat makes one) and the
# vcvars64 environment (INCLUDE), which clang-tidy's cl driver mode reads for the MSVC STL and SDK headers.
include("${CMAKE_CURRENT_LIST_DIR}/QualityCommon.cmake")
set(database "${QUALITY_BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${database}")
    message(FATAL_ERROR "Missing ${database}. Run quality.bat, which configures ${QUALITY_BUILD_DIR}.")
endif()
if(NOT DEFINED ENV{INCLUDE})
    message(FATAL_ERROR "INCLUDE is not set; run from a vcvars64 environment (quality.bat does this).")
endif()

set(scoped)
if(QUALITY_SCOPE STREQUAL "changed")
    icd_quality_scope(tidy scoped)
    if(NOT scoped)
        message(STATUS "No changed translation units or includers of changed headers; clang-tidy skipped.")
        return()
    endif()
endif()

# Keep one database entry per selected project source, verbatim (flags and working directory).
file(READ "${database}" commands)
string(JSON count LENGTH "${commands}")
math(EXPR last "${count} - 1")
set(selected "[")
set(separator "")
set(units 0)
set(seen)
foreach(index RANGE ${last})
    string(JSON source GET "${commands}" ${index} file)
    string(JSON directory GET "${commands}" ${index} directory)
    get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${directory}")
    file(RELATIVE_PATH relative "${project_root}" "${source}")
    if(relative MATCHES "^src/" AND NOT relative IN_LIST seen
        AND (QUALITY_SCOPE STREQUAL "all" OR relative IN_LIST scoped))
        list(APPEND seen "${relative}")
        string(JSON entry GET "${commands}" ${index})
        string(APPEND selected "${separator}${entry}")
        set(separator ",")
        math(EXPR units "${units} + 1")
    endif()
endforeach()
if(units EQUAL 0)
    message(STATUS "No selected translation units are in the compile database; clang-tidy skipped.")
    return()
endif()
set(selected_dir "${QUALITY_BUILD_DIR}/quality/tidy-db")
file(MAKE_DIRECTORY "${selected_dir}")
file(WRITE "${selected_dir}/compile_commands.json" "${selected}]")

get_filename_component(venv_scripts "${QUALITY_PYTHON}" DIRECTORY)
find_program(CLANG_TIDY NAMES clang-tidy HINTS "${venv_scripts}" NO_DEFAULT_PATH REQUIRED)
find_program(RUN_CLANG_TIDY NAMES run-clang-tidy HINTS "${venv_scripts}" NO_DEFAULT_PATH REQUIRED)
if(NOT DEFINED QUALITY_JOBS)
    # Half of the logical cores, so the machine stays responsive.
    cmake_host_system_information(RESULT cpus QUERY NUMBER_OF_LOGICAL_CORES)
    math(EXPR QUALITY_JOBS "${cpus} / 2")
    if(QUALITY_JOBS LESS 1)
        set(QUALITY_JOBS 1)
    endif()
endif()

message(STATUS "clang-tidy: ${units} translation units, scope ${QUALITY_SCOPE}, ${QUALITY_JOBS} jobs")
execute_process(
    COMMAND "${RUN_CLANG_TIDY}" "-p=${selected_dir}" "-j=${QUALITY_JOBS}" -quiet
        "-clang-tidy-binary=${CLANG_TIDY}" "-config-file=${project_root}/.clang-tidy"
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE tidy_result)
if(NOT tidy_result STREQUAL "0")
    message(FATAL_ERROR "clang-tidy failed. Fix the diagnostics above; do not raise thresholds or suppress.")
endif()
message(STATUS "clang-tidy passed.")
