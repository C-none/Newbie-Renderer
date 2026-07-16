include("${CMAKE_CURRENT_LIST_DIR}/NrDlssBridgeArtifact.cmake")

set(test_directory "${CMAKE_CURRENT_LIST_DIR}/../build/dlss-bridge-check/fingerprint-test")
set(lf_file "${test_directory}/lf.txt")
set(crlf_file "${test_directory}/crlf.txt")
file(MAKE_DIRECTORY "${test_directory}")
file(WRITE "${lf_file}" "first line\nsecond line\n")
file(WRITE "${crlf_file}" "first line\r\nsecond line\r\n")

nr_sha256_normalized_text("${lf_file}" lf_sha256)
nr_sha256_normalized_text("${crlf_file}" crlf_sha256)
file(REMOVE_RECURSE "${test_directory}")

if(NOT lf_sha256 STREQUAL crlf_sha256)
    message(FATAL_ERROR "Normalized DLSS bridge text hashes differ for LF and CRLF inputs.")
endif()
message(STATUS "Normalized DLSS bridge text fingerprint is stable across LF and CRLF inputs.")
