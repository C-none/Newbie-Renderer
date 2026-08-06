include_guard(GLOBAL)

function(nr_require_failure_probe)
    set(options)
    set(one_value_arguments PROBE CONTEXT)
    set(multi_value_arguments ARGUMENTS REQUIRED_OUTPUT)
    cmake_parse_arguments(PARSE_ARGV 0 NR_FAILURE "${options}" "${one_value_arguments}" "${multi_value_arguments}")

    set(fallback_context "failure probe contract")
    if(DEFINED NR_FAILURE_CONTEXT AND NOT NR_FAILURE_CONTEXT STREQUAL "")
        set(fallback_context "${NR_FAILURE_CONTEXT}")
    endif()

    if(NR_FAILURE_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "${fallback_context}: unrecognized arguments: '${NR_FAILURE_UNPARSED_ARGUMENTS}'.")
    endif()
    if(NR_FAILURE_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "${fallback_context}: arguments are missing values: '${NR_FAILURE_KEYWORDS_MISSING_VALUES}'.")
    endif()
    if(NOT DEFINED NR_FAILURE_CONTEXT OR NR_FAILURE_CONTEXT STREQUAL "")
        message(FATAL_ERROR "Failure probe CONTEXT is required.")
    endif()
    if(NOT DEFINED NR_FAILURE_PROBE OR NR_FAILURE_PROBE STREQUAL "")
        message(FATAL_ERROR "${NR_FAILURE_CONTEXT}: failure probe path is required.")
    endif()
    if(NOT EXISTS "${NR_FAILURE_PROBE}")
        message(FATAL_ERROR "${NR_FAILURE_CONTEXT}: failure probe does not exist: '${NR_FAILURE_PROBE}'.")
    endif()
    if(IS_DIRECTORY "${NR_FAILURE_PROBE}")
        message(FATAL_ERROR "${NR_FAILURE_CONTEXT}: failure probe path is a directory: '${NR_FAILURE_PROBE}'.")
    endif()

    list(LENGTH NR_FAILURE_REQUIRED_OUTPUT required_output_count)
    if(required_output_count EQUAL 0)
        message(FATAL_ERROR "${NR_FAILURE_CONTEXT}: REQUIRED_OUTPUT must contain at least one literal fragment.")
    endif()

    file(REAL_PATH "${NR_FAILURE_PROBE}" canonical_probe)
    execute_process(
        COMMAND "${canonical_probe}" ${NR_FAILURE_ARGUMENTS}
        RESULT_VARIABLE probe_result
        OUTPUT_VARIABLE probe_stdout
        ERROR_VARIABLE probe_stderr
    )
    set(probe_output "${probe_stdout}\n${probe_stderr}")

    if(NOT "${probe_result}" MATCHES "^-?[0-9]+$")
        message(FATAL_ERROR
            "${NR_FAILURE_CONTEXT}: failure probe could not be spawned; result='${probe_result}'.\n"
            "stdout:\n${probe_stdout}\n"
            "stderr:\n${probe_stderr}")
    endif()
    if(NOT "${probe_result}" STREQUAL "1")
        message(FATAL_ERROR
            "${NR_FAILURE_CONTEXT}: failure probe exited with '${probe_result}' instead of 1.\n"
            "stdout:\n${probe_stdout}\n"
            "stderr:\n${probe_stderr}")
    endif()

    string(FIND "${probe_output}" "[NR VULKAN:ERROR]" vulkan_error_index)
    if(NOT vulkan_error_index EQUAL -1)
        message(FATAL_ERROR
            "${NR_FAILURE_CONTEXT}: failure probe emitted an unexpected Vulkan error. Output:\n${probe_output}")
    endif()

    foreach(expected_fragment IN LISTS NR_FAILURE_REQUIRED_OUTPUT)
        if(expected_fragment STREQUAL "")
            message(FATAL_ERROR "${NR_FAILURE_CONTEXT}: REQUIRED_OUTPUT contains an empty fragment.")
        endif()
        string(FIND "${probe_output}" "${expected_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${NR_FAILURE_CONTEXT}: failure probe output did not contain '${expected_fragment}'. "
                "Output:\n${probe_output}")
        endif()
    endforeach()
endfunction()
