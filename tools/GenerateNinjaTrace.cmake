cmake_minimum_required(VERSION 4.4)

# Perfetto url: https://ui.perfetto.dev/

# Convert the latest Ninja build recorded in build/llvm/.ninja_log:
#   cmake -P tools/GenerateNinjaTrace.cmake

set(nr_source_dir "${CMAKE_CURRENT_LIST_DIR}/..")
cmake_path(ABSOLUTE_PATH nr_source_dir NORMALIZE)

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

if(NOT EXISTS "${NR_NINJA_TRACE_SCRIPT}")
    message(FATAL_ERROR
        "ninjatracing was not found at '${NR_NINJA_TRACE_SCRIPT}'."
    )
endif()

if(NOT EXISTS "${NR_NINJA_TRACE_LOG}")
    message(FATAL_ERROR
        "Ninja log was not found at '${NR_NINJA_TRACE_LOG}'. Build the Ninja "
        "preset first or set NR_NINJA_TRACE_LOG explicitly."
    )
endif()

file(READ "${NR_NINJA_TRACE_LOG}" nr_ninja_log_header LIMIT 32)
if(NOT nr_ninja_log_header MATCHES "^# ninja log v[0-9]+")
    message(FATAL_ERROR
        "'${NR_NINJA_TRACE_LOG}' is not a recognized Ninja log."
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
list(APPEND nr_ninjatracing_args "${NR_NINJA_TRACE_LOG}")

get_filename_component(nr_trace_output_dir "${NR_NINJA_TRACE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${nr_trace_output_dir}")

set(nr_trace_temporary_output "${NR_NINJA_TRACE_OUTPUT}.tmp")
file(REMOVE "${nr_trace_temporary_output}")

execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${NR_NINJA_TRACE_SCRIPT}"
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
    "${NR_NINJA_TRACE_OUTPUT}"
    RESULT nr_trace_rename_result
)
if(NOT "${nr_trace_rename_result}" STREQUAL "0")
    file(REMOVE "${nr_trace_temporary_output}")
    message(FATAL_ERROR
        "Failed to publish '${NR_NINJA_TRACE_OUTPUT}': "
        "${nr_trace_rename_result}"
    )
endif()

file(SIZE "${NR_NINJA_TRACE_OUTPUT}" nr_trace_size)
message(STATUS
    "Generated Ninja trace '${NR_NINJA_TRACE_OUTPUT}' "
    "(${nr_trace_size} bytes) with '${Python3_EXECUTABLE}'."
)
