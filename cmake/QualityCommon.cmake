# Shared settings for the quality check scripts (run with cmake -P).
# Inputs: QUALITY_SCOPE (changed|all, default changed), QUALITY_BUILD_DIR, optional QUALITY_PYTHON, QUALITY_BASE.
get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED QUALITY_SCOPE)
    set(QUALITY_SCOPE changed)
endif()
if(NOT QUALITY_SCOPE MATCHES "^(all|changed)$")
    message(FATAL_ERROR "QUALITY_SCOPE must be all or changed, not '${QUALITY_SCOPE}'.")
endif()
if(NOT DEFINED QUALITY_BUILD_DIR)
    set(QUALITY_BUILD_DIR "${project_root}/build/quality")
endif()
if(NOT DEFINED QUALITY_PYTHON)
    set(QUALITY_PYTHON "${project_root}/.venv-quality/Scripts/python.exe")
endif()
if(NOT EXISTS "${QUALITY_PYTHON}")
    message(FATAL_ERROR "Missing ${QUALITY_PYTHON}. Run quality.bat, which creates .venv-quality from "
        "tools/requirements-quality.txt.")
endif()
if(NOT DEFINED QUALITY_BASE)
    set(QUALITY_BASE HEAD)
endif()

# Stores the changed files that the check kind (tidy or lizard) must look at in output.
function(icd_quality_scope kind output)
    set(list_file "${QUALITY_BUILD_DIR}/quality/${kind}-scope.txt")
    file(MAKE_DIRECTORY "${QUALITY_BUILD_DIR}/quality")
    execute_process(
        COMMAND "${QUALITY_PYTHON}" "${project_root}/cmake/quality_scope.py" --kind ${kind}
            --root "${project_root}" --base "${QUALITY_BASE}" --output "${list_file}"
        RESULT_VARIABLE scope_result)
    if(NOT scope_result STREQUAL "0")
        message(FATAL_ERROR "Selecting changed files failed; run the check with scope 'all' instead.")
    endif()
    file(STRINGS "${list_file}" files)
    set(${output} "${files}" PARENT_SCOPE)
endfunction()
