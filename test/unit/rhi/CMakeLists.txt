nr_add_contract_test(nr_rhi_descriptor_contract_test nr_rhi_descriptor_contract_test.cpp "fast;cpu;unit;rhi"
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_rhi_resource_ops_contract_test nr_rhi_resource_ops_contract_test.cpp "fast;cpu;unit;rhi"
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_rhi_sbt_layout_contract_test nr_rhi_sbt_layout_contract_test.cpp "fast;cpu;unit;rhi;rt"
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_rhi_shader_cursor_contract_test nr_rhi_shader_cursor_contract_test.cpp "fast;cpu;unit;rhi;shader"
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_rhi_rt_shader_reflection_contract_test nr_rhi_rt_shader_reflection_contract_test.cpp "fast;cpu;unit;rhi;rt;shader"
    nrresource
    nrrhi
    utils
    dependency
)
