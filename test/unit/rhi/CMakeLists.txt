nr_add_contract_test(nr_rhi_descriptor_contract_test nr_rhi_descriptor_contract_test.cpp "fast;cpu;unit;rhi"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_resource_ops_contract_test nr_rhi_resource_ops_contract_test.cpp "fast;cpu;unit;rhi"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_swapchain_format_contract_test nr_rhi_swapchain_format_contract_test.cpp "fast;cpu;unit;rhi"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_sbt_layout_contract_test nr_rhi_sbt_layout_contract_test.cpp "fast;cpu;unit;rhi;rt"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_shader_cursor_contract_test nr_rhi_shader_cursor_contract_test.cpp "fast;cpu;unit;rhi;shader"
    nrrhi
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(
    nr_rhi_neural_appearance_shader_contract_test
    nr_rhi_neural_appearance_shader_contract_test.cpp
    "gpu;unit;rhi;shader;neural-appearance;slow"
    nrrhi
    utils
    dependency_graphics
)
set_tests_properties(
    nr_rhi_neural_appearance_shader_contract_test
    PROPERTIES TIMEOUT 900
)

nr_add_contract_test(nr_rhi_rt_shader_reflection_contract_test nr_rhi_rt_shader_reflection_contract_test.cpp "fast;cpu;unit;rhi;rt;shader"
    nrresource
    nrrhi
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(nr_rhi_fas_shader_contract_test nr_rhi_fas_shader_contract_test.cpp "gpu;unit;rhi;rt;shader;slow"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_path_tracing_fas_shader_contract_test nr_rhi_path_tracing_fas_shader_contract_test.cpp "cpu;unit;rhi;rt;shader;slow"
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_rhi_lobe_specific_normal_shader_contract_test nr_rhi_lobe_specific_normal_shader_contract_test.cpp "cpu;unit;rhi;rt;shader;slow"
    nrrhi
    utils
    dependency_math
    dependency_graphics
)
