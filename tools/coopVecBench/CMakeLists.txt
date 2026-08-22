nr_add_executable(coopVecBench coopVecBench.cpp)
set_target_properties(coopVecBench PROPERTIES FOLDER "tools")

nr_link_module_dependencies(coopVecBench coopVecBench.cpp PRIVATE
    nrrhi
    dependency_assets
    dependency_graphics
)
