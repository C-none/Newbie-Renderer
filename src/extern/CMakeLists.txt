set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include("${CMAKE_SOURCE_DIR}/cmake/NrBundledSlangPackage.cmake")

unset(NR_USE_BUNDLED_SLANG_PACKAGE CACHE)
set(NR_BUNDLED_SLANG_PACKAGE_ROOT "${CMAKE_BINARY_DIR}/_packages/slang" CACHE PATH "Build-tree root for the bundled Slang package.")
set(NR_BUNDLED_SLANG_PACKAGE_CONFIGS "" CACHE STRING "Semicolon-separated configs to install for the bundled Slang package. Empty means the active generator configs.")

nr_ensure_bundled_slang_package(
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/slang"
    BINARY_DIR "${NR_BUNDLED_SLANG_PACKAGE_ROOT}/build"
    INSTALL_DIR "${NR_BUNDLED_SLANG_PACKAGE_ROOT}/install"
    CONFIGS "${NR_BUNDLED_SLANG_PACKAGE_CONFIGS}"
)

set(slang_DIR "${NR_BUNDLED_SLANG_PACKAGE_ROOT}/install/cmake" CACHE PATH "Bundled Slang package config directory." FORCE)
set(NR_SLANG_RUNTIME_DIR "${NR_BUNDLED_SLANG_PACKAGE_ROOT}/install/bin" CACHE PATH "Runtime files from the bundled Slang package." FORCE)
set(NR_SLANG_RUNTIME_STAMP "${NR_BUNDLED_SLANG_PACKAGE_ROOT}/install/.nr-bundled-slang-package.signature" CACHE FILEPATH "Bundled Slang package runtime copy stamp." FORCE)

find_package(slang CONFIG REQUIRED)
if(NOT TARGET slang::slang)
    message(FATAL_ERROR "find_package(slang) did not provide the expected slang::slang target.")
endif()

find_package(flecs CONFIG REQUIRED)

option(NR_ENABLE_NSIGHT_GRAPHICS_SDK "Enable Nsight Graphics SDK integration wrappers." ON)
set(NR_NSIGHT_GRAPHICS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/NsightGraphics/0.9.0")
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK AND NOT EXISTS "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include/NGFX_GraphicsCapture_Vulkan.h")
    message(FATAL_ERROR "NR_ENABLE_NSIGHT_GRAPHICS_SDK is ON but Nsight Graphics SDK 0.9.0 headers were not found at ${NR_NSIGHT_GRAPHICS_SDK_DIR}/include")
endif()

set(DEPENDENCY_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/exportDependency.ixx"
)
set(DEPENDENCY_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nsightGraphicsSdkImpl.cpp"
)

set_source_files_properties(
    ${DEPENDENCY_IMPLEMENTATION_SOURCES}
    PROPERTIES CXX_SCAN_FOR_MODULES OFF
)

nr_add_library(dependency STATIC)
target_link_libraries(dependency 
PUBLIC
    slang::slang
    $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
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
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK)
    target_include_directories(dependency
        PRIVATE
            "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include"
    )
endif()
target_compile_definitions(dependency
    PRIVATE
        NR_ENABLE_NSIGHT_GRAPHICS_SDK=$<BOOL:${NR_ENABLE_NSIGHT_GRAPHICS_SDK}>
)
if(MSVC)
    # Flecs C++ lifecycle templates instantiate in downstream modules.
    target_compile_options(dependency
        PUBLIC /wd4291
        PRIVATE /wd4702
    )
endif()
target_sources(dependency
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES ${DEPENDENCY_MODULE_UNITS}
    PRIVATE
        ${DEPENDENCY_IMPLEMENTATION_SOURCES}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${DEPENDENCY_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_MODULE_UNITS}
)
set_target_properties(dependency PROPERTIES FOLDER "extern")
