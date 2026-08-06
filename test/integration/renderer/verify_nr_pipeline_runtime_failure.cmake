if(NOT DEFINED NR_PIPELINE_RUNTIME_FAILURE_PROBE)
    message(FATAL_ERROR "NR_PIPELINE_RUNTIME_FAILURE_PROBE is required.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NrFailureProbeContract.cmake")

function(require_pipeline_runtime_failure scenario expected_text)
    nr_require_failure_probe(
        PROBE "${NR_PIPELINE_RUNTIME_FAILURE_PROBE}"
        ARGUMENTS "${scenario}"
        CONTEXT "PipelineRuntime '${scenario}'"
        REQUIRED_OUTPUT "[NR ASSERT]" "${expected_text}"
    )
endfunction()

require_pipeline_runtime_failure("uninitialized" "bindingSetsForFrame requires an initialized pipeline")
require_pipeline_runtime_failure("invalid-handle" "requires a valid pass-binding handle")
require_pipeline_runtime_failure("stale-handle" "rejected a stale pass-binding handle")
require_pipeline_runtime_failure("foreign-handle" "owned by another runtime")
require_pipeline_runtime_failure("missing-runtime" "requires a valid PipelineRuntime shared pointer")
require_pipeline_runtime_failure("cold-empty-prepare" "ComputePassBuilder::prepare requires a callback")
require_pipeline_runtime_failure("patch-empty-prepare" "ComputePassPatchBuilder::prepare requires a callback")
