# GenerateStaticUtilsConstantsConfig.cmake - Generate config macros for staticUtilsConstants.ixx.

function(nr_read_cache_entry name out_var)
    if(NOT EXISTS "${NR_CACHE_FILE}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(STRINGS "${NR_CACHE_FILE}" matches REGEX "^${name}(:[^=]*)?=")
    if(NOT matches)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    list(GET matches 0 entry)
    string(REGEX REPLACE "^[^=]*=" "" value "${entry}")
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(nr_escape_cpp_string input out_var)
    string(REPLACE "\\" "\\\\" escaped "${input}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    set(${out_var} "${escaped}" PARENT_SCOPE)
endfunction()

function(nr_read_required_positive_u32_cache_entry name out_var)
    nr_read_cache_entry("${name}" value)
    if(value STREQUAL "")
        message(FATAL_ERROR "${name} must be defined by the top-level CMake configuration.")
    endif()

    if(NOT value MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${name} must be a positive integer. Found '${value}'.")
    endif()

    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(nr_read_bool_cache_entry name default_value out_var)
    nr_read_cache_entry("${name}" value)
    if(value STREQUAL "")
        set(value "${default_value}")
    endif()

    string(TOUPPER "${value}" value_upper)
    if(value_upper MATCHES "^(1|ON|TRUE|YES)$")
        set(${out_var} "true" PARENT_SCOPE)
    elseif(value_upper MATCHES "^(0|OFF|FALSE|NO)$")
        set(${out_var} "false" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "${name} must be a boolean value. Found '${value}'.")
    endif()
endfunction()

function(nr_read_shader_optimization_level out_var)
    nr_read_cache_entry("NR_SHADER_OPTIMIZATION_LEVEL" level)
    if(level STREQUAL "")
        set(level "MAXIMAL")
    endif()

    string(TOUPPER "${level}" level)
    if(level STREQUAL "NONE")
        set(value "0")
    elseif(level STREQUAL "DEFAULT")
        set(value "1")
    elseif(level STREQUAL "HIGH")
        set(value "2")
    elseif(level STREQUAL "MAXIMAL")
        set(value "3")
    else()
        message(FATAL_ERROR "NR_SHADER_OPTIMIZATION_LEVEL must be NONE, DEFAULT, HIGH, or MAXIMAL. Found '${level}'.")
    endif()

    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(nr_read_shader_debug_info_level out_var)
    nr_read_cache_entry("NR_SHADER_DEBUG_INFO_LEVEL" level)
    if(level STREQUAL "")
        set(level "MAXIMAL")
    endif()

    string(TOUPPER "${level}" level)
    if(level STREQUAL "NONE")
        set(value "0")
    elseif(level STREQUAL "MINIMAL")
        set(value "1")
    elseif(level STREQUAL "STANDARD")
        set(value "2")
    elseif(level STREQUAL "MAXIMAL")
        set(value "3")
    else()
        message(FATAL_ERROR "NR_SHADER_DEBUG_INFO_LEVEL must be NONE, MINIMAL, STANDARD, or MAXIMAL. Found '${level}'.")
    endif()

    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED NR_OUTPUT_FILE)
    message(FATAL_ERROR "NR_OUTPUT_FILE must be defined.")
endif()

if(NOT DEFINED NR_BUILD_CONFIG OR NR_BUILD_CONFIG STREQUAL "")
    set(NR_BUILD_CONFIG "Debug")
endif()

if(NOT DEFINED NR_BINARY_DIR OR NR_BINARY_DIR STREQUAL "")
    get_filename_component(NR_BINARY_DIR "${NR_OUTPUT_FILE}" DIRECTORY)
endif()

if(NOT DEFINED NR_SOURCE_DIR OR NR_SOURCE_DIR STREQUAL "")
    get_filename_component(NR_SOURCE_DIR "${NR_BINARY_DIR}" DIRECTORY)
endif()

nr_read_cache_entry("NR_LOG_LEVEL" nr_log_level)
if(nr_log_level STREQUAL "")
    set(nr_log_level "INFO")
endif()
string(TOUPPER "${nr_log_level}" nr_log_level)

if(nr_log_level STREQUAL "INFO")
    set(nr_global_log_level "LogLevel::info")
elseif(nr_log_level STREQUAL "WARNING")
    set(nr_global_log_level "LogLevel::warning")
elseif(nr_log_level STREQUAL "ERROR")
    set(nr_global_log_level "LogLevel::error")
else()
    message(FATAL_ERROR "NR_LOG_LEVEL must be INFO, WARNING, or ERROR. Found '${nr_log_level}'.")
endif()

if(NR_BUILD_CONFIG STREQUAL "Debug")
    set(nr_is_debug_mode "true")
else()
    set(nr_is_debug_mode "false")
endif()

include(ProcessorCount)
ProcessorCount(nr_max_threads)
if(nr_max_threads EQUAL 0)
    set(nr_max_threads 4)
endif()

nr_read_required_positive_u32_cache_entry("NR_MAX_FRAME_IN_FLIGHT" nr_max_frame_in_flight)
nr_read_required_positive_u32_cache_entry("NR_STATISTICS_SAMPLE_FRAME_COUNT" nr_statistics_sample_frame_count)
nr_read_shader_optimization_level(nr_shader_optimization_level)
nr_read_shader_debug_info_level(nr_shader_debug_info_level)
nr_read_bool_cache_entry("NR_SHADER_ENABLE_RICH_DIAGNOSTICS" "ON" nr_shader_enable_rich_diagnostics)
nr_read_bool_cache_entry("NR_SHADER_DUMP_REPRO_ON_ERROR" "ON" nr_shader_dump_repro_on_error)
nr_read_bool_cache_entry("NR_GPU_DEBUG_NAMES_ENABLED" "ON" nr_gpu_debug_names_enabled)

nr_read_cache_entry("NR_SHADER_WARNINGS_AS_ERRORS" nr_shader_warnings_as_errors)
nr_escape_cpp_string("${nr_shader_warnings_as_errors}" nr_shader_warnings_as_errors)

nr_read_cache_entry("NR_SHADER_CACHE_DIR" nr_shader_cache_dir)
if(nr_shader_cache_dir STREQUAL "")
    set(nr_shader_cache_dir "${NR_BINARY_DIR}/shader_cache/${NR_BUILD_CONFIG}")
endif()
string(REPLACE "$<CONFIG>" "${NR_BUILD_CONFIG}" nr_shader_cache_dir "${nr_shader_cache_dir}")
file(TO_CMAKE_PATH "${nr_shader_cache_dir}" nr_shader_cache_dir)
nr_escape_cpp_string("${nr_shader_cache_dir}" nr_shader_cache_dir)

nr_read_cache_entry("NR_SHADER_ROOT_DIR" nr_shader_root_dir)
if(nr_shader_root_dir STREQUAL "")
    set(nr_shader_root_dir "${NR_SOURCE_DIR}/shader")
endif()
string(REPLACE "$<CONFIG>" "${NR_BUILD_CONFIG}" nr_shader_root_dir "${nr_shader_root_dir}")
file(TO_CMAKE_PATH "${nr_shader_root_dir}" nr_shader_root_dir)
nr_escape_cpp_string("${nr_shader_root_dir}" nr_shader_root_dir)

file(TO_CMAKE_PATH "${NR_SOURCE_DIR}" nr_project_root_dir)
nr_escape_cpp_string("${nr_project_root_dir}" nr_project_root_dir)

set(content "#pragma once

#define NR_IS_DEBUG_MODE ${nr_is_debug_mode}
#define NR_MAX_THREADS ${nr_max_threads}u
#define NR_MAX_FRAME_IN_FLIGHT ${nr_max_frame_in_flight}u
#define NR_STATISTICS_SAMPLE_FRAME_COUNT ${nr_statistics_sample_frame_count}u
#define NR_GLOBAL_LOG_LEVEL ${nr_global_log_level}
#define NR_PROJECT_ROOT \"${nr_project_root_dir}\"
#define NR_SHADER_CACHE_ROOT \"${nr_shader_cache_dir}\"
#define NR_SHADER_ROOT \"${nr_shader_root_dir}\"
#define NR_SHADER_OPTIMIZATION_LEVEL ${nr_shader_optimization_level}
#define NR_SHADER_DEBUG_INFO_LEVEL ${nr_shader_debug_info_level}
#define NR_SHADER_ENABLE_RICH_DIAGNOSTICS ${nr_shader_enable_rich_diagnostics}
#define NR_SHADER_DUMP_REPRO_ON_ERROR ${nr_shader_dump_repro_on_error}
#define NR_SHADER_WARNINGS_AS_ERRORS \"${nr_shader_warnings_as_errors}\"
#define NR_GPU_DEBUG_NAMES_ENABLED ${nr_gpu_debug_names_enabled}
")

get_filename_component(output_dir "${NR_OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

if(EXISTS "${NR_OUTPUT_FILE}")
    file(READ "${NR_OUTPUT_FILE}" old_content)
    if(old_content STREQUAL content)
        return()
    endif()
endif()

file(WRITE "${NR_OUTPUT_FILE}" "${content}")
