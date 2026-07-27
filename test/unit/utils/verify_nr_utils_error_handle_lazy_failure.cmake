if(NOT DEFINED NR_ASSERT_FAILURE_PROBE)
    message(FATAL_ERROR "NR_ASSERT_FAILURE_PROBE is required.")
endif()

execute_process(
    COMMAND "${NR_ASSERT_FAILURE_PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr
)

if(NOT probe_result EQUAL 1)
    message(FATAL_ERROR "Lazy assertion probe exited with '${probe_result}' instead of 1.")
endif()

set(probe_output "${probe_stdout}${probe_stderr}")

foreach(expected_text
        "[NR ASSERT]"
        "lazy assertion failure probe invocation=1"
        "nr_utils_error_handle_lazy_failure_probe.cpp")
    string(FIND "${probe_output}" "${expected_text}" match_index)
    if(match_index EQUAL -1)
        message(FATAL_ERROR "Lazy assertion probe output did not contain '${expected_text}'. Output:\n${probe_output}")
    endif()
endforeach()
