include_guard(GLOBAL)

function(nr_append_cmake_cache_arg out_var name value)
    string(REPLACE ";" "\\;" escaped_value "${value}")
    list(APPEND ${out_var} -D "${name}=${escaped_value}")
    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(nr_append_defined_cmake_cache_arg out_var name)
    if(DEFINED ${name} AND NOT "${${name}}" STREQUAL "")
        nr_append_cmake_cache_arg(${out_var} "${name}" "${${name}}")
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endif()
endfunction()

function(nr_find_visual_studio_dumpbin out_var)
    find_program(nr_dumpbin_executable NAMES dumpbin)
    if(nr_dumpbin_executable)
        set(${out_var} "${nr_dumpbin_executable}" PARENT_SCOPE)
        return()
    endif()

    set(vs_roots)
    if(DEFINED ENV{ProgramFiles})
        list(APPEND vs_roots "$ENV{ProgramFiles}/Microsoft Visual Studio")
    endif()
    if(DEFINED ENV{ProgramFiles\(x86\)})
        list(APPEND vs_roots "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio")
    endif()

    set(dumpbin_candidates)
    foreach(vs_root IN LISTS vs_roots)
        file(GLOB vs_dumpbin_candidates
            "${vs_root}/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
            "${vs_root}/*/*/VC/Tools/MSVC/*/bin/Hostx64/x86/dumpbin.exe"
            "${vs_root}/*/*/VC/Tools/MSVC/*/bin/Hostx86/x64/dumpbin.exe"
            "${vs_root}/*/*/VC/Tools/MSVC/*/bin/Hostx86/x86/dumpbin.exe"
        )
        list(APPEND dumpbin_candidates ${vs_dumpbin_candidates})
    endforeach()

    if(dumpbin_candidates)
        list(SORT dumpbin_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET dumpbin_candidates 0 nr_dumpbin_executable)
        set(${out_var} "${nr_dumpbin_executable}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(nr_seed_msys2_clang_toolchain_cache_args out_var)
    if(NOT WIN32)
        return()
    endif()

    if(NOT DEFINED CMAKE_CXX_COMPILER OR "${CMAKE_CXX_COMPILER}" STREQUAL "")
        return()
    endif()

    get_filename_component(nr_cxx_compiler_name "${CMAKE_CXX_COMPILER}" NAME)
    if(NOT nr_cxx_compiler_name MATCHES "^(clang\\+\\+|c\\+\\+)\\.exe$")
        return()
    endif()

    get_filename_component(nr_clang_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

    if(NOT DEFINED CMAKE_C_COMPILER OR "${CMAKE_C_COMPILER}" STREQUAL "")
        set(nr_c_compiler_candidate "${nr_clang_bin_dir}/clang.exe")
        if(EXISTS "${nr_c_compiler_candidate}")
            nr_append_cmake_cache_arg(${out_var} CMAKE_C_COMPILER "${nr_c_compiler_candidate}")
        endif()
    endif()

    if(NOT DEFINED CMAKE_RC_COMPILER OR "${CMAKE_RC_COMPILER}" STREQUAL "")
        foreach(nr_rc_candidate IN ITEMS
            "${nr_clang_bin_dir}/llvm-rc.exe"
            "${nr_clang_bin_dir}/windres.exe"
        )
            if(EXISTS "${nr_rc_candidate}")
                nr_append_cmake_cache_arg(${out_var} CMAKE_RC_COMPILER "${nr_rc_candidate}")
                break()
            endif()
        endforeach()
    endif()

    foreach(nr_llvm_tool IN ITEMS
        CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=clang-scan-deps.exe
        CMAKE_AR=llvm-ar.exe
        CMAKE_RANLIB=llvm-ranlib.exe
        CMAKE_OBJCOPY=llvm-objcopy.exe
        CMAKE_STRIP=llvm-strip.exe
    )
        string(REPLACE "=" ";" nr_llvm_tool_parts "${nr_llvm_tool}")
        list(GET nr_llvm_tool_parts 0 nr_llvm_tool_var)
        list(GET nr_llvm_tool_parts 1 nr_llvm_tool_exe)
        if(DEFINED ${nr_llvm_tool_var} AND NOT "${${nr_llvm_tool_var}}" STREQUAL "")
            continue()
        endif()

        set(nr_llvm_tool_candidate "${nr_clang_bin_dir}/${nr_llvm_tool_exe}")
        if(EXISTS "${nr_llvm_tool_candidate}")
            nr_append_cmake_cache_arg(${out_var} "${nr_llvm_tool_var}" "${nr_llvm_tool_candidate}")
        endif()
    endforeach()

    set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()

function(nr_ensure_bundled_slang_package)
    set(one_value_args
        SOURCE_DIR
        BINARY_DIR
        INSTALL_DIR
        CONFIGS
    )
    cmake_parse_arguments(NR_SLANG "" "${one_value_args}" "" ${ARGN})

    if(NOT NR_SLANG_SOURCE_DIR)
        message(FATAL_ERROR "nr_ensure_bundled_slang_package requires SOURCE_DIR.")
    endif()
    if(NOT NR_SLANG_BINARY_DIR)
        message(FATAL_ERROR "nr_ensure_bundled_slang_package requires BINARY_DIR.")
    endif()
    if(NOT NR_SLANG_INSTALL_DIR)
        message(FATAL_ERROR "nr_ensure_bundled_slang_package requires INSTALL_DIR.")
    endif()

    if(NOT EXISTS "${NR_SLANG_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Bundled Slang submodule is missing: '${NR_SLANG_SOURCE_DIR}'.")
    endif()

    set(configure_args
        -S "${NR_SLANG_SOURCE_DIR}"
        -B "${NR_SLANG_BINARY_DIR}"
        -G "${CMAKE_GENERATOR}"
    )
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND configure_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND configure_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()

    nr_append_cmake_cache_arg(configure_args CMAKE_INSTALL_PREFIX "${NR_SLANG_INSTALL_DIR}")
    nr_append_cmake_cache_arg(configure_args CMAKE_EXPORT_COMPILE_COMMANDS OFF)
    nr_append_cmake_cache_arg(
        configure_args
        CMAKE_PROJECT_slang_INCLUDE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/NrSlangPackageProjectHook.cmake"
    )

    if(CMAKE_CONFIGURATION_TYPES)
        nr_append_cmake_cache_arg(configure_args CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}")
        if(CMAKE_DEFAULT_BUILD_TYPE)
            nr_append_cmake_cache_arg(configure_args CMAKE_DEFAULT_BUILD_TYPE "${CMAKE_DEFAULT_BUILD_TYPE}")
        endif()
    else()
        if(CMAKE_BUILD_TYPE)
            nr_append_cmake_cache_arg(configure_args CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
        else()
            nr_append_cmake_cache_arg(configure_args CMAKE_BUILD_TYPE Debug)
        endif()
    endif()

    foreach(cache_var IN ITEMS
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS
        CMAKE_CXX_COMPILER_TARGET
        CMAKE_C_COMPILER_TARGET
        CMAKE_MAKE_PROGRAM
        CMAKE_AR
        CMAKE_RANLIB
        CMAKE_OBJCOPY
        CMAKE_STRIP
        CMAKE_RC_COMPILER
        CMAKE_SYSROOT
        CMAKE_TOOLCHAIN_FILE
        VCPKG_CHAINLOAD_TOOLCHAIN_FILE
        VCPKG_FEATURE_FLAGS
        VCPKG_INSTALL_OPTIONS
        VCPKG_TARGET_TRIPLET
        VCPKG_OVERLAY_TRIPLETS
        VCPKG_INSTALLED_DIR
    )
        nr_append_defined_cmake_cache_arg(configure_args "${cache_var}")
    endforeach()

    nr_seed_msys2_clang_toolchain_cache_args(configure_args)

    if(MSVC AND CMAKE_MSVC_RUNTIME_LIBRARY)
        nr_append_cmake_cache_arg(configure_args CMAKE_MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
    endif()

    set(slang_options
        SLANG_LIB_TYPE=SHARED
        SLANG_ENABLE_CUDA=OFF
        SLANG_ENABLE_OPTIX=OFF
        SLANG_ENABLE_NVAPI=OFF
        SLANG_ENABLE_AFTERMATH=OFF
        SLANG_ENABLE_TESTS=OFF
        SLANG_ENABLE_EXAMPLES=OFF
        SLANG_ENABLE_GFX=OFF
        SLANG_ENABLE_SLANG_RHI=OFF
        SLANG_ENABLE_SLANGRT=OFF
        SLANG_ENABLE_SLANG_GLSLANG=OFF
        SLANG_ENABLE_SLANGD=OFF
        SLANG_ENABLE_SLANGI=OFF
        SLANG_ENABLE_SLANGC=OFF
        SLANG_ENABLE_REPLAYER=OFF
        SLANG_ENABLE_DXIL=OFF
        SLANG_ENABLE_PREBUILT_BINARIES=OFF
        SLANG_EXCLUDE_DAWN=ON
        SLANG_EXCLUDE_TINT=ON
        SLANG_SLANG_LLVM_FLAVOR=DISABLE
        SLANG_ENABLE_RELEASE_DEBUG_INFO=OFF
    )
    foreach(option IN LISTS slang_options)
        list(APPEND configure_args -D "${option}")
    endforeach()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU" AND WIN32)
        foreach(secure_crt_macro IN ITEMS
            HAVE_MEMCPY_S
            HAVE_FOPEN_S
            HAVE_FREAD_S
            HAVE_WCSNLEN_S
            HAVE_STRNLEN_S
            HAVE_SPRINTF_S
            HAVE_SWPRINTF_S
            HAVE_WCSCPY_S
            HAVE_STRCPY_S
            HAVE_WCSNCPY_S
            HAVE_STRNCPY_S
        )
            list(APPEND configure_args -D "${secure_crt_macro}=ON")
        endforeach()
    endif()

    set(slang_env_args)
    if(WIN32)
        nr_find_visual_studio_dumpbin(nr_slang_dumpbin_executable)
        if(nr_slang_dumpbin_executable)
            get_filename_component(nr_slang_dumpbin_dir "${nr_slang_dumpbin_executable}" DIRECTORY)
            set(nr_slang_env_path "${nr_slang_dumpbin_dir};$ENV{PATH}")
            string(REPLACE ";" "\\;" nr_slang_env_path "${nr_slang_env_path}")
            list(APPEND slang_env_args "PATH=${nr_slang_env_path}")
        else()
            message(STATUS "dumpbin.exe was not found; bundled Slang proxy DLL generation may fail.")
        endif()
    endif()

    if(NR_SLANG_CONFIGS)
        set(build_configs ${NR_SLANG_CONFIGS})
    elseif(CMAKE_CONFIGURATION_TYPES)
        set(build_configs ${CMAKE_CONFIGURATION_TYPES})
    elseif(CMAKE_BUILD_TYPE)
        set(build_configs ${CMAKE_BUILD_TYPE})
    else()
        set(build_configs Debug)
    endif()

    find_package(Git QUIET)
    set(slang_source_revision "unknown")
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${NR_SLANG_SOURCE_DIR}" rev-parse HEAD
            RESULT_VARIABLE slang_revision_result
            OUTPUT_VARIABLE slang_source_revision_output
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(slang_revision_result EQUAL 0 AND NOT slang_source_revision_output STREQUAL "")
            set(slang_source_revision "${slang_source_revision_output}")
        endif()
    endif()

    set(package_config_file "${NR_SLANG_INSTALL_DIR}/cmake/slangConfig.cmake")
    set(package_targets_file "${NR_SLANG_INSTALL_DIR}/cmake/slangTargets.cmake")
    set(package_signature_file "${NR_SLANG_INSTALL_DIR}/.nr-bundled-slang-package.signature")
    string(REPLACE ";" "\n" package_signature_input
        "${configure_args};${slang_options};env=${slang_env_args};configs=${build_configs};source=${slang_source_revision};script=6"
    )
    string(SHA256 package_signature "${package_signature_input}")

    if(EXISTS "${package_config_file}" AND EXISTS "${package_targets_file}" AND EXISTS "${package_signature_file}")
        file(READ "${package_signature_file}" existing_package_signature)
        string(STRIP "${existing_package_signature}" existing_package_signature)
        if(existing_package_signature STREQUAL package_signature)
            message(STATUS "Using bundled Slang package at ${NR_SLANG_INSTALL_DIR}")
            return()
        endif()
    endif()

    message(STATUS "Configuring bundled Slang package from ${NR_SLANG_SOURCE_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${slang_env_args} -- ${CMAKE_COMMAND} ${configure_args}
        RESULT_VARIABLE configure_result
        COMMAND_ECHO STDOUT
    )
    if(NOT configure_result EQUAL 0)
        message(FATAL_ERROR "Bundled Slang package configure failed with exit code ${configure_result}.")
    endif()

    foreach(build_config IN LISTS build_configs)
        if(build_config STREQUAL "")
            continue()
        endif()

        message(STATUS "Building bundled Slang package install prerequisites (${build_config})")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ${slang_env_args} -- ${CMAKE_COMMAND}
                --build "${NR_SLANG_BINARY_DIR}"
                --config "${build_config}"
                --target slang-glsl-module
            RESULT_VARIABLE prerequisite_build_result
            COMMAND_ECHO STDOUT
        )
        if(NOT prerequisite_build_result EQUAL 0)
            message(FATAL_ERROR "Bundled Slang package prerequisite build failed for ${build_config} with exit code ${prerequisite_build_result}.")
        endif()

        message(STATUS "Installing bundled Slang package (${build_config})")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E env ${slang_env_args} -- ${CMAKE_COMMAND}
                --build "${NR_SLANG_BINARY_DIR}"
                --config "${build_config}"
                --target install
            RESULT_VARIABLE build_result
            COMMAND_ECHO STDOUT
        )
        if(NOT build_result EQUAL 0)
            message(FATAL_ERROR "Bundled Slang package install failed for ${build_config} with exit code ${build_result}.")
        endif()
    endforeach()

    file(WRITE "${package_signature_file}" "${package_signature}\n")
endfunction()
