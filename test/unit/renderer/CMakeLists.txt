nr_add_contract_test(nr_renderer_viewer_camera_controller_contract_test nr_renderer_viewer_camera_controller_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    utils
    dependency_math
)

nr_add_contract_test(nr_renderer_frame_services_contract_test nr_renderer_frame_services_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    utils
)

nr_add_contract_test(nr_renderer_option_effect_contract_test nr_renderer_option_effect_contract_test.cpp "fast;cpu;unit;renderer;options;effects"
    nrrenderer
    nroptions
    utils
)

nr_add_contract_test(nr_renderer_camera_jitter_contract_test nr_renderer_camera_jitter_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    nrscene
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(nr_renderer_benchmark_contract_test nr_renderer_benchmark_contract_test.cpp "fast;cpu;unit;renderer;benchmark"
    nrrenderer
    utils
    dependency_graphics
)

nr_add_contract_test(nr_renderer_lifetime_contract_test nr_renderer_lifetime_contract_test.cpp "fast;cpu;unit;renderer;lifetime"
    nrrenderer
    utils
)

nr_add_contract_test(nr_render_graph_builder_contract_test nr_render_graph_builder_contract_test.cpp "fast;cpu;unit;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_render_graph_compiler_contract_test nr_render_graph_compiler_contract_test.cpp "fast;cpu;unit;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_render_graph_executor_contract_test nr_render_graph_executor_contract_test.cpp "fast;cpu;unit;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)

nr_add_executable(nr_render_graph_failure_probe
    nr_render_graph_failure_probe.cpp
)
set_target_properties(nr_render_graph_failure_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_render_graph_failure_probe
    nr_render_graph_failure_probe.cpp
    PRIVATE
        nrrenderer
        nrrhi
        utils
        dependency_graphics
)

add_test(
    NAME nr_render_graph_failure_contract_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_RENDER_GRAPH_FAILURE_PROBE=$<TARGET_FILE:nr_render_graph_failure_probe>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_render_graph_failure.cmake
)
nr_set_registered_test_contract(nr_render_graph_failure_contract_test "fast;cpu;unit;renderer;rdg")
