# Embedded Triangle remains a manually-run sample target, not a default CTest.
nr_add_executable(embeddedTriangle embeddedTriangle.cpp)
set_target_properties(embeddedTriangle PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(embeddedTriangle embeddedTriangle.cpp PRIVATE
    nr_test_options
    nrapp
    nrrenderpasses
    nrrenderer
    utils
    dependency_graphics
)

nr_add_executable(nr_normal_buffer_ui_smoke_test normalBufferUiSmoke.cpp)
set_target_properties(nr_normal_buffer_ui_smoke_test PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(nr_normal_buffer_ui_smoke_test normalBufferUiSmoke.cpp PRIVATE
    nr_test_options
    nrapp
    nrrenderpasses
    nrrenderer
    nrscene
    nrload
    utils
    dependency_graphics
)

add_test(NAME nr_normal_buffer_ui_smoke_test COMMAND nr_normal_buffer_ui_smoke_test)
nr_set_registered_test_contract(nr_normal_buffer_ui_smoke_test "gpu;window;smoke;app")

nr_add_executable(nr_rt_object_material_smoke_test rtObjectMaterialSmoke.cpp)
set_target_properties(nr_rt_object_material_smoke_test PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(nr_rt_object_material_smoke_test rtObjectMaterialSmoke.cpp PRIVATE
    nr_test_options
    nrapp
    nrrenderpasses
    nrrenderer
    nrscene
    nrload
    utils
    dependency_math
    dependency_graphics
)

add_test(NAME nr_rt_object_material_smoke_test COMMAND nr_rt_object_material_smoke_test)
nr_set_registered_test_contract(
    nr_rt_object_material_smoke_test
    "gpu;window;smoke;app;rt;assets;requires-unsandboxed"
)

nr_add_executable(nr_swapchain_out_of_date_smoke_test swapchainOutOfDateSmoke.cpp)
set_target_properties(nr_swapchain_out_of_date_smoke_test PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(nr_swapchain_out_of_date_smoke_test swapchainOutOfDateSmoke.cpp PRIVATE
    nr_test_options
    nrapp
    nrrenderpasses
    nrrenderer
    utils
    dependency_graphics
)

add_test(NAME nr_swapchain_out_of_date_smoke_test COMMAND nr_swapchain_out_of_date_smoke_test)
nr_set_registered_test_contract(
    nr_swapchain_out_of_date_smoke_test
    "gpu;window;smoke;app;swapchain;validation"
)
