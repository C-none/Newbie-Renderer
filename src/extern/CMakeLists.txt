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
find_package(OpenEXR CONFIG REQUIRED)
if(NOT TARGET OpenEXR::OpenEXR)
    message(FATAL_ERROR "find_package(OpenEXR) did not provide the expected OpenEXR::OpenEXR target.")
endif()

add_subdirectory(dlssBridge)

option(NR_ENABLE_NSIGHT_GRAPHICS_SDK "Enable Nsight Graphics SDK integration wrappers." ON)
set(NR_NSIGHT_GRAPHICS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/NsightGraphics/0.9.0")
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK AND NOT EXISTS "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include/NGFX_GraphicsCapture_Vulkan.h")
    message(FATAL_ERROR "NR_ENABLE_NSIGHT_GRAPHICS_SDK is ON but Nsight Graphics SDK 0.9.0 headers were not found at ${NR_NSIGHT_GRAPHICS_SDK_DIR}/include")
endif()

file(GLOB DEPENDENCY_MODULE_UNITS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.ixx"
)
file(GLOB_RECURSE NR_SHADER_CODEGEN_SLANG_DEPENDENCIES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shader/*.slang"
)
set(DEPENDENCY_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nsightGraphicsSdkImpl.cpp"
)
set(DEPENDENCY_MODULE_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyDlss.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNsight.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyWindow.cpp"
)

set(NR_SHADER_SHARE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(NR_SHADER_SHARE_GENERATED_MODULE "${NR_SHADER_SHARE_GENERATED_DIR}/dependencyShaderShare.ixx")

nr_add_executable(nr_shader_share_codegen
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/nrShaderShareCodegen.cpp"
)
target_link_libraries(nr_shader_share_codegen PRIVATE slang::slang)
set_target_properties(nr_shader_share_codegen PROPERTIES FOLDER "extern/tools")

add_custom_command(
    OUTPUT "${NR_SHADER_SHARE_GENERATED_MODULE}"
    COMMAND "$<TARGET_FILE:nr_shader_share_codegen>"
        --shader-root "${CMAKE_SOURCE_DIR}/shader"
        --share-root "${CMAKE_SOURCE_DIR}/shader/include/share"
        --root-module common
        --output "${NR_SHADER_SHARE_GENERATED_MODULE}"
        --module dependency.shaderShare
        --namespace nr::shader::share
    DEPENDS
        nr_shader_share_codegen
        ${NR_SHADER_CODEGEN_SLANG_DEPENDENCIES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating dependency.shaderShare from Slang shared ABI declarations"
    VERBATIM
)
set_source_files_properties("${NR_SHADER_SHARE_GENERATED_MODULE}" PROPERTIES GENERATED TRUE)

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
    OpenEXR::OpenEXR
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
)
target_include_directories(dependency
PUBLIC
    ${Stb_INCLUDE_DIR}
PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/dlssBridge/include"
)
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK)
    target_include_directories(dependency
        PRIVATE
            "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include"
    )
endif()
target_compile_definitions(dependency
    PRIVATE
        NR_DLSS_BRIDGE_ENABLED=$<BOOL:${NR_DLSS_NGX_SDK_OPERATIONAL}>
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
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${NR_SHADER_SHARE_GENERATED_DIR}"
        FILES
            ${DEPENDENCY_MODULE_UNITS}
            "${NR_SHADER_SHARE_GENERATED_MODULE}"
    PRIVATE
        ${DEPENDENCY_IMPLEMENTATION_SOURCES}
        ${DEPENDENCY_MODULE_IMPLEMENTATION_SOURCES}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${DEPENDENCY_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_MODULE_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_MODULE_UNITS}
)
source_group("generated" FILES "${NR_SHADER_SHARE_GENERATED_MODULE}")
set_target_properties(dependency PROPERTIES FOLDER "extern")
