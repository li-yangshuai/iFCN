# Native macOS bundle metadata; deployment is performed by macos/deploy.py.
if(APPLE)
    set_target_properties(fcnx_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "iFCN"
        MACOSX_BUNDLE_BUNDLE_NAME "iFCN"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.ifcn.desktop"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/macos/Info.plist.in")
endif()
