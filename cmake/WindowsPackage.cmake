# Windows releases are built with the native MSYS2 UCRT64 toolchain. The
# development environment is only needed on the build machine.
option(IFCN_BUNDLE_WINDOWS_RUNTIME "Bundle the complete Windows release runtime" OFF)
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "iFCN-${PROJECT_VERSION}-windows-x86_64")
if(NOT IFCN_BUNDLE_WINDOWS_RUNTIME)
    return()
endif()
if(NOT MINGW)
    message(FATAL_ERROR "Windows release packaging requires the MSYS2 MinGW/UCRT toolchain")
endif()
if(NOT IFCN_BUILD_LAYOUT_BINDINGS)
    message(FATAL_ERROR "Windows release packaging includes the classical layout backend; enable IFCN_BUILD_LAYOUT_BINDINGS")
endif()

get_filename_component(IFCN_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(IFCN_MINGW_PREFIX "${IFCN_MINGW_BIN}" DIRECTORY)
find_program(IFCN_WINDOWS_PYTHON NAMES python3.exe python.exe
    PATHS "${IFCN_MINGW_BIN}" NO_DEFAULT_PATH REQUIRED)

add_executable(ifcn_windows_launcher WIN32
    "${PROJECT_SOURCE_DIR}/cmake/windows/launcher.cpp")
set_target_properties(ifcn_windows_launcher PROPERTIES OUTPUT_NAME iFCN)
target_link_libraries(ifcn_windows_launcher PRIVATE user32)
target_link_options(ifcn_windows_launcher PRIVATE -static)
install(TARGETS ifcn_windows_launcher RUNTIME DESTINATION .)

# Run after the normal install rules so the dependency closure includes every
# installed executable, Python extension, and explicitly selected plugin.
install(CODE "
    execute_process(
        COMMAND \"${IFCN_WINDOWS_PYTHON}\"
            \"${PROJECT_SOURCE_DIR}/cmake/windows/bundle.py\"
            --prefix \"${IFCN_MINGW_PREFIX}\"
            --stage \"\${CMAKE_INSTALL_PREFIX}\"
        RESULT_VARIABLE _ifcn_bundle_result)
    if(NOT _ifcn_bundle_result EQUAL 0)
        message(FATAL_ERROR \"Windows runtime dependency packaging failed\")
    endif()
")
