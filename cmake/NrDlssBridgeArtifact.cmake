function(nr_sha256_normalized_text input output_variable)
    file(READ "${input}" normalized_content)
    string(REPLACE "\r\n" "\n" normalized_content "${normalized_content}")
    string(REPLACE "\r" "\n" normalized_content "${normalized_content}")
    string(SHA256 normalized_sha256 "${normalized_content}")
    set(${output_variable} "${normalized_sha256}" PARENT_SCOPE)
endfunction()

function(nr_compute_dlss_bridge_fingerprints bridge_source_dir dlss_sdk_dir output_prefix)
    set(bridge_inputs
        "${bridge_source_dir}/CMakeLists.txt"
        "${bridge_source_dir}/include/nrDlssBridge.h"
        "${bridge_source_dir}/nrDlssBridge.cpp"
        "${bridge_source_dir}/nrDlssBridge.def"
    )
    file(GLOB_RECURSE ngx_headers LIST_DIRECTORIES FALSE "${dlss_sdk_dir}/include/*.h")
    list(SORT ngx_headers)
    list(APPEND bridge_inputs ${ngx_headers})

    set(combined_fingerprint "")
    foreach(input IN LISTS bridge_inputs)
        if(NOT EXISTS "${input}")
            message(FATAL_ERROR "DLSS bridge fingerprint input is missing: ${input}")
        endif()
        nr_sha256_normalized_text("${input}" input_sha256)
        file(RELATIVE_PATH relative_input "${bridge_source_dir}" "${input}")
        string(APPEND combined_fingerprint "${relative_input}=${input_sha256}\n")
    endforeach()
    string(SHA256 inputs_sha256 "${combined_fingerprint}")
    nr_sha256_normalized_text("${bridge_source_dir}/include/nrDlssBridge.h" header_sha256)

    set(loader "${dlss_sdk_dir}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib")
    if(NOT EXISTS "${loader}")
        message(FATAL_ERROR "The DLSS NGX Release static loader is missing: ${loader}")
    endif()
    file(SHA256 "${loader}" loader_sha256)

    set(${output_prefix}_INPUTS_SHA256 "${inputs_sha256}" PARENT_SCOPE)
    set(${output_prefix}_HEADER_SHA256 "${header_sha256}" PARENT_SCOPE)
    set(${output_prefix}_LOADER_SHA256 "${loader_sha256}" PARENT_SCOPE)
endfunction()

function(nr_validate_dlss_bridge_artifact bridge_source_dir dlss_sdk_dir artifact_dir)
    set(bridge_dll "${artifact_dir}/nr_dlss_bridge.dll")
    set(manifest_path "${artifact_dir}/manifest.json")
    if(NOT EXISTS "${bridge_dll}")
        message(FATAL_ERROR
            "The tracked Release DLSS bridge is missing: ${bridge_dll}. "
            "Clone recursively, or publish it from the MSVC Release preset."
        )
    endif()
    if(NOT EXISTS "${manifest_path}")
        message(FATAL_ERROR "The tracked DLSS bridge manifest is missing: ${manifest_path}")
    endif()

    nr_compute_dlss_bridge_fingerprints("${bridge_source_dir}" "${dlss_sdk_dir}" expected)
    file(SHA256 "${bridge_dll}" expected_dll_sha256)
    file(READ "${manifest_path}" manifest)

    foreach(key IN ITEMS
        schemaVersion
        bridgeAbi
        sdkVersion
        platform
        architecture
        configuration
        runtime
        apiHeaderSha256
        bridgeInputsSha256
        ngxLoaderSha256
        dllSha256
        export
    )
        string(JSON manifest_${key} ERROR_VARIABLE json_error GET "${manifest}" "${key}")
        if(NOT json_error STREQUAL "NOTFOUND")
            message(FATAL_ERROR "Invalid DLSS bridge manifest key '${key}': ${json_error}")
        endif()
    endforeach()

    if(NOT manifest_schemaVersion STREQUAL "1" OR
       NOT manifest_bridgeAbi STREQUAL "1" OR
       NOT manifest_sdkVersion STREQUAL "310.7.0" OR
       NOT manifest_platform STREQUAL "windows" OR
       NOT manifest_architecture STREQUAL "x86_64" OR
       NOT manifest_configuration STREQUAL "Release" OR
       NOT manifest_runtime STREQUAL "MT" OR
       NOT manifest_export STREQUAL "nrDlssBridgeGetApi")
        message(FATAL_ERROR "The tracked DLSS bridge manifest does not describe the required Windows x86_64 Release ABI 1 artifact.")
    endif()
    if(NOT manifest_apiHeaderSha256 STREQUAL expected_HEADER_SHA256)
        message(FATAL_ERROR "The tracked DLSS bridge was built for a different C ABI header. Publish it again with MSVC Release.")
    endif()
    if(NOT manifest_bridgeInputsSha256 STREQUAL expected_INPUTS_SHA256)
        message(FATAL_ERROR "The tracked DLSS bridge is stale relative to its source or NGX headers. Publish it again with MSVC Release.")
    endif()
    if(NOT manifest_ngxLoaderSha256 STREQUAL expected_LOADER_SHA256)
        message(FATAL_ERROR "The tracked DLSS bridge was built against a different NGX static loader.")
    endif()
    if(NOT manifest_dllSha256 STREQUAL expected_dll_sha256)
        message(FATAL_ERROR "The tracked DLSS bridge DLL hash does not match its manifest.")
    endif()

    set(NR_VALIDATED_DLSS_BRIDGE_DLL "${bridge_dll}" PARENT_SCOPE)
endfunction()
