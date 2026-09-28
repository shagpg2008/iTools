if(NOT DEFINED STRIP_PROGRAM OR STRIP_PROGRAM STREQUAL "")
    message(FATAL_ERROR "STRIP_PROGRAM is required")
endif()
if(NOT DEFINED TARGET_FILE OR TARGET_FILE STREQUAL "")
    message(FATAL_ERROR "TARGET_FILE is required")
endif()

# Antivirus scanners, file indexers and other platform services can briefly
# hold a newly linked executable. Retry only the transient strip operation
# without changing the artifact layout. STRIP_OPTIONS is a CMake list so the
# wrapper also supports platform-specific flags (GNU --strip-all, Apple -x).
set(STRIP_ATTEMPTS 10)
foreach(STRIP_ATTEMPT RANGE 1 ${STRIP_ATTEMPTS})
    execute_process(
        COMMAND "${STRIP_PROGRAM}" ${STRIP_OPTIONS} "${TARGET_FILE}"
        RESULT_VARIABLE STRIP_RESULT
        ERROR_VARIABLE STRIP_ERROR
    )
    if(STRIP_RESULT EQUAL 0)
        if(STRIP_ATTEMPT GREATER 1)
            message(STATUS "strip succeeded on attempt ${STRIP_ATTEMPT}")
        endif()
        return()
    endif()
    if(STRIP_ATTEMPT LESS STRIP_ATTEMPTS)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
    endif()
endforeach()

string(STRIP "${STRIP_ERROR}" STRIP_ERROR)
message(FATAL_ERROR
    "strip failed after ${STRIP_ATTEMPTS} attempts for ${TARGET_FILE}: ${STRIP_ERROR}")
