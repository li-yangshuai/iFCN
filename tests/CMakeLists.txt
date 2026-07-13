option(IFCN_BUILD_LEGACY_SIMON_TESTS "Build the original long-running simon tests" OFF)

add_executable(simulation_metrics_tests SimulationMetricsUnitTest.cpp)
target_link_libraries(simulation_metrics_tests PRIVATE simon)
add_test(NAME simulation_metrics_regression COMMAND simulation_metrics_tests)

add_test(
    NAME physical_benchmark_smoke
    COMMAND $<TARGET_FILE:ifcn_physical_benchmark>
            "${PROJECT_SOURCE_DIR}/tests/cell_level_examples/others/xor2.qca"
            --model both
            --repetitions 1
            --samples 128
            --duration 2e-14
            --time-step 1e-16
            --require-equivalent
            --equivalence-tolerance 0)

set(IFCN_HFUT_SIM_BENCHMARK_DIR "${PROJECT_SOURCE_DIR}/../hfut-sim/benchmark" CACHE PATH
    "Path to the original hfut-sim QCA benchmark directory")

add_executable(accelerated_bistable_tests AcceleratedBistableUnitTest.cpp)
target_link_libraries(accelerated_bistable_tests PRIVATE simon)

add_executable(accelerated_coherence_tests AcceleratedCoherenceUnitTest.cpp)
target_link_libraries(accelerated_coherence_tests PRIVATE simon)

if(EXISTS "${IFCN_HFUT_SIM_BENCHMARK_DIR}/xor-14.qca")
    add_test(NAME accelerated_bistable_equivalence
             COMMAND accelerated_bistable_tests
                     "${IFCN_HFUT_SIM_BENCHMARK_DIR}/xor-14.qca")
    add_test(NAME accelerated_coherence_equivalence
             COMMAND accelerated_coherence_tests
                     "${IFCN_HFUT_SIM_BENCHMARK_DIR}/xor-14.qca")
else()
    message(STATUS "hfut-sim benchmarks not found; physical equivalence tests are skipped")
endif()

if(IFCN_BUILD_LEGACY_SIMON_TESTS)
    include_directories(catch2) # v2.10.1
    set(FILENAMES test_main.cpp EnergyAnalysisUnitTest.cpp)
    add_executable(run_tests ${FILENAMES})
    target_link_libraries(run_tests simon)
    target_compile_definitions(run_tests PUBLIC "TESTCASE=\"${CMAKE_CURRENT_SOURCE_DIR}/benchmark\"")
endif()

#add_executable(energy EnergyAnalysisUnitTest.cpp)
#target_link_libraries(energy simon)
#target_compile_definitions(energy PUBLIC "TESTCASE=\"${CMAKE_CURRENT_SOURCE_DIR}/benchmark\"")
