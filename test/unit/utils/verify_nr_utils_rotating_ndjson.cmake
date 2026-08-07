if(NOT DEFINED NR_ROTATING_NDJSON_PROBE)
    message(FATAL_ERROR "NR_ROTATING_NDJSON_PROBE is required.")
endif()

if(NR_ROTATING_NDJSON_PROBE STREQUAL "")
    message(FATAL_ERROR "NR_ROTATING_NDJSON_PROBE must not be empty.")
endif()

get_filename_component(probe_path "${NR_ROTATING_NDJSON_PROBE}" ABSOLUTE)
if(NOT EXISTS "${probe_path}")
    message(FATAL_ERROR "Rotating NDJSON probe does not exist: '${probe_path}'.")
endif()
if(IS_DIRECTORY "${probe_path}")
    message(FATAL_ERROR "Rotating NDJSON probe must be a file: '${probe_path}'.")
endif()

file(REAL_PATH "${probe_path}" canonical_probe_path)
get_filename_component(probe_parent "${canonical_probe_path}" DIRECTORY)
file(REAL_PATH "${probe_parent}" canonical_probe_parent)
if(NOT IS_DIRECTORY "${canonical_probe_parent}")
    message(FATAL_ERROR "Rotating NDJSON probe parent is not a directory: '${canonical_probe_parent}'.")
endif()

set(output_root_leaf "nr_utils_rotating_ndjson_test_owned_v2")
set(ownership_marker_leaf ".nr-utils-rotating-ndjson-owner")
set(ownership_token "nr-utils-rotating-ndjson-test-root-v1")

function(validate_rotating_ndjson_cleanup_root candidate_root canonical_parent expected_leaf result_variable
         reason_variable)
    set(${result_variable} FALSE PARENT_SCOPE)
    set(${reason_variable} "unknown ownership validation failure" PARENT_SCOPE)

    get_filename_component(candidate_parent "${candidate_root}" DIRECTORY)
    get_filename_component(candidate_leaf "${candidate_root}" NAME)
    if(NOT candidate_parent STREQUAL canonical_parent)
        set(${reason_variable} "candidate is not a direct child of the canonical probe parent" PARENT_SCOPE)
        return()
    endif()
    if(NOT candidate_leaf STREQUAL expected_leaf)
        set(${reason_variable} "candidate leaf does not match the fixed domain leaf" PARENT_SCOPE)
        return()
    endif()
    if(NOT EXISTS "${candidate_root}")
        set(${reason_variable} "candidate does not exist" PARENT_SCOPE)
        return()
    endif()
    if(NOT IS_DIRECTORY "${candidate_root}")
        set(${reason_variable} "candidate is not a directory" PARENT_SCOPE)
        return()
    endif()

    file(REAL_PATH "${candidate_root}" canonical_candidate_root)
    get_filename_component(canonical_candidate_parent "${canonical_candidate_root}" DIRECTORY)
    if(NOT canonical_candidate_root STREQUAL candidate_root OR
       NOT canonical_candidate_parent STREQUAL canonical_parent)
        set(${reason_variable} "candidate resolves outside its exact canonical direct-child path" PARENT_SCOPE)
        return()
    endif()

    set(ownership_marker "${candidate_root}/${ownership_marker_leaf}")
    if(NOT EXISTS "${ownership_marker}" OR IS_DIRECTORY "${ownership_marker}" OR IS_SYMLINK "${ownership_marker}")
        set(${reason_variable} "ownership marker is missing or is not a regular file" PARENT_SCOPE)
        return()
    endif()
    file(READ "${ownership_marker}" marker_contents)
    if(NOT marker_contents STREQUAL ownership_token)
        set(${reason_variable} "ownership marker token does not match" PARENT_SCOPE)
        return()
    endif()

    set(${result_variable} TRUE PARENT_SCOPE)
    set(${reason_variable} "" PARENT_SCOPE)
endfunction()

set(output_root "${canonical_probe_parent}/${output_root_leaf}")
get_filename_component(output_root_parent "${output_root}" DIRECTORY)
get_filename_component(output_root_name "${output_root}" NAME)
if(NOT output_root_parent STREQUAL canonical_probe_parent OR NOT output_root_name STREQUAL output_root_leaf)
    message(FATAL_ERROR "Derived rotating NDJSON output root is not the fixed direct child: '${output_root}'.")
endif()

if(EXISTS "${output_root}" OR IS_SYMLINK "${output_root}")
    validate_rotating_ndjson_cleanup_root(
        "${output_root}"
        "${canonical_probe_parent}"
        "${output_root_leaf}"
        output_root_owned
        output_root_reason
    )
    if(NOT output_root_owned)
        message(FATAL_ERROR "Refusing to clean rotating NDJSON output root: ${output_root_reason}: '${output_root}'.")
    endif()
endif()

file(REMOVE_RECURSE "${output_root}")
file(MAKE_DIRECTORY "${output_root}")
file(WRITE "${output_root}/${ownership_marker_leaf}" "${ownership_token}")

set(main_output_dir "${output_root}/main")

execute_process(
    COMMAND "${canonical_probe_path}" "${main_output_dir}"
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
        "NR_NDJSON_ROTATION_MUST_REACH_CMD")
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
    set(active_path "${main_output_dir}/${stream}.ndjson")
    set(first_backup_path "${main_output_dir}/${stream}.1.ndjson")
    if(NOT EXISTS "${active_path}")
        message(FATAL_ERROR "Active ${stream} NDJSON file was not created: '${active_path}'.")
    endif()
    if(NOT EXISTS "${first_backup_path}")
        message(FATAL_ERROR "Rotated ${stream} NDJSON backup was not created: '${first_backup_path}'.")
    endif()

    file(
        GLOB stream_paths
        LIST_DIRECTORIES false
        "${main_output_dir}/${stream}.ndjson"
        "${main_output_dir}/${stream}.*.ndjson"
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

set(fatal_output_dir "${output_root}/fatal")
execute_process(
    COMMAND "${canonical_probe_path}" "${fatal_output_dir}" --fatal
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
    COMMAND "${canonical_probe_path}" "${main_output_dir}" --prune
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
    if(NOT EXISTS "${main_output_dir}/${stream}.1.ndjson")
        message(FATAL_ERROR "Retention prune probe did not preserve '${stream}.1.ndjson'.")
    endif()
    foreach(excess_index RANGE 5 16)
        if(EXISTS "${main_output_dir}/${stream}.${excess_index}.ndjson")
            message(
                FATAL_ERROR
                "Retention prune probe left excess '${stream}.${excess_index}.ndjson'."
            )
        endif()
    endforeach()
endforeach()

if(EXISTS "${main_output_dir}/.active-viewer")
    message(FATAL_ERROR "Orderly shutdown left the NDJSON viewer lease behind.")
endif()

set(stale_lease_dir "${output_root}/stale-lease")
file(MAKE_DIRECTORY "${stale_lease_dir}/.active-viewer")
execute_process(
    COMMAND "${canonical_probe_path}" "${stale_lease_dir}"
    RESULT_VARIABLE stale_lease_result
    OUTPUT_VARIABLE stale_lease_stdout
    ERROR_VARIABLE stale_lease_stderr
)
if(NOT stale_lease_result EQUAL 0)
    message(
        FATAL_ERROR
        "Stale-lease recovery probe exited with '${stale_lease_result}'.\n"
        "stdout:\n${stale_lease_stdout}\n"
        "stderr:\n${stale_lease_stderr}"
    )
endif()
if(NOT EXISTS "${stale_lease_dir}/engine.ndjson" OR
   NOT EXISTS "${stale_lease_dir}/options.ndjson")
    message(FATAL_ERROR "Stale-lease recovery did not create both active NDJSON files.")
endif()
if(EXISTS "${stale_lease_dir}/.active-viewer")
    message(FATAL_ERROR "Stale-lease recovery left the NDJSON viewer marker behind.")
endif()

set(live_lease_dir "${output_root}/live-lease")
execute_process(
    COMMAND "${canonical_probe_path}" "${live_lease_dir}" --conflict-parent
    RESULT_VARIABLE live_lease_result
    OUTPUT_VARIABLE live_lease_stdout
    ERROR_VARIABLE live_lease_stderr
)
if(NOT live_lease_result EQUAL 0)
    message(
        FATAL_ERROR
        "Live two-process lease probe exited with '${live_lease_result}'.\n"
        "stdout:\n${live_lease_stdout}\n"
        "stderr:\n${live_lease_stderr}"
    )
endif()
string(FIND "${live_lease_stdout}${live_lease_stderr}" "already owned by another viewer" live_conflict_message_index)
if(live_conflict_message_index EQUAL -1)
    message(FATAL_ERROR "Live two-process lease probe did not report the ownership conflict.")
endif()
if(EXISTS "${live_lease_dir}/.active-viewer")
    message(FATAL_ERROR "Live two-process lease probe left the NDJSON viewer marker behind.")
endif()

set(unexpected_marker_dir "${output_root}/unexpected-marker")
file(MAKE_DIRECTORY "${unexpected_marker_dir}/.active-viewer")
file(WRITE "${unexpected_marker_dir}/.active-viewer/unexpected" "not a stale empty marker")
execute_process(
    COMMAND "${canonical_probe_path}" "${unexpected_marker_dir}"
    RESULT_VARIABLE unexpected_marker_result
    OUTPUT_VARIABLE unexpected_marker_stdout
    ERROR_VARIABLE unexpected_marker_stderr
)
if(NOT unexpected_marker_result EQUAL 3)
    message(
        FATAL_ERROR
        "Unexpected-marker probe exited with '${unexpected_marker_result}' instead of 3.\n"
        "stdout:\n${unexpected_marker_stdout}\n"
        "stderr:\n${unexpected_marker_stderr}"
    )
endif()
if(EXISTS "${unexpected_marker_dir}/engine.ndjson" OR
   EXISTS "${unexpected_marker_dir}/options.ndjson")
    message(FATAL_ERROR "Unexpected-marker probe touched active NDJSON files.")
endif()
