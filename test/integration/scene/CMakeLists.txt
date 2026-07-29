nr_add_contract_test(nr_scene_bridge_plan_contract_test nr_scene_bridge_plan_contract_test.cpp "cpu;integration;scene"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency_math
)

nr_add_contract_test(nr_scene_render_bridge_contract_test nr_scene_render_bridge_contract_test.cpp "cpu;integration;scene;renderer-boundary"
    nrscene
    nrresource
    nrrhi
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(nr_scene_runtime_extraction_contract_test nr_scene_runtime_extraction_contract_test.cpp "cpu;integration;scene"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency_math
)

nr_add_contract_test(nr_scene_as_mesh_contract_test nr_scene_as_mesh_contract_test.cpp "gpu;window;integration;scene;rt"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency_graphics
)

nr_add_contract_test(nr_scene_rt_material_contract_test nr_scene_rt_material_contract_test.cpp "cpu;integration;scene;rt;assets"
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency_services
    dependency_math
)
