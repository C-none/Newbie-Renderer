set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Generic function to move all targets in a subdirectory to a specified folder hierarchy
function(move_targets_to_folder dir_path folder_prefix)
    get_property(targets DIRECTORY "${dir_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
        if(TARGET ${target})
            get_target_property(folder_name ${target} FOLDER)
            if(folder_name AND NOT folder_name STREQUAL "NOTFOUND")
                # If target already has a folder, prefix it with the specified folder
                set_target_properties(${target} PROPERTIES FOLDER "${folder_prefix}/${folder_name}")
            else()
                # If target has no folder, put it directly under the specified folder
                set_target_properties(${target} PROPERTIES FOLDER "${folder_prefix}")
            endif()
        endif()
    endforeach()
    
    # Recursively process subdirectories
    get_property(subdirs DIRECTORY "${dir_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirs})
        move_targets_to_folder("${subdir}" "${folder_prefix}")
    endforeach()
endfunction()

# Helper function specifically for external libraries
function(add_extern_library lib_name)
    # Add the subdirectory
    add_subdirectory(${lib_name})
    # Move all targets to extern/${lib_name} folder
    move_targets_to_folder("${CMAKE_CURRENT_SOURCE_DIR}/${lib_name}" "extern/${lib_name}")
endfunction()

set(NVAPI_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nvapi")
# set(NVAPI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/nvapi")
# set(NVAPI_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/nvapi/amd64/nvapi64.lib")
set(Aftermath_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/aftermath")
# set(Aftermath_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/aftermath/include")
# set(Aftermath_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/aftermath/lib")

# Add slang submodule
# According to https://docs.shader-slang.org/en/latest/external/slang/docs/building.html#configure-and-build
set(SLANG_ENABLE_EXAMPLES False)
set(SLANG_ENABLE_TESTS False)
add_extern_library(slang)