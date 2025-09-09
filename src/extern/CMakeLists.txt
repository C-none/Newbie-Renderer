set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Add slang submodule
add_subdirectory(slang)

# Function to recursively move all slang targets to extern/slang folder hierarchy
function(move_targets_to_slang_folder dir_path)
    get_property(targets DIRECTORY "${dir_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
        if(TARGET ${target})
            get_target_property(folder_name ${target} FOLDER)
            if(folder_name AND NOT folder_name STREQUAL "NOTFOUND")
                # If target already has a folder, prefix it with "extern/slang/"
                set_target_properties(${target} PROPERTIES FOLDER "extern/slang/${folder_name}")
            else()
                # If target has no folder, put it directly under "extern/slang"
                set_target_properties(${target} PROPERTIES FOLDER "extern/slang")
            endif()
        endif()
    endforeach()
    
    # Recursively process subdirectories
    get_property(subdirs DIRECTORY "${dir_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirs})
        move_targets_to_slang_folder("${subdir}")
    endforeach()
endfunction()

# Call the function to reorganize slang targets
move_targets_to_slang_folder("${CMAKE_CURRENT_SOURCE_DIR}/slang")
