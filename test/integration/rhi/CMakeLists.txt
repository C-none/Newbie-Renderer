nr_add_contract_test(nr_rhi_device_capability_contract_test nr_rhi_device_capability_contract_test.cpp "gpu;window;integration;rhi"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_upload_readback_buffer_roundtrip_test nr_rhi_upload_readback_buffer_roundtrip_test.cpp "gpu;window;integration;rhi;upload;readback"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_acceleration_structure_geometry_contract_test nr_rhi_acceleration_structure_geometry_contract_test.cpp "gpu;window;integration;rhi;rt;requires-unsandboxed"
    nrrhi
    utils
    dependency_graphics
)

nr_add_executable(nr_rhi_acceleration_structure_geometry_failure_probe
    nr_rhi_acceleration_structure_geometry_failure_probe.cpp
)
set_target_properties(nr_rhi_acceleration_structure_geometry_failure_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_rhi_acceleration_structure_geometry_failure_probe
    nr_rhi_acceleration_structure_geometry_failure_probe.cpp
    PRIVATE
        nrrhi
        utils
        dependency_graphics
)

add_test(
    NAME nr_rhi_acceleration_structure_geometry_failure_contract_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_RHI_AS_GEOMETRY_FAILURE_PROBE=$<TARGET_FILE:nr_rhi_acceleration_structure_geometry_failure_probe>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_rhi_acceleration_structure_geometry_failure.cmake
)
nr_set_registered_test_contract(
    nr_rhi_acceleration_structure_geometry_failure_contract_test
    "gpu;window;integration;rhi;rt;requires-unsandboxed"
)

nr_add_contract_test(nr_rhi_environment_map_upload_test nr_rhi_environment_map_upload_test.cpp "gpu;window;integration;rhi;upload;readback;assets;slow"
    nrload
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_path_tracing_shadow_ray_gpu_contract_test nr_rhi_path_tracing_shadow_ray_gpu_contract_test.cpp "gpu;window;integration;rhi;rt;shader;slow;requires-unsandboxed"
    nrrhi
    utils
    dependency_graphics
    dependency_shader_share
)
