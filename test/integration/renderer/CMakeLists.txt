nr_add_contract_test(nr_renderer_camera_override_contract_test nr_renderer_camera_override_contract_test.cpp "gpu;window;integration;renderer"
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
