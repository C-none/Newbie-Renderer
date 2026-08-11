nr_add_executable(neuralMaterialViewer neuralMaterialViewer.cpp)
set_target_properties(neuralMaterialViewer PROPERTIES FOLDER "tools")

nr_link_module_dependencies(neuralMaterialViewer neuralMaterialViewer.cpp PRIVATE
    nrapp
    nroptions
    nrrenderpasses
    nrrenderer
    nrrhi
    utils
    dependency_graphics
)
