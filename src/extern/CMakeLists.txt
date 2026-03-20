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
set(SLANG_LIB_TYPE STATIC CACHE STRING "Build Slang as static library for NewbieRenderer." FORCE)
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "Disable Slang examples." FORCE)
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "Disable Slang tests." FORCE)
set(SLANG_ENABLE_GFX OFF CACHE BOOL "Disable legacy gfx target." FORCE)
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "Disable slangd executable." FORCE)
set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "Disable slangc executable." FORCE)
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "Disable slangi executable." FORCE)
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "Disable slang-replay executable." FORCE)
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "Disable slang-rhi integration for host build." FORCE)

# Keep Slang's SPIR-V dependency chain available, but strip third-party CLI targets.
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "Disable SPIRV-Tools CLI executables." FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "Disable SPIRV-Tools tests." FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "Disable glslang standalone binaries." FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "Disable SPIR-V remapper target." FORCE)
set(LZ4_BUILD_CLI OFF CACHE BOOL "Disable lz4 CLI executable." FORCE)
set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE BOOL "Disable lz4c legacy CLI executable." FORCE)
add_extern_library(slang)

set(FLECS_STATIC ON CACHE BOOL "Build static flecs library for NewbieRenderer." FORCE)
set(FLECS_SHARED OFF CACHE BOOL "Disable shared flecs library for NewbieRenderer." FORCE)
set(FLECS_TESTS OFF CACHE BOOL "Disable flecs tests." FORCE)
add_extern_library(flecs)

if(MSVC AND TARGET flecs_static)
    target_compile_options(flecs_static PUBLIC
        "$<$<COMPILE_LANGUAGE:CXX>:/wd5260>"
        "$<$<COMPILE_LANGUAGE:CXX>:/wd2220>"
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

# if(MSVC)
    # Some third-party headers currently emit warnings when imported as header units.
    # Keep /WX for project code while silencing those third-party diagnostics here.
    # target_compile_options(dependency PRIVATE /wd4267 /wd4702 /wd5260)
# endif()
