if(NOT DEFINED NR_RHI_AS_GEOMETRY_FAILURE_PROBE)
    message(FATAL_ERROR "NR_RHI_AS_GEOMETRY_FAILURE_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NrFailureProbeContract.cmake")

foreach(probe_case IN ITEMS
        primitive-offset-after-indexed
        primitive-offset-before-indexed
        primitive-offset-plus-first-vertex
        nonindexed-max-vertex
        indexed-first-vertex
        vertex-offset-multiply-overflow
        vertex-offset-add-overflow
        vertex-size-overflow
        indexed-vertex-range
        indexed-index-range)
    if(probe_case MATCHES "^primitive-offset")
        set(expected_text "Triangle BLAS vertex data range exceeds its declared buffer")
    elseif(probe_case STREQUAL "nonindexed-max-vertex")
        set(expected_text "Non-indexed triangle BLAS geometry requires maxVertex to cover every consumed vertex")
    elseif(probe_case STREQUAL "indexed-first-vertex")
        set(expected_text "Indexed triangle BLAS geometry requires firstVertex <= maxVertex")
    elseif(probe_case MATCHES "^vertex-offset")
        set(expected_text "Triangle BLAS vertex range offset overflows VkDeviceSize")
    elseif(probe_case STREQUAL "vertex-size-overflow")
        set(expected_text "Triangle BLAS vertex range size overflows VkDeviceSize")
    elseif(probe_case STREQUAL "indexed-vertex-range")
        set(expected_text "Triangle BLAS vertex data range exceeds its declared buffer")
    else()
        set(expected_text "Triangle BLAS index data range exceeds its declared buffer")
    endif()

    nr_require_failure_probe(
        PROBE "${NR_RHI_AS_GEOMETRY_FAILURE_PROBE}"
        ARGUMENTS "${probe_case}"
        CONTEXT "RHI AS geometry '${probe_case}'"
        REQUIRED_OUTPUT "[NR ASSERT]" "${expected_text}"
    )
endforeach()
