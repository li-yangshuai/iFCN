option(IFCN_BUILD_LEGACY_SIMON_TESTS "Build the original long-running simon tests" OFF)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
    NAME negotiated_router_regression
    COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/include/gcn_rl_layout/tests/test_negotiated_router.py")

add_executable(simulation_metrics_tests SimulationMetricsUnitTest.cpp)
target_link_libraries(simulation_metrics_tests PRIVATE simon)
add_test(NAME simulation_metrics_regression COMMAND simulation_metrics_tests)

add_executable(mapping_io_contraction_tests MappingIoContractionUnitTest.cpp)
target_include_directories(mapping_io_contraction_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(mapping_io_contraction_tests PRIVATE autopr)
add_test(NAME mapping_io_contraction_regression COMMAND mapping_io_contraction_tests)

add_executable(mapping_crossover_tests MappingCrossoverUnitTest.cpp)
target_include_directories(mapping_crossover_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(mapping_crossover_tests PRIVATE autopr)
add_test(NAME mapping_crossover_regression COMMAND mapping_crossover_tests)

find_package(Qt5 REQUIRED COMPONENTS Core)
add_executable(cell_level_io_contraction_tests
    CellLevelIoContractionUnitTest.cpp
    ${PROJECT_SOURCE_DIR}/src/controllers/CellLevelIoContraction.cpp)
target_include_directories(cell_level_io_contraction_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(cell_level_io_contraction_tests PRIVATE Qt5::Core)
add_test(NAME cell_level_io_contraction_regression COMMAND cell_level_io_contraction_tests)

add_test(
    NAME io_contraction_control_no_change_regression
    COMMAND ${CMAKE_COMMAND}
            "-DIFCN_GUI=$<TARGET_FILE:fcnx_gui>"
            "-DIFCN_INPUT=${PROJECT_SOURCE_DIR}/tests/cell_level_examples/others/xor2_use.qca"
            "-DIFCN_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/io_contraction_no_change.svg"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/IoContractionControlNoChangeTest.cmake")
set_tests_properties(io_contraction_control_no_change_regression
                     PROPERTIES TIMEOUT 30)

add_executable(phase_aware_routing_tests PhaseAwareRoutingUnitTest.cpp)
target_include_directories(phase_aware_routing_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(phase_aware_routing_tests PRIVATE autopr)
add_test(NAME phase_aware_routing_regression COMMAND phase_aware_routing_tests)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_GVC REQUIRED libgvc)
pkg_check_modules(TEST_CGRAPH REQUIRED libcgraph)
add_executable(stochastic_compact_graph_tests StochasticCompactGraphUnitTest.cpp)
target_include_directories(stochastic_compact_graph_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${TEST_GVC_INCLUDE_DIRS}
    ${TEST_CGRAPH_INCLUDE_DIRS})
target_compile_definitions(stochastic_compact_graph_tests PRIVATE
    IFCN_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(stochastic_compact_graph_tests PRIVATE
    autopr
    ${TEST_GVC_LIBRARIES}
    ${TEST_CGRAPH_LIBRARIES})
add_test(NAME stochastic_compact_graph_regression COMMAND stochastic_compact_graph_tests)
add_test(
    NAME stochastic_compact_graph_fallback_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/clpl.v")
set_tests_properties(stochastic_compact_graph_fallback_regression
                     PROPERTIES TIMEOUT 30)
add_test(
    NAME stochastic_compact_graph_buffered_fallback_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/xor5R.v"
            --keep-buffers)
set_tests_properties(stochastic_compact_graph_buffered_fallback_regression
                     PROPERTIES TIMEOUT 45)
add_test(
    NAME stochastic_compact_graph_anisotropic_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/RCA2.v"
            --graphviz-xy 35 36 420 0 1800)
set_tests_properties(stochastic_compact_graph_anisotropic_regression
                     PROPERTIES TIMEOUT 45)
add_test(
    NAME stochastic_compact_graph_adaptive_compact_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/TOY/paper_2ddwave_crossing_demo.v"
            --adaptive-compact 2 8 120)
add_test(
    NAME stochastic_compact_graph_adaptive_maj_adder_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/1bitAdderMaj.v"
            --adaptive-compact 2 8 120)
set_tests_properties(
    stochastic_compact_graph_adaptive_compact_regression
    stochastic_compact_graph_adaptive_maj_adder_regression
    PROPERTIES TIMEOUT 15)
set(IFCN_OGDF_TEST_ORDERER
    "${PROJECT_SOURCE_DIR}/build-ogdf/ifcn_ogdf_layer_order")
if(EXISTS "${IFCN_OGDF_TEST_ORDERER}")
    add_test(
        NAME ogdf_crossing_order_regression
        COMMAND ${CMAKE_COMMAND}
                -DOGDF_ORDERER=${IFCN_OGDF_TEST_ORDERER}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/OgdfCrossingOrderRegression.cmake")
endif()
add_test(
    NAME stochastic_compact_graph_small_maj_xor_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/xor2.v"
            --graphviz-xy 52 72 480 10 72)
add_test(
    NAME stochastic_compact_graph_small_maj_xnor_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/xnor2.v"
            --graphviz-xy 56 72 420 8 84)
add_test(
    NAME stochastic_compact_graph_small_mux_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/TOY/mux21.v"
            --graphviz-xy 96 96 240 8 20)
add_test(
    NAME stochastic_compact_graph_small_maj_adder_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/1bitAdderMaj.v"
            --graphviz-xy 88 88 300 12 42)
add_test(
    NAME stochastic_compact_graph_small_toy_xnor_regression
    COMMAND stochastic_compact_graph_tests
            "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/TOY/xnor2.v"
            --graphviz-xy 40 68 420 8 50)
set_tests_properties(
    stochastic_compact_graph_small_maj_xor_regression
    stochastic_compact_graph_small_maj_xnor_regression
    stochastic_compact_graph_small_mux_regression
    stochastic_compact_graph_small_maj_adder_regression
    stochastic_compact_graph_small_toy_xnor_regression
    PROPERTIES TIMEOUT 15)

add_executable(legacy_graphviz_renderer_tests LegacyGraphvizRendererUnitTest.cpp)
target_include_directories(legacy_graphviz_renderer_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${TEST_GVC_INCLUDE_DIRS}
    ${TEST_CGRAPH_INCLUDE_DIRS})
target_compile_definitions(legacy_graphviz_renderer_tests PRIVATE
    IFCN_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(legacy_graphviz_renderer_tests PRIVATE
    autopr
    ${TEST_GVC_LIBRARIES}
    ${TEST_CGRAPH_LIBRARIES})
add_test(NAME legacy_graphviz_renderer_regression COMMAND legacy_graphviz_renderer_tests)

add_executable(june_random_clock_graph_tests JuneRandomClockGraphUnitTest.cpp)
target_include_directories(june_random_clock_graph_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${TEST_GVC_INCLUDE_DIRS}
    ${TEST_CGRAPH_INCLUDE_DIRS})
target_compile_definitions(june_random_clock_graph_tests PRIVATE
    IFCN_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(june_random_clock_graph_tests PRIVATE
    autopr
    ${TEST_GVC_LIBRARIES}
    ${TEST_CGRAPH_LIBRARIES})
add_test(NAME june_random_clock_graph_regression COMMAND june_random_clock_graph_tests)
add_test(
    NAME june_random_clock_graph_fallback_regression
    COMMAND ${CMAKE_COMMAND}
            "-DIFCN_TEST_EXECUTABLE=$<TARGET_FILE:june_random_clock_graph_tests>"
            "-DIFCN_INPUT=${PROJECT_SOURCE_DIR}/tests/benchmarks_f/MAJ/clpl.v"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/JuneRandomClockFallbackTest.cmake")

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
