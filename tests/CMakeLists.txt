include_directories(catch2) # v2.10.1

set(FILENAMES test_main.cpp EnergyAnalysisUnitTest.cpp)
#file(GLOB_RECURSE FILENAMES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(run_tests ${FILENAMES})
target_link_libraries(run_tests simon)
target_compile_definitions(run_tests PUBLIC "TESTCASE=\"${CMAKE_CURRENT_SOURCE_DIR}/benchmark\"")

#add_executable(energy EnergyAnalysisUnitTest.cpp)
#target_link_libraries(energy simon)
#target_compile_definitions(energy PUBLIC "TESTCASE=\"${CMAKE_CURRENT_SOURCE_DIR}/benchmark\"")

