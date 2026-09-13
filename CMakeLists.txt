cmake_minimum_required(VERSION 3.14.0)
project(FCNX VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(IFCN_BUILD_LAYOUT_BINDINGS "Build the classical layout Python extension module" OFF)
option(IFCN_BUILD_OGDF_ORDERER "Build the optional OGDF fixed-layer orderer" OFF)
option(IFCN_BUILD_TESTS "Build iFCN regression tests" ON)
option(IFCN_BUILD_TOOLS "Build headless layout, simulation, and diagnostic tools" ON)

# Set paths
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Test fixtures and test executables are generated only for development builds.
if(IFCN_BUILD_TESTS)
    enable_testing()
endif()

# Platform specific settings
# Qt, Boost, and Graphviz are discovered from the selected toolchain, including
# Windows builds outside the default MSYS2 installation directory.
if (UNIX)
    add_compile_options(-W -Wall -Wextra)
    add_compile_options(-w)  # Disable all warnings for GCC/Clang
endif()

# Boost
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED COMPONENTS filesystem program_options)

# Set static library output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Sub directories
add_subdirectory(include/autopr)  # Add autopr subdirectory
add_subdirectory(include/simon)
if(IFCN_BUILD_LAYOUT_BINDINGS)
    add_subdirectory(include/layout_backend)
endif()
if(IFCN_BUILD_OGDF_ORDERER)
    add_subdirectory(include/layout_backend/ogdf)
endif()
add_subdirectory(src)
if(IFCN_BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(cmake/Release.cmake)
