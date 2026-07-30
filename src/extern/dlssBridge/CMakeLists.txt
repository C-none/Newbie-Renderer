if(NOT NR_DLSS_NGX_SDK_OPERATIONAL OR NOT MSVC)
    return()
endif()

find_package(Vulkan REQUIRED)

add_library(nr_dlss_bridge SHARED
    "nrDlssBridge.cpp"
    "nrDlssBridge.def"
    "include/nrDlssBridge.h"
)
target_compile_definitions(nr_dlss_bridge PRIVATE NR_DLSS_BRIDGE_BUILD)
target_compile_options(nr_dlss_bridge PRIVATE /std:c++latest /permissive-)
target_include_directories(nr_dlss_bridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(nr_dlss_bridge PRIVATE
    Vulkan::Vulkan
    Advapi32
    User32
    "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/../DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_s_dbg.lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${CMAKE_CURRENT_SOURCE_DIR}/../DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib>"
)
set_target_properties(nr_dlss_bridge PROPERTIES
    FOLDER "extern/dlss"
    OUTPUT_NAME "nr_dlss_bridge"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
if(COMMAND nr_apply_target_settings)
    nr_apply_target_settings(nr_dlss_bridge)
else()
    target_compile_options(nr_dlss_bridge PRIVATE /EHsc /W4 /WX
        "$<$<CONFIG:Debug>:/Zi>"
        "$<$<CONFIG:Release>:/O2>"
    )
    target_link_options(nr_dlss_bridge PRIVATE
        "$<$<CONFIG:Debug>:/DEBUG:FULL>"
        "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
    )
endif()

set(nr_dlss_bridge_artifact_dir
    "${CMAKE_CURRENT_SOURCE_DIR}/artifacts/windows-x86_64/release"
)
add_custom_target(nr_publish_dlss_bridge
    COMMAND "${CMAKE_COMMAND}"
        "-DNR_PUBLISH_CONFIG=$<CONFIG>"
        "-DNR_PUBLISH_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DNR_BRIDGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DNR_DLSS_SDK_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../DLSS"
        "-DNR_BRIDGE_BUILD_DLL=$<TARGET_FILE:nr_dlss_bridge>"
        "-DNR_BRIDGE_ARTIFACT_DIR=${nr_dlss_bridge_artifact_dir}"
        "-DNR_ARTIFACT_HELPER=${CMAKE_SOURCE_DIR}/cmake/NrDlssBridgeArtifact.cmake"
        -P "${CMAKE_SOURCE_DIR}/cmake/PublishDlssBridge.cmake"
    DEPENDS nr_dlss_bridge
    COMMENT "Publishing the MSVC Release DLSS bridge artifact"
    USES_TERMINAL
    VERBATIM
)
set_target_properties(nr_publish_dlss_bridge PROPERTIES FOLDER "extern/dlss")
