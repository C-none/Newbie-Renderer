# GenerateConstants.cmake - Generate staticUtilsConstants.h from template

# Debug mode detection
if(CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG_MODE true)
else()
    set(IS_DEBUG_MODE false)
endif()

# Thread count
include(ProcessorCount)
ProcessorCount(MAX_THREADS)
if(MAX_THREADS EQUAL 0)
    set(MAX_THREADS 4)
endif()

# Log level definitions
set(LOG_LEVELS "info:INFO" "warning:WARNING" "error:ERROR")

set(LOG_LEVEL_ENUM "")
set(LOG_LEVEL_NAMES "")
set(LOG_LEVEL_VALUE 0)
set(LOG_LEVEL_NAME "info")
set(i 0)
foreach(DEF ${LOG_LEVELS})
    string(REPLACE ":" ";" P "${DEF}")
    list(GET P 0 ENUM)
    list(GET P 1 NAME)
    if(i GREATER 0)
        string(APPEND LOG_LEVEL_ENUM ",\n    ")
        string(APPEND LOG_LEVEL_NAMES ", ")
    else()
        string(APPEND LOG_LEVEL_ENUM "\n    ")
    endif()
    string(APPEND LOG_LEVEL_ENUM "${ENUM} = ${i}")
    string(APPEND LOG_LEVEL_NAMES "\"${NAME}\"")
    if(NR_LOG_LEVEL STREQUAL "${NAME}")
        set(LOG_LEVEL_VALUE ${i})
        set(LOG_LEVEL_NAME ${ENUM})
    endif()
    math(EXPR i "${i} + 1")
endforeach()
string(APPEND LOG_LEVEL_ENUM ",\n    number = ${i}")

configure_file("${INPUT_FILE}" "${OUTPUT_FILE}")
