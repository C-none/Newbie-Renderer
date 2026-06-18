nr_add_contract_test(nr_scene_bridge_plan_contract_test nr_scene_bridge_plan_contract_test.cpp "cpu;integration;scene"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_scene_render_bridge_contract_test nr_scene_render_bridge_contract_test.cpp "cpu;integration;scene;renderer-boundary"
    nrscene
    nrresource
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_scene_runtime_extraction_contract_test nr_scene_runtime_extraction_contract_test.cpp "cpu;integration;scene"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)
