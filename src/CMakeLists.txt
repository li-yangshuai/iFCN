set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Svg)

##graph render libraries##
find_package(PkgConfig REQUIRED)
find_package(Qt5 COMPONENTS Svg REQUIRED)
pkg_check_modules(GVC REQUIRED libgvc)
pkg_check_modules(CGRAPH REQUIRED libcgraph)
#graph render libraries##

qt5_add_resources(RES_RCC "${CMAKE_CURRENT_SOURCE_DIR}/images/images.qrc")
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include_directories(${PROJECT_SOURCE_DIR}/include)

##graph render libraries##
include_directories(${GVC_INCLUDE_DIRS})
include_directories(${CGRAPH_INCLUDE_DIRS})
##graph render libraries##

add_executable(
    fcnx_gui 
    main_gui.cpp 
    MainWindow.cpp 
    QCADCellItem.cpp 
    QCADView.cpp             
    QCADScene.cpp 
    QCADClockScheme.cpp 
    SimulationManager.cpp
    VerilogHandler.cpp
    widgets/LayerComboBox.cpp             
    widgets/CustomStatusBar.cpp
    widgets/plotWidget.cpp
    widgets/qcustomplot.cpp
    widgets/waveformwindow.cpp
    widgets/typewindow.cpp
    widgets/GaChessboardInputDialog.cpp
    ${RES_RCC})
    
target_link_libraries(fcnx_gui PRIVATE autopr simon  ${Boost_LIBRARIES} Qt5::Widgets  Qt5::PrintSupport Qt5::Svg
                       ${GVC_LIBRARIES} ${CGRAPH_LIBRARIES})


