cmake_minimum_required(VERSION 4.4)

# Perfetto url: https://ui.perfetto.dev/

# Convert the latest Ninja build recorded in build/llvm/.ninja_log:
#   cmake -P tools/GenerateNinjaTrace.cmake

set(nr_source_dir "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(ABSOLUTE_PATH nr_source_dir NORMALIZE)

function(nr_require_distinct_trace_output output log script)
    if(WIN32)
        string(TOLOWER "${output}" output)
        string(TOLOWER "${log}" log)
        string(TOLOWER "${script}" script)
    endif()
    if(output STREQUAL log)
        message(FATAL_ERROR "Ninja trace output must not overwrite the Ninja log: '${NR_NINJA_TRACE_OUTPUT}'.")
    endif()
    if(output STREQUAL script)
        message(FATAL_ERROR "Ninja trace output must not overwrite ninjatracing: '${NR_NINJA_TRACE_OUTPUT}'.")
    endif()
endfunction()

if(NOT DEFINED NR_NINJA_TRACE_BUILD_DIR)
    set(NR_NINJA_TRACE_BUILD_DIR "${nr_source_dir}/build/llvm")
endif()
cmake_path(
    ABSOLUTE_PATH NR_NINJA_TRACE_BUILD_DIR
    BASE_DIRECTORY "${nr_source_dir}"
    NORMALIZE
)

if(NOT DEFINED NR_NINJA_TRACE_LOG)
    set(NR_NINJA_TRACE_LOG "${NR_NINJA_TRACE_BUILD_DIR}/.ninja_log")
endif()
cmake_path(
    ABSOLUTE_PATH NR_NINJA_TRACE_LOG
    BASE_DIRECTORY "${nr_source_dir}"
    NORMALIZE
)

if(NOT DEFINED NR_NINJA_TRACE_OUTPUT)
    set(NR_NINJA_TRACE_OUTPUT "${NR_NINJA_TRACE_BUILD_DIR}/ninja-trace.json")
endif()
cmake_path(
    ABSOLUTE_PATH NR_NINJA_TRACE_OUTPUT
    BASE_DIRECTORY "${nr_source_dir}"
    NORMALIZE
)

if(NOT DEFINED NR_NINJA_TRACE_SCRIPT)
    set(NR_NINJA_TRACE_SCRIPT "${nr_source_dir}/tools/ninjatracing/ninjatracing")
endif()
cmake_path(
    ABSOLUTE_PATH NR_NINJA_TRACE_SCRIPT
    BASE_DIRECTORY "${nr_source_dir}"
    NORMALIZE
)

if(NOT EXISTS "${NR_NINJA_TRACE_SCRIPT}" OR IS_DIRECTORY "${NR_NINJA_TRACE_SCRIPT}")
    message(FATAL_ERROR
        "ninjatracing was not found at '${NR_NINJA_TRACE_SCRIPT}'."
    )
endif()
file(REAL_PATH "${NR_NINJA_TRACE_SCRIPT}" nr_ninja_trace_script)

if(NOT EXISTS "${NR_NINJA_TRACE_LOG}" OR IS_DIRECTORY "${NR_NINJA_TRACE_LOG}")
    message(FATAL_ERROR
        "Ninja log was not found at '${NR_NINJA_TRACE_LOG}'. Build the Ninja "
        "preset first or set NR_NINJA_TRACE_LOG explicitly."
    )
endif()
file(REAL_PATH "${NR_NINJA_TRACE_LOG}" nr_ninja_trace_log)

if(EXISTS "${NR_NINJA_TRACE_OUTPUT}" AND IS_DIRECTORY "${NR_NINJA_TRACE_OUTPUT}")
    message(FATAL_ERROR "Ninja trace output is a directory: '${NR_NINJA_TRACE_OUTPUT}'.")
endif()
nr_require_distinct_trace_output(
    "${NR_NINJA_TRACE_OUTPUT}"
    "${nr_ninja_trace_log}"
    "${nr_ninja_trace_script}"
)

file(READ "${nr_ninja_trace_log}" nr_ninja_log_header LIMIT 32)
if(NOT nr_ninja_log_header MATCHES "^# ninja log v[0-9]+")
    message(FATAL_ERROR
        "'${nr_ninja_trace_log}' is not a recognized Ninja log."
    )
endif()

find_package(Python3 3.7 REQUIRED COMPONENTS Interpreter)

set(nr_ninjatracing_args)
if(NR_NINJA_TRACE_SHOW_ALL)
    list(APPEND nr_ninjatracing_args --showall)
endif()
if(NR_NINJA_TRACE_EMBED_TIME_TRACE)
    list(APPEND nr_ninjatracing_args --embed-time-trace)
endif()
if(DEFINED NR_NINJA_TRACE_GRANULARITY)
    if(NOT NR_NINJA_TRACE_GRANULARITY MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "NR_NINJA_TRACE_GRANULARITY must be a non-negative integer."
        )
    endif()
    list(APPEND nr_ninjatracing_args
        "--granularity=${NR_NINJA_TRACE_GRANULARITY}"
    )
endif()
list(APPEND nr_ninjatracing_args "${nr_ninja_trace_log}")

get_filename_component(nr_trace_output_dir "${NR_NINJA_TRACE_OUTPUT}" DIRECTORY)
get_filename_component(nr_trace_output_name "${NR_NINJA_TRACE_OUTPUT}" NAME)
file(MAKE_DIRECTORY "${nr_trace_output_dir}")
file(REAL_PATH "${nr_trace_output_dir}" nr_trace_output_canonical_dir)
set(nr_trace_publish_output "${nr_trace_output_canonical_dir}")
cmake_path(APPEND nr_trace_publish_output "${nr_trace_output_name}")
if(EXISTS "${NR_NINJA_TRACE_OUTPUT}")
    file(REAL_PATH "${NR_NINJA_TRACE_OUTPUT}" nr_trace_output_collision_path)
else()
    set(nr_trace_output_collision_path "${nr_trace_publish_output}")
endif()
nr_require_distinct_trace_output(
    "${nr_trace_output_collision_path}"
    "${nr_ninja_trace_log}"
    "${nr_ninja_trace_script}"
)

while(TRUE)
    string(RANDOM LENGTH 24 ALPHABET 0123456789abcdef nr_trace_temporary_token)
    set(nr_trace_temporary_output
        "${nr_trace_output_canonical_dir}/.${nr_trace_output_name}.${nr_trace_temporary_token}.tmp"
    )
    if(NOT EXISTS "${nr_trace_temporary_output}")
        break()
    endif()
endwhile()

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${nr_ninja_trace_script}"
        ${nr_ninjatracing_args}
    WORKING_DIRECTORY "${nr_source_dir}"
    RESULT_VARIABLE nr_ninjatracing_result
    ERROR_VARIABLE nr_ninjatracing_error
    OUTPUT_FILE "${nr_trace_temporary_output}"
    ENCODING UTF-8
)

if(NOT "${nr_ninjatracing_result}" STREQUAL "0")
    file(REMOVE "${nr_trace_temporary_output}")
    string(STRIP "${nr_ninjatracing_error}" nr_ninjatracing_error)
    message(FATAL_ERROR
        "ninjatracing failed with result '${nr_ninjatracing_result}': "
        "${nr_ninjatracing_error}"
    )
endif()

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        -c
        "import json, pathlib, sys; json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))"
        "${nr_trace_temporary_output}"
    RESULT_VARIABLE nr_trace_validation_result
    ERROR_VARIABLE nr_trace_validation_error
    ENCODING UTF-8
)

if(NOT "${nr_trace_validation_result}" STREQUAL "0")
    file(REMOVE "${nr_trace_temporary_output}")
    string(STRIP "${nr_trace_validation_error}" nr_trace_validation_error)
    message(FATAL_ERROR
        "ninjatracing produced invalid JSON: ${nr_trace_validation_error}"
    )
endif()

file(
    RENAME
    "${nr_trace_temporary_output}"
    "${nr_trace_publish_output}"
    RESULT nr_trace_rename_result
)
if(NOT "${nr_trace_rename_result}" STREQUAL "0")
    file(REMOVE "${nr_trace_temporary_output}")
    message(FATAL_ERROR
        "Failed to publish '${nr_trace_publish_output}': "
        "${nr_trace_rename_result}"
    )
endif()

file(SIZE "${nr_trace_publish_output}" nr_trace_size)
message(STATUS
    "Generated Ninja trace '${nr_trace_publish_output}' "
    "(${nr_trace_size} bytes) with '${Python3_EXECUTABLE}'."
)
