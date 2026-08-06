if(NOT DEFINED NR_ASSERT_FAILURE_PROBE)
    message(FATAL_ERROR "NR_ASSERT_FAILURE_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NrFailureProbeContract.cmake")

nr_require_failure_probe(
    PROBE "${NR_ASSERT_FAILURE_PROBE}"
    CONTEXT "Lazy assertion failure"
    REQUIRED_OUTPUT
        "[NR ASSERT]"
        "lazy assertion failure probe invocation=1"
        "nr_utils_error_handle_lazy_failure_probe.cpp"
)
