cmake_minimum_required(VERSION 4.4)

# Compile one or more single-entry shaders through the same ShaderService batch path used by the renderer:
#   cmake "-DNR_SHADER_FILE=renderer/embeddedTriangle/vertex.slang" -P tools/CheckSlangShader.cmake
#   cmake "-DNR_SHADER_FILES=renderer/embeddedTriangle/vertex.slang;renderer/embeddedTriangle/fragment.slang" -P tools/CheckSlangShader.cmake

set(nr_source_dir "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(ABSOLUTE_PATH nr_source_dir NORMALIZE)

if(DEFINED NR_SHADER_FILE AND
   NOT "${NR_SHADER_FILE}" STREQUAL "" AND
   DEFINED NR_SHADER_FILES AND
   NOT "${NR_SHADER_FILES}" STREQUAL "")
    message(FATAL_ERROR
        "Set either NR_SHADER_FILE or NR_SHADER_FILES, not both."
    )
endif()

if(DEFINED NR_SHADER_FILES AND NOT "${NR_SHADER_FILES}" STREQUAL "")
    set(nr_shader_inputs ${NR_SHADER_FILES})
elseif(DEFINED NR_SHADER_FILE AND NOT "${NR_SHADER_FILE}" STREQUAL "")
    set(nr_shader_inputs "${NR_SHADER_FILE}")
else()
    message(FATAL_ERROR
        "NR_SHADER_FILE or NR_SHADER_FILES is required and must name Slang files under the configured shader root."
    )
endif()
list(FILTER nr_shader_inputs EXCLUDE REGEX "^$")
if(NOT nr_shader_inputs)
    message(FATAL_ERROR "The shader compile batch is empty.")
endif()

if(NOT DEFINED NR_SHADER_CHECK_BUILD_DIR)
    set(NR_SHADER_CHECK_BUILD_DIR "${nr_source_dir}/build/llvm")
endif()
cmake_path(
    ABSOLUTE_PATH NR_SHADER_CHECK_BUILD_DIR
    BASE_DIRECTORY "${nr_source_dir}"
    NORMALIZE
)

if(NOT DEFINED NR_SHADER_CHECK_CONFIG)
    set(NR_SHADER_CHECK_CONFIG "Debug")
endif()
if(NOT NR_SHADER_CHECK_CONFIG MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR
        "NR_SHADER_CHECK_CONFIG contains unsupported characters: '${NR_SHADER_CHECK_CONFIG}'."
    )
endif()

if(NOT EXISTS "${NR_SHADER_CHECK_BUILD_DIR}/CMakeCache.txt")
    message(FATAL_ERROR
        "No configured CMake build was found at '${NR_SHADER_CHECK_BUILD_DIR}'. "
        "Run 'cmake --preset llvm' first or set NR_SHADER_CHECK_BUILD_DIR."
    )
endif()

load_cache(
    "${NR_SHADER_CHECK_BUILD_DIR}"
    READ_WITH_PREFIX nr_shader_check_cache_
    NR_SHADER_ROOT_DIR
)
if(NOT DEFINED nr_shader_check_cache_NR_SHADER_ROOT_DIR OR
   nr_shader_check_cache_NR_SHADER_ROOT_DIR STREQUAL "")
    message(FATAL_ERROR
        "NR_SHADER_ROOT_DIR was not found in '${NR_SHADER_CHECK_BUILD_DIR}/CMakeCache.txt'."
    )
endif()

set(nr_shader_root "${nr_shader_check_cache_NR_SHADER_ROOT_DIR}")
cmake_path(ABSOLUTE_PATH nr_shader_root NORMALIZE)
if(NOT IS_DIRECTORY "${nr_shader_root}")
    message(FATAL_ERROR
        "The configured shader root does not exist: '${nr_shader_root}'."
    )
endif()
file(REAL_PATH "${nr_shader_root}" nr_shader_root)

set(nr_shader_module_paths)
foreach(nr_shader_input IN LISTS nr_shader_inputs)
    set(nr_shader_file "${nr_shader_input}")
    cmake_path(IS_ABSOLUTE nr_shader_file nr_shader_file_is_absolute)
    if(NOT nr_shader_file_is_absolute)
        cmake_path(
            ABSOLUTE_PATH nr_shader_file
            BASE_DIRECTORY "${nr_shader_root}"
            NORMALIZE
        )
    else()
        cmake_path(NORMAL_PATH nr_shader_file)
    endif()

    if(NOT EXISTS "${nr_shader_file}")
        get_filename_component(nr_missing_shader_extension "${nr_shader_file}" LAST_EXT)
        if(nr_missing_shader_extension STREQUAL "" AND
           EXISTS "${nr_shader_file}.slang")
            string(APPEND nr_shader_file ".slang")
        endif()
    endif()

    if(NOT EXISTS "${nr_shader_file}")
        message(FATAL_ERROR "Shader file was not found: '${nr_shader_file}'.")
    endif()
    if(IS_DIRECTORY "${nr_shader_file}")
        message(FATAL_ERROR "Shader path names a directory: '${nr_shader_file}'.")
    endif()
    file(REAL_PATH "${nr_shader_file}" nr_shader_file)

    get_filename_component(nr_shader_extension "${nr_shader_file}" LAST_EXT)
    string(TOLOWER "${nr_shader_extension}" nr_shader_extension)
    if(NOT nr_shader_extension STREQUAL ".slang")
        message(FATAL_ERROR "Shader file must use the .slang extension: '${nr_shader_file}'.")
    endif()

    cmake_path(
        RELATIVE_PATH nr_shader_file
        BASE_DIRECTORY "${nr_shader_root}"
        OUTPUT_VARIABLE nr_shader_module_path
    )
    cmake_path(IS_ABSOLUTE nr_shader_module_path nr_shader_module_path_is_absolute)
    if(nr_shader_module_path_is_absolute OR
       nr_shader_module_path MATCHES "^\\.\\.(/|$)")
        message(FATAL_ERROR
            "Shader file must be inside the configured shader root '${nr_shader_root}': "
            "'${nr_shader_file}'."
        )
    endif()
    list(APPEND nr_shader_module_paths "${nr_shader_module_path}")
endforeach()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        --build "${NR_SHADER_CHECK_BUILD_DIR}"
        --config "${NR_SHADER_CHECK_CONFIG}"
        --target nr_shader_compile_check
    WORKING_DIRECTORY "${nr_source_dir}"
    RESULT_VARIABLE nr_shader_check_build_result
)
if(NOT nr_shader_check_build_result STREQUAL "0")
    message(FATAL_ERROR
        "Failed to build nr_shader_compile_check for '${NR_SHADER_CHECK_CONFIG}' "
        "with result '${nr_shader_check_build_result}'."
    )
endif()

set(
    nr_shader_check_manifest
    "${NR_SHADER_CHECK_BUILD_DIR}/tools/nrShaderCompileCheck-${NR_SHADER_CHECK_CONFIG}.cmake"
)
if(NOT EXISTS "${nr_shader_check_manifest}")
    message(FATAL_ERROR
        "The shader checker executable manifest was not generated: "
        "'${nr_shader_check_manifest}'."
    )
endif()
include("${nr_shader_check_manifest}")

if(NOT DEFINED NR_SHADER_COMPILE_CHECK_EXECUTABLE OR
   NOT EXISTS "${NR_SHADER_COMPILE_CHECK_EXECUTABLE}")
    message(FATAL_ERROR
        "The shader checker executable was not found: "
        "'${NR_SHADER_COMPILE_CHECK_EXECUTABLE}'."
    )
endif()

set(
    nr_shader_check_working_dir
    "${NR_SHADER_CHECK_BUILD_DIR}/tools/shader-check/${NR_SHADER_CHECK_CONFIG}"
)
file(MAKE_DIRECTORY "${nr_shader_check_working_dir}")

execute_process(
    COMMAND
        "${NR_SHADER_COMPILE_CHECK_EXECUTABLE}"
        ${nr_shader_module_paths}
    WORKING_DIRECTORY "${nr_shader_check_working_dir}"
    RESULT_VARIABLE nr_shader_check_result
)
if(NOT nr_shader_check_result STREQUAL "0")
    string(JOIN ", " nr_shader_module_list ${nr_shader_module_paths})
    message(FATAL_ERROR
        "Shader batch compilation failed for '${nr_shader_module_list}' "
        "with result '${nr_shader_check_result}'."
    )
endif()

list(LENGTH nr_shader_module_paths nr_shader_module_count)
message(STATUS
    "${nr_shader_module_count} single-entry shader(s) passed the runtime-equivalent Slang batch compile check."
)
