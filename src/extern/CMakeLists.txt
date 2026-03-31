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

set(Aftermath_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/aftermath")
# Add slang submodule
# According to https://docs.shader-slang.org/en/latest/external/slang/docs/building.html#configure-and-build
# Keep embedded Slang build lean for engine integration.
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "Disable Slang tests for NewbieRenderer." FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "Disable Slang examples for NewbieRenderer." FORCE)
set(SLANG_ENABLE_GFX OFF CACHE BOOL "Disable Slang gfx layer for NewbieRenderer." FORCE)
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "Disable Slang RHI layer for NewbieRenderer." FORCE)
set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "Disable Slang glslang bridge for NewbieRenderer." FORCE)
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "Disable Slang language server for NewbieRenderer." FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "Disable Slang interpreter for NewbieRenderer." FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "Disable Slang replay tool for NewbieRenderer." FORCE)
set(SLANG_STANDARD_MODULE_DEVELOP_BUILD OFF CACHE BOOL "Disable Slang standard-module dev mode to avoid Debug-only assertions." FORCE)
add_extern_library(slang)

set(FLECS_STATIC ON CACHE BOOL "Build static flecs library for NewbieRenderer." FORCE)
set(FLECS_SHARED OFF CACHE BOOL "Disable shared flecs library for NewbieRenderer." FORCE)
set(FLECS_TESTS OFF CACHE BOOL "Disable flecs tests." FORCE)
add_extern_library(flecs)

if(MSVC AND TARGET flecs_static)
    target_compile_options(flecs_static PUBLIC
        "$<$<COMPILE_LANGUAGE:CXX>:/wd5260>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4702>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd4267>"
    )
endif()

file(GLOB MODULE_UNITS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)
nr_add_library(dependency STATIC)
target_link_libraries(dependency 
PUBLIC
    slang
    flecs::flecs_static
    glfw
    imgui::imgui
    glm::glm
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    Tracy::TracyClient
    assimp::assimp
    libjpeg-turbo::turbojpeg
)
target_include_directories(dependency
PUBLIC
    ${Stb_INCLUDE_DIR}
)
target_sources(dependency
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES  ${MODULE_UNITS}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${MODULE_UNITS}
)
set_target_properties(dependency PROPERTIES FOLDER "extern")

