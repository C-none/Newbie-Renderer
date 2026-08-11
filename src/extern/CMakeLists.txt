set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(flecs CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(Boost 1.91.0 EXACT CONFIG REQUIRED COMPONENTS json)
find_package(lua 5.5.0 EXACT CONFIG REQUIRED)

if(${Vulkan_VERSION} VERSION_LESS "1.4.341")
    message(FATAL_ERROR "Minimum required Vulkan version for C++ modules is 1.4.341. "
        "Found ${Vulkan_VERSION}."
    )
endif()

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

if(NOT TARGET OpenEXR::OpenEXR)
    message(FATAL_ERROR "find_package(OpenEXR) did not provide the expected OpenEXR::OpenEXR target.")
endif()
if(NOT TARGET mikktspace::mikktspace)
    message(FATAL_ERROR "find_package(mikktspace) did not provide the expected mikktspace::mikktspace target.")
endif()
if(NOT TARGET Boost::json)
    message(FATAL_ERROR "find_package(Boost) did not provide the expected Boost::json target.")
endif()
if(NOT TARGET lua::lua)
    message(FATAL_ERROR "find_package(lua) did not provide the expected lua::lua target.")
endif()

add_subdirectory(dlssBridge)

option(NR_ENABLE_NSIGHT_GRAPHICS_SDK "Enable Nsight Graphics SDK integration wrappers." ON)
set(NR_NSIGHT_GRAPHICS_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/NsightGraphics/0.9.0")
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK AND NOT EXISTS "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include/NGFX_GraphicsCapture_Vulkan.h")
    message(FATAL_ERROR "NR_ENABLE_NSIGHT_GRAPHICS_SDK is ON but Nsight Graphics SDK 0.9.0 headers were not found at ${NR_NSIGHT_GRAPHICS_SDK_DIR}/include")
endif()

file(GLOB_RECURSE NR_SHADER_SHARE_SLANG_DEPENDENCIES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shader/include/share/*.slang"
)

set(DEPENDENCY_GRAPHICS_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyVulkan.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyVma.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyWindow.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNsight.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyDlss.ixx"
)
set(DEPENDENCY_GRAPHICS_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyVmaImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNsightInjectionImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nsightGraphicsSdkImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyDlss.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNsight.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyWindow.cpp"
)
set(DEPENDENCY_MATH_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyMath.ixx"
)
set(DEPENDENCY_ECS_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyEcs.ixx"
)
set(DEPENDENCY_ECS_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyEcsImpl.cpp"
)
set(DEPENDENCY_SHADER_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencySlang.ixx"
)
set(DEPENDENCY_ASSETS_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyAssets.ixx"
)
set(DEPENDENCY_ASSETS_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyAssetsImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyAssetsMikk.cpp"
)
set(DEPENDENCY_UI_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyUi.ixx"
)
set(DEPENDENCY_SERVICES_MODULE_UNITS
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyJson.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyLua.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNetwork.ixx"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyProcessLease.ixx"
)
set(DEPENDENCY_SERVICES_IMPLEMENTATION_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyJson.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyLua.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNetwork.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyProcessLease.cpp"
)

set(NR_SHADER_SHARE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
set(NR_SHADER_SHARE_GENERATED_MODULE "${NR_SHADER_SHARE_GENERATED_DIR}/dependencyShaderShare.ixx")
set(NR_SHADER_SHARE_GENERATED_STAMP "${NR_SHADER_SHARE_GENERATED_DIR}/dependencyShaderShare.stamp")

add_executable(nr_shader_share_codegen
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/nrShaderShareCodegen.cpp"
)
target_link_libraries(nr_shader_share_codegen PRIVATE slang::slang)
nr_add_slang_runtime_dependency(nr_shader_share_codegen
    DEFER_TARGET_DEPENDENCY
    OUT_COPY_STAMP NR_SHADER_SHARE_CODEGEN_RUNTIME_STAMP
)
nr_apply_target_settings(nr_shader_share_codegen)
set_target_properties(nr_shader_share_codegen PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
    CXX_MODULE_STD OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE
    INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE
    FOLDER "extern/tools"
)

add_custom_command(
    OUTPUT "${NR_SHADER_SHARE_GENERATED_STAMP}"
    BYPRODUCTS "${NR_SHADER_SHARE_GENERATED_MODULE}"
    COMMAND "$<TARGET_FILE:nr_shader_share_codegen>"
        --shader-root "${CMAKE_SOURCE_DIR}/shader"
        --share-root "${CMAKE_SOURCE_DIR}/shader/include/share"
        --root-module common
        --output "${NR_SHADER_SHARE_GENERATED_MODULE}"
        --module dependency.shaderShare
        --namespace nr::shader::share
    COMMAND ${CMAKE_COMMAND} -E touch "${NR_SHADER_SHARE_GENERATED_STAMP}"
    DEPENDS
        nr_shader_share_codegen
        ${NR_SHADER_SHARE_CODEGEN_RUNTIME_STAMP}
        ${NR_SHADER_SHARE_SLANG_DEPENDENCIES}
    DEPENDS_EXPLICIT_ONLY
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating dependency.shaderShare from Slang shared ABI declarations"
    VERBATIM
)
add_custom_target(nr_generate_shader_share
    DEPENDS "${NR_SHADER_SHARE_GENERATED_STAMP}"
)
set_target_properties(nr_generate_shader_share PROPERTIES FOLDER "extern/tools")

set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyAssetsImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyVmaImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyEcsImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencyNsightInjectionImpl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/nsightGraphicsSdkImpl.cpp"
    PROPERTIES CXX_SCAN_FOR_MODULES OFF
)

nr_add_library(dependency_graphics STATIC)
target_link_libraries(dependency_graphics
    PUBLIC
        glfw
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
)
target_include_directories(dependency_graphics
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/dlssBridge/include"
)
if(NR_ENABLE_NSIGHT_GRAPHICS_SDK)
    target_include_directories(dependency_graphics
        PRIVATE
            "${NR_NSIGHT_GRAPHICS_SDK_DIR}/include"
    )
endif()
target_compile_definitions(dependency_graphics
    PRIVATE
        NR_DLSS_BRIDGE_ENABLED=$<BOOL:${NR_DLSS_NGX_SDK_OPERATIONAL}>
        NR_ENABLE_NSIGHT_GRAPHICS_SDK=$<BOOL:${NR_ENABLE_NSIGHT_GRAPHICS_SDK}>
)
target_sources(dependency_graphics
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_GRAPHICS_MODULE_UNITS}
    PRIVATE
        ${DEPENDENCY_GRAPHICS_IMPLEMENTATION_SOURCES}
)
set_target_properties(dependency_graphics PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_math STATIC)
target_link_libraries(dependency_math PUBLIC Microsoft::DirectXMath)
target_sources(dependency_math
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_MATH_MODULE_UNITS}
)
set_target_properties(dependency_math PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_ecs STATIC)
target_link_libraries(dependency_ecs
    PUBLIC
        $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>
)
if(MSVC)
    # Flecs C++ lifecycle templates instantiate in downstream modules.
    target_compile_options(dependency_ecs
        PUBLIC /wd4291
        PRIVATE /wd4702
    )
endif()
target_sources(dependency_ecs
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_ECS_MODULE_UNITS}
    PRIVATE
        ${DEPENDENCY_ECS_IMPLEMENTATION_SOURCES}
)
set_target_properties(dependency_ecs PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_shader STATIC)
target_link_libraries(dependency_shader PUBLIC slang::slang)
target_sources(dependency_shader
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_SHADER_MODULE_UNITS}
)
set_target_properties(dependency_shader PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_assets STATIC)
target_link_libraries(dependency_assets
    PUBLIC
        assimp::assimp
        OpenEXR::OpenEXR
        $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
    PRIVATE
        mikktspace::mikktspace
)
target_include_directories(dependency_assets PUBLIC ${Stb_INCLUDE_DIR})
target_sources(dependency_assets
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_ASSETS_MODULE_UNITS}
    PRIVATE
        ${DEPENDENCY_ASSETS_IMPLEMENTATION_SOURCES}
)
set_target_properties(dependency_assets PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_ui STATIC)
target_link_libraries(dependency_ui PUBLIC imgui::imgui)
target_sources(dependency_ui
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_UI_MODULE_UNITS}
)
set_target_properties(dependency_ui PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency_services STATIC)
target_link_libraries(dependency_services
    PUBLIC
        Boost::json
        lua::lua
)
if(WIN32)
    target_link_libraries(dependency_services PUBLIC ws2_32 mswsock)
endif()
target_sources(dependency_services
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES ${DEPENDENCY_SERVICES_MODULE_UNITS}
    PRIVATE
        ${DEPENDENCY_SERVICES_IMPLEMENTATION_SOURCES}
)
set_target_properties(dependency_services PROPERTIES FOLDER "extern/providers")

nr_add_library(dependency STATIC)
target_link_libraries(dependency
    PUBLIC
        dependency_graphics
        dependency_math
        dependency_ecs
        dependency_shader
        dependency_assets
        dependency_ui
        dependency_services
)
target_sources(dependency
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/exportDependency.ixx"
)
set_target_properties(dependency PROPERTIES FOLDER "extern")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${DEPENDENCY_GRAPHICS_MODULE_UNITS}
    ${DEPENDENCY_GRAPHICS_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_MATH_MODULE_UNITS}
    ${DEPENDENCY_ECS_MODULE_UNITS}
    ${DEPENDENCY_ECS_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_SHADER_MODULE_UNITS}
    ${DEPENDENCY_ASSETS_MODULE_UNITS}
    ${DEPENDENCY_ASSETS_IMPLEMENTATION_SOURCES}
    ${DEPENDENCY_UI_MODULE_UNITS}
    ${DEPENDENCY_SERVICES_MODULE_UNITS}
    ${DEPENDENCY_SERVICES_IMPLEMENTATION_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/exportDependency.ixx"
)

if(NR_BUILD_TESTS)
    nr_add_library(dependency_network_test STATIC)
    target_link_libraries(dependency_network_test
        PUBLIC
            dependency_services
    )
    target_sources(dependency_network_test
        PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/test"
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/test/dependencyNetworkTest.ixx"
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/test/dependencyNetworkTest.cpp"
    )
    set_target_properties(dependency_network_test PROPERTIES FOLDER "extern/test")
endif()

nr_add_library(dependency_shader_share STATIC)
add_dependencies(dependency_shader_share nr_generate_shader_share)
target_link_libraries(dependency_shader_share PUBLIC dependency_math)
target_sources(dependency_shader_share
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${NR_SHADER_SHARE_GENERATED_DIR}"
        FILES "${NR_SHADER_SHARE_GENERATED_MODULE}"
)
set_target_properties(dependency_shader_share PROPERTIES FOLDER "extern")
