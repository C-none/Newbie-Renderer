nr_add_contract_test(nr_renderer_camera_override_contract_test nr_renderer_camera_override_contract_test.cpp "gpu;window;integration;renderer"
    nr_test_options
    nrrenderpasses
    nrrenderer
    nrscene
    nrload
    nrrhi
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(nr_renderer_maintenance9_ownership_contract_test nr_renderer_maintenance9_ownership_contract_test.cpp "gpu;window;integration;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_pipeline_runtime_descriptor_ownership_contract_test nr_pipeline_runtime_descriptor_ownership_contract_test.cpp "gpu;window;integration;renderer;descriptor"
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)

nr_add_executable(nr_pipeline_runtime_failure_probe
    nr_pipeline_runtime_failure_probe.cpp
)
set_target_properties(nr_pipeline_runtime_failure_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_pipeline_runtime_failure_probe
    nr_pipeline_runtime_failure_probe.cpp
    PRIVATE
        nrrenderer
        nrrhi
        utils
        dependency_graphics
)

add_test(
    NAME nr_pipeline_runtime_failure_contract_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_PIPELINE_RUNTIME_FAILURE_PROBE=$<TARGET_FILE:nr_pipeline_runtime_failure_probe>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_pipeline_runtime_failure.cmake
)
nr_set_registered_test_contract(
    nr_pipeline_runtime_failure_contract_test
    "gpu;window;integration;renderer;descriptor"
)

nr_add_executable(nr_render_graph_executor_failure_probe
    nr_render_graph_executor_failure_probe.cpp
)
set_target_properties(nr_render_graph_executor_failure_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_render_graph_executor_failure_probe
    nr_render_graph_executor_failure_probe.cpp
    PRIVATE
        nrrenderer
        nrrhi
        utils
        dependency_graphics
)

add_test(
    NAME nr_render_graph_executor_failure_contract_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_RENDER_GRAPH_EXECUTOR_FAILURE_PROBE=$<TARGET_FILE:nr_render_graph_executor_failure_probe>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_render_graph_executor_failure.cmake
)
nr_set_registered_test_contract(
    nr_render_graph_executor_failure_contract_test
    "gpu;window;integration;renderer;rdg"
)
