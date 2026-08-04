if(NOT DEFINED IFCN_TEST_EXECUTABLE OR NOT DEFINED IFCN_INPUT)
    message(FATAL_ERROR "IFCN_TEST_EXECUTABLE and IFCN_INPUT are required")
endif()

set(candidate_scales 40 20)
set(candidate_costs 40 320)
set(candidate_retries 4 24)
list(LENGTH candidate_scales candidate_count)
math(EXPR candidate_last "${candidate_count} - 1")

foreach(candidate_index RANGE 0 ${candidate_last})
    list(GET candidate_scales ${candidate_index} grid_scale)
    list(GET candidate_costs ${candidate_index} search_cost)
    list(GET candidate_retries ${candidate_index} route_order_retries)
    execute_process(
        COMMAND "${IFCN_TEST_EXECUTABLE}" "${IFCN_INPUT}" "${grid_scale}" "${search_cost}" "${route_order_retries}"
        RESULT_VARIABLE candidate_result
        OUTPUT_VARIABLE candidate_stdout
        ERROR_VARIABLE candidate_stderr)
    if(candidate_result EQUAL 0)
        message(STATUS "June fallback accepted /${grid_scale}, cost ${search_cost}, retries ${route_order_retries}")
        return()
    endif()
endforeach()

message(FATAL_ERROR
    "All June random-clock fallback candidates failed.\n${candidate_stdout}\n${candidate_stderr}")
