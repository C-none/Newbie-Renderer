if(NOT DEFINED NR_RENDER_GRAPH_EXECUTOR_FAILURE_PROBE)
    message(FATAL_ERROR "NR_RENDER_GRAPH_EXECUTOR_FAILURE_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NrFailureProbeContract.cmake")

nr_require_failure_probe(
    PROBE "${NR_RENDER_GRAPH_EXECUTOR_FAILURE_PROBE}"
    ARGUMENTS "record-undeclared-resource"
    CONTEXT "RenderGraph record capability"
    REQUIRED_OUTPUT "[NR ASSERT]" "record resolver rejected undeclared resource handle"
)
