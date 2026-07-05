# Embedded Triangle remains a manually-run sample target, not a default CTest.
nr_add_executable(embeddedTriangle embeddedTriangle.cpp)
set_target_properties(embeddedTriangle PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(embeddedTriangle embeddedTriangle.cpp PRIVATE
    nrapp
    nrrenderpasses
    nrrenderer
    utils
    dependency
)

nr_add_executable(nr_normal_buffer_ui_smoke_test normalBufferUiSmoke.cpp)
set_target_properties(nr_normal_buffer_ui_smoke_test PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(nr_normal_buffer_ui_smoke_test normalBufferUiSmoke.cpp PRIVATE
    nrapp
    nrrenderpasses
    nrrenderer
    nrscene
    nrload
    utils
    dependency
)

add_test(NAME nr_normal_buffer_ui_smoke_test COMMAND nr_normal_buffer_ui_smoke_test)
set_tests_properties(nr_normal_buffer_ui_smoke_test PROPERTIES LABELS "gpu;window;smoke;app")

nr_add_executable(nr_rt_object_material_smoke_test rtObjectMaterialSmoke.cpp)
set_target_properties(nr_rt_object_material_smoke_test PROPERTIES FOLDER "Test/smoke/app")

nr_link_module_dependencies(nr_rt_object_material_smoke_test rtObjectMaterialSmoke.cpp PRIVATE
    nrapp
    nrrenderpasses
    nrrenderer
    nrscene
    nrload
    utils
    dependency
)

add_test(NAME nr_rt_object_material_smoke_test COMMAND nr_rt_object_material_smoke_test)
set_tests_properties(nr_rt_object_material_smoke_test PROPERTIES LABELS "gpu;window;smoke;app;rt;assets")
