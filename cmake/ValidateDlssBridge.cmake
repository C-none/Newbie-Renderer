foreach(required_variable IN ITEMS NR_BRIDGE_SOURCE_DIR NR_DLSS_SDK_DIR NR_BRIDGE_ARTIFACT_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "Validation input '${required_variable}' was not provided.")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/NrDlssBridgeArtifact.cmake")
nr_validate_dlss_bridge_artifact(
    "${NR_BRIDGE_SOURCE_DIR}"
    "${NR_DLSS_SDK_DIR}"
    "${NR_BRIDGE_ARTIFACT_DIR}"
)
message(STATUS "Validated DLSS bridge artifact: ${NR_VALIDATED_DLSS_BRIDGE_DLL}")
