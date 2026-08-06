if(NOT DEFINED NR_TEST_EMPTY_REGISTRY_PROBE)
    message(FATAL_ERROR "NR_TEST_EMPTY_REGISTRY_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/NrFailureProbeContract.cmake")

nr_require_failure_probe(
    PROBE "${NR_TEST_EMPTY_REGISTRY_PROBE}"
    CONTEXT "Empty nr.test registry"
    REQUIRED_OUTPUT "[nr_test] no tests registered"
)
