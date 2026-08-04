cmake_minimum_required(VERSION 3.14)
project(ifcn_ogdf_layer_order LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OGDF_SOURCE_DIR "" CACHE PATH "Path to an existing OGDF source checkout")
set(OGDF_GIT_TAG "foxglove-202510" CACHE STRING "Pinned OGDF release tag")

if(NOT TARGET OGDF)
    if(OGDF_SOURCE_DIR AND EXISTS "${OGDF_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${OGDF_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ogdf" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            ogdf
            GIT_REPOSITORY https://github.com/ogdf/ogdf.git
            GIT_TAG ${OGDF_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(ogdf)
    endif()
endif()

add_executable(ifcn_ogdf_layer_order ogdf_layer_order.cpp)
target_link_libraries(ifcn_ogdf_layer_order PRIVATE OGDF)

