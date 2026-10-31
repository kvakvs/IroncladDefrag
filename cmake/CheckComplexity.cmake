# Cyclomatic complexity gate: Lizard over the C++ sources in src/ (cmake -P, see QualityCommon.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/QualityCommon.cmake")
if(NOT DEFINED COMPLEXITY_MAX_CCN)
    set(COMPLEXITY_MAX_CCN 10)
endif()
if(NOT COMPLEXITY_MAX_CCN MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "COMPLEXITY_MAX_CCN must be a positive integer.")
endif()

set(inputs src)
if(QUALITY_SCOPE STREQUAL "changed")
    icd_quality_scope(lizard inputs)
    if(NOT inputs)
        message(STATUS "No changed C++ files in src/; Lizard skipped.")
        return()
    endif()
endif()
# -w prints only the functions over a threshold; Lizard exits non-zero when there are any.
execute_process(
    COMMAND "${QUALITY_PYTHON}" -m lizard -l cpp -w -C "${COMPLEXITY_MAX_CCN}" ${inputs}
    WORKING_DIRECTORY "${project_root}"
    RESULT_VARIABLE lizard_result)
if(NOT lizard_result STREQUAL "0")
    message(FATAL_ERROR "Complexity check failed (Lizard exit ${lizard_result}). Simplify the functions above.")
endif()
message(STATUS "Lizard passed (CCN <= ${COMPLEXITY_MAX_CCN}, scope ${QUALITY_SCOPE}).")
