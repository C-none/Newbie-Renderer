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

function(disable_cxx_scan_for_targets dir_path)
    get_property(targets DIRECTORY "${dir_path}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
        if(TARGET ${target})
            get_target_property(target_type ${target} TYPE)
            if(target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
                set_target_properties(${target} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
            endif()
        endif()
    endforeach()

    get_property(subdirs DIRECTORY "${dir_path}" PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirs})
        disable_cxx_scan_for_targets("${subdir}")
    endforeach()
endfunction()

# Helper function specifically for external libraries
function(add_extern_library lib_name)
    # Add the subdirectory
    add_subdirectory(${lib_name})
    # Move all targets to extern/${lib_name} folder
    move_targets_to_folder("${CMAKE_CURRENT_SOURCE_DIR}/${lib_name}" "extern/${lib_name}")
endfunction()

# set(Aftermath_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/aftermath")
# Add slang submodule
# According to https://docs.shader-slang.org/en/latest/external/slang/docs/building.html#configure-and-build
# Keep embedded Slang build lean for engine integration.
set(SLANG_LIB_TYPE STATIC CACHE STRING "Build Slang as a static library for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_CUDA OFF CACHE BOOL "Disable CUDA support detection for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_OPTIX OFF CACHE BOOL "Disable OptiX support detection for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_NVAPI OFF CACHE BOOL "Disable NVAPI support detection for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_AFTERMATH OFF CACHE BOOL "Disable Slang Aftermath integration for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_TESTS OFF CACHE BOOL "Disable Slang tests for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "Disable Slang examples for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_GFX OFF CACHE BOOL "Disable Slang gfx layer for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "Disable Slang RHI layer for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "Disable Slang runtime target for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "Disable Slang glslang bridge for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "Disable Slang language server for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "Disable Slang interpreter for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "Disable Slang command-line compiler for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "Disable Slang replay tool for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_DXIL OFF CACHE BOOL "Disable DXIL generation support for NewbieRenderer." FORCE)
# set(SLANG_ENABLE_PREBUILT_BINARIES OFF CACHE BOOL "Disable copying optional prebuilt Slang runtime binaries for NewbieRenderer." FORCE)
# set(SLANG_EXCLUDE_DAWN ON CACHE BOOL "Exclude WebGPU Dawn artifacts from the embedded Slang build." FORCE)
# set(SLANG_EXCLUDE_TINT ON CACHE BOOL "Exclude slang-tint artifacts from the embedded Slang build." FORCE)
# set(SLANG_SLANG_LLVM_FLAVOR DISABLE CACHE STRING "Disable LLVM support for SPIR-V-only NewbieRenderer builds." FORCE)
# set(SLANG_ENABLE_RELEASE_DEBUG_INFO OFF CACHE BOOL "Disable debug info in Release builds of Slang for NewbieRenderer." FORCE)
block(SCOPE_FOR VARIABLES)
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_extern_library(slang)

    if(MSVC AND CMAKE_GENERATOR MATCHES "^Visual Studio")
        # External Slang targets do not use C++20 modules, and disabling the
        # Visual Studio scan avoids noisy false-positive PCH diagnostics.
        disable_cxx_scan_for_targets("${CMAKE_CURRENT_SOURCE_DIR}/slang")
    endif()
endblock()

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
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)
target_include_directories(dependency
PUBLIC
    ${Stb_INCLUDE_DIR}
)
if(MSVC)
    # Flecs C++ lifecycle templates instantiate in downstream modules.
    target_compile_options(dependency PUBLIC /wd4291)
endif()
target_sources(dependency
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES  ${MODULE_UNITS}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${MODULE_UNITS}
)
set_target_properties(dependency PROPERTIES FOLDER "extern")

