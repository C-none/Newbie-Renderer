nr_add_executable(nr_scene_phase1_test
    nr_scene_phase1_test.cpp
)
set_target_properties(nr_scene_phase1_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase1_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase1_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase1_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase1_test COMMAND nr_scene_phase1_test)

nr_add_executable(nr_scene_phase2_test
    nr_scene_phase2_test.cpp
)
set_target_properties(nr_scene_phase2_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase2_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase2_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase2_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase2_test COMMAND nr_scene_phase2_test)

nr_add_executable(nr_scene_phase25_test
    nr_scene_phase25_test.cpp
)
set_target_properties(nr_scene_phase25_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase25_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase25_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase25_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase25_test COMMAND nr_scene_phase25_test)

nr_add_executable(nr_scene_phase3_test
    nr_scene_phase3_test.cpp
)
set_target_properties(nr_scene_phase3_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase3_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase3_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase3_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase3_test COMMAND nr_scene_phase3_test)

nr_add_executable(nr_scene_phase4_test
    nr_scene_phase4_test.cpp
)
set_target_properties(nr_scene_phase4_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase4_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase4_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase4_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase4_test COMMAND nr_scene_phase4_test)

nr_add_executable(nr_scene_phase5_test
    nr_scene_phase5_test.cpp
)
set_target_properties(nr_scene_phase5_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_phase5_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_phase5_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_phase5_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_phase5_test COMMAND nr_scene_phase5_test)

nr_add_executable(nr_scene_upload_readback_test
    nr_scene_upload_readback_test.cpp
)
set_target_properties(nr_scene_upload_readback_test PROPERTIES FOLDER "Test/scene")

target_link_libraries(nr_scene_upload_readback_test PRIVATE
    nrscene
    nrload
    nrresource
    nrrhi
    utils
    dependency
)

target_compile_definitions(nr_scene_upload_readback_test PRIVATE
    NR_PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(nr_scene_upload_readback_test PRIVATE /wd4702)
endif()

add_test(NAME nr_scene_upload_readback_test COMMAND nr_scene_upload_readback_test)
