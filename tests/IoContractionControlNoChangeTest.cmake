if(NOT DEFINED IFCN_GUI OR NOT DEFINED IFCN_INPUT OR NOT DEFINED IFCN_OUTPUT)
    message(FATAL_ERROR "IFCN_GUI, IFCN_INPUT, and IFCN_OUTPUT are required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "QT_QPA_PLATFORM=offscreen"
            "IFCN_AUTO_MAP_FILE=${IFCN_INPUT}"
            "IFCN_AUTO_EXPORT_CELL_LAYOUT=${IFCN_OUTPUT}"
            "IFCN_AUTO_CONTRACT_IO=1"
            "${IFCN_GUI}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
    TIMEOUT 20)

# A no-change layout is expected to return 3.  A recursive signal loop instead
# terminates abnormally, so it must not be accepted as the expected outcome.
if(NOT "${result}" STREQUAL "3")
    message(FATAL_ERROR
        "Expected a clean no-change exit (3), got '${result}'.\n"
        "stdout:\n${stdout}\n"
        "stderr:\n${stderr}")
endif()
