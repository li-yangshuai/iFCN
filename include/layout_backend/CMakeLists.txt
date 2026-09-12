cmake_minimum_required(VERSION 3.14)
project(ifcn_layout_backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

add_library(layout_parse STATIC
    src/parse/Parse.cpp
)
target_include_directories(layout_parse PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
)
set_target_properties(layout_parse PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(layout_chessboard STATIC
    src/chessboard/MapPhaseAStar.cpp
    src/chessboard/RightDownAStar.cpp
)
target_include_directories(layout_chessboard PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
)
set_target_properties(layout_chessboard PROPERTIES POSITION_INDEPENDENT_CODE ON)

pybind11_add_module(iFCN_Lab
    lib/bindings/ifcn_bindings.cpp
)
target_include_directories(iFCN_Lab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parse
    ${CMAKE_CURRENT_SOURCE_DIR}/src/chessboard
)
target_link_libraries(iFCN_Lab PRIVATE
    layout_parse
    layout_chessboard
    Python3::Python
)

set_target_properties(iFCN_Lab PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/lib"
)
