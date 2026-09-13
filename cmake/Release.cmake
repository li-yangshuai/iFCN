# Install application/runtime files and the curated examples/documentation.
# Build trees, regression executables, and generated fixtures stay out of packages.
install(TARGETS fcnx_gui RUNTIME DESTINATION bin BUNDLE DESTINATION .)

foreach(IFCN_RUNTIME_TOOL IN ITEMS
        ifcn_mapping_metrics
        ifcn_combinational_pnr
        ifcn_sequential_pnr
        ifcn_paper_cyclic_pnr
        ifcn_physical_state_layout
        ifcn_energy_analysis
        ifcn_physical_benchmark
        ifcn_ogdf_layer_order)
    if(TARGET ${IFCN_RUNTIME_TOOL})
        install(TARGETS ${IFCN_RUNTIME_TOOL} RUNTIME DESTINATION bin)
    endif()
endforeach()

if(UNIX AND NOT APPLE)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/ifcn.in"
                   "${PROJECT_BINARY_DIR}/ifcn" @ONLY)
    install(PROGRAMS "${PROJECT_BINARY_DIR}/ifcn" DESTINATION .)
endif()

if(IFCN_BUILD_LAYOUT_BINDINGS)
    install(TARGETS iFCN_Lab
        LIBRARY DESTINATION python/lib
        RUNTIME DESTINATION python/lib)
    # main/test_normal_graph_draw.py is the production GUI backend entry point.
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/layout_backend/src/algorithm"
        DESTINATION include/layout_backend/src
        FILES_MATCHING PATTERN "*.py"
        PATTERN "__pycache__" EXCLUDE
        PATTERN "tests" EXCLUDE)
    install(FILES "${PROJECT_SOURCE_DIR}/include/layout_backend/requirements.txt"
        DESTINATION include/layout_backend)
endif()

install(DIRECTORY "${PROJECT_SOURCE_DIR}/examples/"
    DESTINATION examples FILES_MATCHING PATTERN "*.ifcn")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/tests/benchmarks_f/"
    DESTINATION tests/benchmarks_f FILES_MATCHING
    PATTERN "*.v" PATTERN "*.sv"
    PATTERN "__pycache__" EXCLUDE)
install(FILES
    "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/NOTICE"
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/README.en.md"
    DESTINATION .)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/LICENSES/" DESTINATION LICENSES)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs/"
    DESTINATION docs FILES_MATCHING
    PATTERN "*.md" PATTERN "*.png" PATTERN "*.csv"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "examples" EXCLUDE)

set(CPACK_GENERATOR "TGZ")
set(CPACK_PACKAGE_NAME "iFCN")
set(CPACK_PACKAGE_VENDOR "iFCN contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "iFCN circuit design, layout, and simulation")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/li-yangshuai/iFCN")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME
    "iFCN-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

if(WIN32)
    include("${PROJECT_SOURCE_DIR}/cmake/WindowsPackage.cmake")
elseif(APPLE)
    include("${PROJECT_SOURCE_DIR}/cmake/MacPackage.cmake")
endif()
include(CPack)
