cmake_minimum_required(VERSION 3.14.0)
project(FCNX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(IFCN_BUILD_GCN_RL_BINDINGS "Build the bundled GCN/RL Python extension module" OFF)
option(IFCN_BUILD_OGDF_ORDERER "Build the optional OGDF fixed-layer orderer" OFF)
option(IFCN_BUILD_TESTS "Build iFCN regression tests" ON)

# Set paths
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Enable testing
enable_testing()

# Platform specific settings
if (WIN32)
    link_directories(C:/msys64/mingw64/bin C:/msys64/mingw64/lib)
    set(DCMAKE_SH "CMAKE_SH-NOTFOUND")
elseif (UNIX)
    add_compile_options(-W -Wall -Wextra)
    add_compile_options(-w)  # Disable all warnings for GCC/Clang
endif()

# Boost
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED COMPONENTS unit_test_framework system filesystem program_options)

# Set static library output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Sub directories
add_subdirectory(include/autopr)  # Add autopr subdirectory
add_subdirectory(include/simon)
if(IFCN_BUILD_GCN_RL_BINDINGS)
    add_subdirectory(include/gcn_rl_layout)
endif()
if(IFCN_BUILD_OGDF_ORDERER)
    add_subdirectory(include/gcn_rl_layout/ogdf)
endif()
add_subdirectory(src)
if(IFCN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
