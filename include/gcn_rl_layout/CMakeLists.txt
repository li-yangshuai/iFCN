cmake_minimum_required(VERSION 3.14)
project(ifcn_gcn_rl_layout)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

add_library(gcn_rl_parse STATIC
    src/parse/Parse.cpp
)
target_include_directories(gcn_rl_parse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
)
set_target_properties(gcn_rl_parse PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(gcn_rl_chessboard STATIC
    src/chessboard/MapPhaseAStar.cpp
)
target_include_directories(gcn_rl_chessboard PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
)
set_target_properties(gcn_rl_chessboard PROPERTIES POSITION_INDEPENDENT_CODE ON)

pybind11_add_module(iFCN_Lab
    lib/bindings/ifcn_bindings.cpp
)
target_include_directories(iFCN_Lab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
)
target_link_libraries(iFCN_Lab PRIVATE
    gcn_rl_parse
    gcn_rl_chessboard
    Python3::Python
)

add_custom_command(TARGET iFCN_Lab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm/lib
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:iFCN_Lab>
            ${CMAKE_CURRENT_SOURCE_DIR}/src/algorithm/lib/$<TARGET_FILE_NAME:iFCN_Lab>
)
