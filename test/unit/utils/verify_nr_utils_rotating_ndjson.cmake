if(NOT DEFINED NR_ROTATING_NDJSON_PROBE)
    message(FATAL_ERROR "NR_ROTATING_NDJSON_PROBE is required.")
endif()

if(NOT DEFINED NR_ROTATING_NDJSON_OUTPUT_DIR)
    message(FATAL_ERROR "NR_ROTATING_NDJSON_OUTPUT_DIR is required.")
endif()

file(REMOVE_RECURSE "${NR_ROTATING_NDJSON_OUTPUT_DIR}")

execute_process(
    COMMAND "${NR_ROTATING_NDJSON_PROBE}" "${NR_ROTATING_NDJSON_OUTPUT_DIR}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr
)

if(NOT probe_result EQUAL 0)
    message(
        FATAL_ERROR
        "Rotating NDJSON probe exited with '${probe_result}'.\n"
        "stdout:\n${probe_stdout}\n"
        "stderr:\n${probe_stderr}"
    )
endif()

set(probe_output "${probe_stdout}${probe_stderr}")

foreach(suppressed_marker
        "NR_NDJSON_INFO_MUST_NOT_REACH_CMD"
        "NR_NDJSON_OPTION_MUST_NOT_REACH_CMD")
    string(FIND "${probe_output}" "${suppressed_marker}" match_index)
    if(NOT match_index EQUAL -1)
        message(
            FATAL_ERROR
            "Suppressed marker '${suppressed_marker}' reached the command stream:\n${probe_output}"
        )
    endif()
endforeach()

foreach(visible_marker
        "NR_NDJSON_WARNING_MUST_REACH_CMD"
        "NR_NDJSON_ERROR_MUST_REACH_CMD")
    string(FIND "${probe_output}" "${visible_marker}" match_index)
    if(match_index EQUAL -1)
        message(
            FATAL_ERROR
            "Visible marker '${visible_marker}' was missing from the command stream:\n${probe_output}"
        )
    endif()
endforeach()

set(expected_session_id "nr-utils-rotating-ndjson-test")
set(total_engine_records 0)
set(total_option_records 0)

foreach(stream IN ITEMS engine options)
    set(active_path "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.ndjson")
    set(first_backup_path "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.1.ndjson")
    if(NOT EXISTS "${active_path}")
        message(FATAL_ERROR "Active ${stream} NDJSON file was not created: '${active_path}'.")
    endif()
    if(NOT EXISTS "${first_backup_path}")
        message(FATAL_ERROR "Rotated ${stream} NDJSON backup was not created: '${first_backup_path}'.")
    endif()

    file(
        GLOB stream_paths
        LIST_DIRECTORIES false
        "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.ndjson"
        "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.*.ndjson"
    )
    list(SORT stream_paths)

    foreach(stream_path IN LISTS stream_paths)
        file(STRINGS "${stream_path}" physical_lines ENCODING UTF-8)
        if(NOT physical_lines)
            message(FATAL_ERROR "NDJSON file was empty: '${stream_path}'.")
        endif()

        set(line_index 0)
        foreach(physical_line IN LISTS physical_lines)
            string(
                JSON record_schema
                ERROR_VARIABLE json_error
                GET "${physical_line}" schema
            )
            if(NOT json_error STREQUAL "NOTFOUND")
                message(
                    FATAL_ERROR
                    "Invalid JSON line in '${stream_path}': ${json_error}\n${physical_line}"
                )
            endif()

            if(line_index EQUAL 0)
                if(NOT record_schema STREQUAL "NR_LOG_SESSION_V1")
                    message(
                        FATAL_ERROR
                        "First line of '${stream_path}' used schema '${record_schema}' instead of NR_LOG_SESSION_V1."
                    )
                endif()
                string(JSON record_session_id GET "${physical_line}" session_id)
                string(JSON record_stream GET "${physical_line}" stream)
                if(NOT record_session_id STREQUAL expected_session_id)
                    message(
                        FATAL_ERROR
                        "Session id '${record_session_id}' in '${stream_path}' did not match '${expected_session_id}'."
                    )
                endif()
                if(NOT record_stream STREQUAL stream)
                    message(
                        FATAL_ERROR
                        "Session stream '${record_stream}' in '${stream_path}' did not match '${stream}'."
                    )
                endif()
            elseif(stream STREQUAL "engine")
                if(NOT record_schema STREQUAL "NR_LOG_V1")
                    message(
                        FATAL_ERROR
                        "Engine file '${stream_path}' contained routed schema '${record_schema}'."
                    )
                endif()
                string(JSON record_channel GET "${physical_line}" channel)
                if(NOT record_channel STREQUAL "LOG")
                    message(
                        FATAL_ERROR
                        "Engine file '${stream_path}' contained channel '${record_channel}' instead of LOG."
                    )
                endif()
                math(EXPR total_engine_records "${total_engine_records} + 1")
            else()
                if(NOT record_schema STREQUAL "NR_OPTION_V1")
                    message(
                        FATAL_ERROR
                        "Option file '${stream_path}' contained routed schema '${record_schema}'."
                    )
                endif()
                string(JSON record_marker GET "${physical_line}" marker)
                if(NOT record_marker STREQUAL "NR_NDJSON_OPTION_MUST_NOT_REACH_CMD")
                    message(
                        FATAL_ERROR
                        "Option file '${stream_path}' contained unexpected marker '${record_marker}'."
                    )
                endif()
                math(EXPR total_option_records "${total_option_records} + 1")
            endif()

            math(EXPR line_index "${line_index} + 1")
        endforeach()
    endforeach()
endforeach()

if(total_engine_records LESS 3)
    message(FATAL_ERROR "Expected at least three persisted engine records, found ${total_engine_records}.")
endif()

if(NOT total_option_records EQUAL 12)
    message(FATAL_ERROR "Expected 12 persisted option records, found ${total_option_records}.")
endif()

set(fatal_output_dir "${NR_ROTATING_NDJSON_OUTPUT_DIR}-fatal")
file(REMOVE_RECURSE "${fatal_output_dir}")
execute_process(
    COMMAND "${NR_ROTATING_NDJSON_PROBE}" "${fatal_output_dir}" --fatal
    RESULT_VARIABLE fatal_probe_result
    OUTPUT_VARIABLE fatal_probe_stdout
    ERROR_VARIABLE fatal_probe_stderr
)

if(NOT fatal_probe_result EQUAL 1)
    message(
        FATAL_ERROR
        "Fatal rotating NDJSON probe exited with '${fatal_probe_result}' instead of 1.\n"
        "stdout:\n${fatal_probe_stdout}\n"
        "stderr:\n${fatal_probe_stderr}"
    )
endif()

set(fatal_engine_path "${fatal_output_dir}/engine.ndjson")
if(NOT EXISTS "${fatal_engine_path}")
    message(FATAL_ERROR "Fatal probe did not create '${fatal_engine_path}'.")
endif()
file(STRINGS "${fatal_engine_path}" fatal_engine_lines ENCODING UTF-8)
set(fatal_marker_found FALSE)
foreach(fatal_engine_line IN LISTS fatal_engine_lines)
    string(
        JSON fatal_record_schema
        ERROR_VARIABLE fatal_json_error
        GET "${fatal_engine_line}" schema
    )
    if(NOT fatal_json_error STREQUAL "NOTFOUND")
        message(
            FATAL_ERROR
            "Invalid JSON line in '${fatal_engine_path}': ${fatal_json_error}\n${fatal_engine_line}"
        )
    endif()
    string(
        JSON fatal_record_message
        ERROR_VARIABLE fatal_message_error
        GET "${fatal_engine_line}" message
    )
    if(fatal_message_error STREQUAL "NOTFOUND"
       AND fatal_record_message STREQUAL "NR_NDJSON_FATAL_MUST_FLUSH")
        set(fatal_marker_found TRUE)
    endif()
endforeach()
if(NOT fatal_marker_found)
    message(
        FATAL_ERROR
        "Fatal record was not flushed before process exit: '${fatal_engine_path}'."
    )
endif()

if(EXISTS "${fatal_output_dir}/.active-viewer")
    message(FATAL_ERROR "Fatal shutdown left the NDJSON viewer lease behind.")
endif()

execute_process(
    COMMAND "${NR_ROTATING_NDJSON_PROBE}" "${NR_ROTATING_NDJSON_OUTPUT_DIR}" --prune
    RESULT_VARIABLE prune_probe_result
    OUTPUT_VARIABLE prune_probe_stdout
    ERROR_VARIABLE prune_probe_stderr
)
if(NOT prune_probe_result EQUAL 0)
    message(
        FATAL_ERROR
        "Retention prune probe exited with '${prune_probe_result}'.\n"
        "stdout:\n${prune_probe_stdout}\n"
        "stderr:\n${prune_probe_stderr}"
    )
endif()

foreach(stream IN ITEMS engine options)
    if(NOT EXISTS "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.1.ndjson")
        message(FATAL_ERROR "Retention prune probe did not preserve '${stream}.1.ndjson'.")
    endif()
    foreach(excess_index RANGE 5 16)
        if(EXISTS "${NR_ROTATING_NDJSON_OUTPUT_DIR}/${stream}.${excess_index}.ndjson")
            message(
                FATAL_ERROR
                "Retention prune probe left excess '${stream}.${excess_index}.ndjson'."
            )
        endif()
    endforeach()
endforeach()

if(EXISTS "${NR_ROTATING_NDJSON_OUTPUT_DIR}/.active-viewer")
    message(FATAL_ERROR "Orderly shutdown left the NDJSON viewer lease behind.")
endif()

set(lease_conflict_dir "${NR_ROTATING_NDJSON_OUTPUT_DIR}-lease-conflict")
file(REMOVE_RECURSE "${lease_conflict_dir}")
file(MAKE_DIRECTORY "${lease_conflict_dir}/.active-viewer")
execute_process(
    COMMAND "${NR_ROTATING_NDJSON_PROBE}" "${lease_conflict_dir}"
    RESULT_VARIABLE lease_conflict_result
    OUTPUT_VARIABLE lease_conflict_stdout
    ERROR_VARIABLE lease_conflict_stderr
)
if(NOT lease_conflict_result EQUAL 3)
    message(
        FATAL_ERROR
        "Lease-conflict probe exited with '${lease_conflict_result}' instead of 3.\n"
        "stdout:\n${lease_conflict_stdout}\n"
        "stderr:\n${lease_conflict_stderr}"
    )
endif()
if(EXISTS "${lease_conflict_dir}/engine.ndjson" OR
   EXISTS "${lease_conflict_dir}/options.ndjson")
    message(FATAL_ERROR "Lease-conflict probe touched active NDJSON files.")
endif()
file(REMOVE_RECURSE "${lease_conflict_dir}")
