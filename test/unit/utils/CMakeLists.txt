nr_add_contract_test(nr_utils_threading_contract_test nr_utils_threading_contract_test.cpp "fast;cpu;unit;utils"
    utils
)

nr_add_contract_test(nr_utils_revision_contract_test nr_utils_revision_contract_test.cpp "fast;cpu;unit;utils"
    utils
)

nr_add_contract_test(nr_utils_error_handle_contract_test nr_utils_error_handle_contract_test.cpp "fast;cpu;unit;utils"
    utils
)

nr_add_executable(nr_utils_error_handle_lazy_failure_probe
    nr_utils_error_handle_lazy_failure_probe.cpp
)
set_target_properties(nr_utils_error_handle_lazy_failure_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_utils_error_handle_lazy_failure_probe
    nr_utils_error_handle_lazy_failure_probe.cpp
    PRIVATE
        utils
)

add_test(
    NAME nr_utils_error_handle_lazy_failure_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_ASSERT_FAILURE_PROBE=$<TARGET_FILE:nr_utils_error_handle_lazy_failure_probe>"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_utils_error_handle_lazy_failure.cmake
)
set_tests_properties(
    nr_utils_error_handle_lazy_failure_test
    PROPERTIES LABELS "fast;cpu;unit;utils"
)

nr_add_executable(nr_utils_rotating_ndjson_probe
    nr_utils_rotating_ndjson_probe.cpp
)
set_target_properties(nr_utils_rotating_ndjson_probe PROPERTIES FOLDER "Test")
nr_link_module_dependencies(
    nr_utils_rotating_ndjson_probe
    nr_utils_rotating_ndjson_probe.cpp
    PRIVATE
        utils
)

add_test(
    NAME nr_utils_rotating_ndjson_test
    COMMAND ${CMAKE_COMMAND}
        "-DNR_ROTATING_NDJSON_PROBE=$<TARGET_FILE:nr_utils_rotating_ndjson_probe>"
        "-DNR_ROTATING_NDJSON_OUTPUT_DIR=$<TARGET_FILE_DIR:nr_utils_rotating_ndjson_probe>/nr_utils_rotating_ndjson_test_logs"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_nr_utils_rotating_ndjson.cmake
)
set_tests_properties(
    nr_utils_rotating_ndjson_test
    PROPERTIES
        LABELS "fast;cpu;unit;utils"
        TIMEOUT 30
)
