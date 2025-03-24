cmake_minimum_required(VERSION 3.0)
project(autopr)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR})

# Add source files
file(GLOB_RECURSE AUTOPR_SOURCES
    *.cpp
)

# Create a static library
add_library(autopr STATIC ${AUTOPR_SOURCES})

# Set properties for the library
set_target_properties(autopr PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
)
