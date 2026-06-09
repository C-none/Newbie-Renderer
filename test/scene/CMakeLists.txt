function(nr_add_scene_contract_test target_name source_file)
    nr_add_executable(${target_name}
        ${source_file}
    )
    set_target_properties(${target_name} PROPERTIES FOLDER "Test/scene")

    nr_link_module_dependencies(${target_name} ${source_file} PRIVATE
        nrscene
        nrload
        nrresource
        nrrhi
        utils
        dependency
    )

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /wd4702)
    endif()

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

nr_add_scene_contract_test(nr_scene_bridge_plan_test nr_scene_bridge_plan_test.cpp)
nr_add_scene_contract_test(nr_scene_camera_projection_contract_test nr_scene_camera_projection_contract_test.cpp)
nr_add_scene_contract_test(nr_scene_packet_readiness_test nr_scene_packet_readiness_test.cpp)
