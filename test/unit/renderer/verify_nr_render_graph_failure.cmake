if(NOT DEFINED NR_RENDER_GRAPH_FAILURE_PROBE)
    message(FATAL_ERROR "NR_RENDER_GRAPH_FAILURE_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NrFailureProbeContract.cmake")

foreach(probe_case IN ITEMS
        conflicting-uses
        copy-self
        compiler-same-pass)
    if(probe_case STREQUAL "conflicting-uses")
        set(expected_text "resource handle has conflicting use declarations")
    elseif(probe_case STREQUAL "copy-self")
        set(expected_text "requires distinct source and destination resources")
    else()
        set(expected_text "conflicting same-resource uses after builder canonicalization")
    endif()

    nr_require_failure_probe(
        PROBE "${NR_RENDER_GRAPH_FAILURE_PROBE}"
        ARGUMENTS "${probe_case}"
        CONTEXT "RenderGraph '${probe_case}'"
        REQUIRED_OUTPUT "[NR ASSERT]" "${expected_text}"
    )
endforeach()
