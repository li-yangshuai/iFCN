if(NOT DEFINED OGDF_ORDERER OR NOT EXISTS "${OGDF_ORDERER}")
    message(FATAL_ERROR "OGDF layer-order helper is unavailable: ${OGDF_ORDERER}")
endif()

set(input_path "${CMAKE_CURRENT_BINARY_DIR}/ogdf_crossing_order_regression.in")
file(WRITE "${input_path}"
"8 10 4
0 0 0
1 0 1
2 0 2
4 1 0
5 1 1
6 1 2
7 2 0
3 3 0
0 4
0 6
1 4
1 5
2 5
2 6
4 7
5 7
6 7
7 3
")

execute_process(
    COMMAND "${OGDF_ORDERER}"
    INPUT_FILE "${input_path}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    RESULT_VARIABLE result)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "OGDF helper failed (${result}): ${error}")
endif()
if(NOT output MATCHES "^OGDF [0-9.]+ 2")
    message(FATAL_ERROR "Expected OGDF to reduce the demo from 3 to 2 crossings, got:\n${output}")
endif()
string(FIND "${output}" "1 0 4\n1 1 6\n1 2 5\n" optimized_layer)
if(optimized_layer LESS 0)
    message(FATAL_ERROR "Unexpected OGDF layer-1 order; expected [4,6,5]:\n${output}")
endif()
