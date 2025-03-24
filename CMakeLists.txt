cmake_minimum_required(VERSION 3.14.0)
project(FCNX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set paths
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})
set(CMAKE_BUILD_TYPE "Debug")

# Enable testing
enable_testing()

# Platform specific settings
if (WIN32)
    link_directories(C:/msys64/mingw64/bin C:/msys64/mingw64/lib)
    set(DCMAKE_SH "CMAKE_SH-NOTFOUND")
elseif (UNIX)
    add_compile_options(-W -Wall -Wextra -O2)
    add_compile_options(-w)  # Disable all warnings for GCC/Clang
endif()

# Boost
set(Boost_USE_MULTITHREADED ON)
find_package(Boost REQUIRED COMPONENTS unit_test_framework system filesystem program_options)

# Set static library output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/lib)

# Sub directories
add_subdirectory(include/autopr)  # Add autopr subdirectory
add_subdirectory(include/simon)
# add_subdirectory(include/sim)
add_subdirectory(src)
