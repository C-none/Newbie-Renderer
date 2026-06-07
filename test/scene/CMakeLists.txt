function(nr_add_scene_contract_test target_name source_file)
    nr_add_executable(${target_name}
        ${source_file}
    )
    set_target_properties(${target_name} PROPERTIES FOLDER "Test/scene")

    target_link_libraries(${target_name} PRIVATE
        nrscene
        nrload
        nrresource
        nrrhi
        utils
        dependency
    )
    add_dependencies(${target_name} nrscene nrload nrresource nrrhi utils dependency)
    nr_add_clang_module_object_depends(${source_file} nrscene nrload nrresource nrrhi utils dependency)

    target_compile_definitions(${target_name} PRIVATE
        NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /wd4702)
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

nr_add_scene_contract_test(nr_scene_bridge_plan_test nr_scene_bridge_plan_test.cpp)
nr_add_scene_contract_test(nr_scene_camera_projection_contract_test nr_scene_camera_projection_contract_test.cpp)
nr_add_scene_contract_test(nr_scene_packet_readiness_test nr_scene_packet_readiness_test.cpp)
