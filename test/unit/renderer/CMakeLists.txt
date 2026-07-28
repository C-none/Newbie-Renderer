nr_add_contract_test(nr_renderer_viewer_camera_controller_contract_test nr_renderer_viewer_camera_controller_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    utils
    dependency
)

nr_add_contract_test(nr_renderer_frame_services_contract_test nr_renderer_frame_services_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    utils
    dependency
)

nr_add_contract_test(nr_renderer_option_effect_contract_test nr_renderer_option_effect_contract_test.cpp "fast;cpu;unit;renderer;options;effects"
    nrrenderer
    nroptions
    utils
    dependency
)

nr_add_contract_test(nr_renderer_camera_jitter_contract_test nr_renderer_camera_jitter_contract_test.cpp "fast;cpu;unit;renderer"
    nrrenderer
    nrscene
    utils
    dependency
)

nr_add_contract_test(nr_renderer_benchmark_contract_test nr_renderer_benchmark_contract_test.cpp "fast;cpu;unit;renderer;benchmark"
    nrrenderer
    utils
    dependency
)

nr_add_contract_test(nr_render_graph_builder_contract_test nr_render_graph_builder_contract_test.cpp "fast;cpu;unit;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_render_graph_compiler_contract_test nr_render_graph_compiler_contract_test.cpp "fast;cpu;unit;renderer;rdg"
    nrrenderer
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_render_graph_skeleton_contract_test nr_render_graph_skeleton_contract_test.cpp "fast;cpu;unit;renderer;rdg;skeleton"
    nrrenderer
    nrrhi
    utils
    dependency
)
