nr_add_executable(nr_shader_compile_check EXCLUDE_FROM_ALL
    nrShaderCompileCheck.cpp
)
set_target_properties(nr_shader_compile_check PROPERTIES
    FOLDER "tools"
)

nr_link_module_dependencies(
    nr_shader_compile_check
    nrShaderCompileCheck.cpp
    PRIVATE
        nrrhi
)

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/tools/nrShaderCompileCheck-$<CONFIG>.cmake"
    CONTENT "set(NR_SHADER_COMPILE_CHECK_EXECUTABLE [==[$<TARGET_FILE:nr_shader_compile_check>]==])\n"
    TARGET nr_shader_compile_check
)

add_subdirectory(neuralMaterialViewer)
