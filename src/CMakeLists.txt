set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Svg)

##graph render libraries##
find_package(PkgConfig REQUIRED)
find_package(Qt5 COMPONENTS Svg REQUIRED)
pkg_check_modules(GVC REQUIRED libgvc)
pkg_check_modules(CGRAPH REQUIRED libcgraph)
#graph render libraries##

qt5_add_resources(RES_RCC "${PROJECT_SOURCE_DIR}/resources/ui_images/images.qrc")
include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(${PROJECT_SOURCE_DIR}/include)

##graph render libraries##
include_directories(${GVC_INCLUDE_DIRS})
include_directories(${CGRAPH_INCLUDE_DIRS})
##graph render libraries##

add_executable(
    fcnx_gui 
    app/main_gui.cpp 
    ui/mainwindow/MainWindow.h
    ui/mainwindow/TabbedMainWindow.cpp
    ui/mainwindow/MainWindow.Core.cpp
    ui/mainwindow/MainWindow.MenuToolbar.cpp
    ui/mainwindow/MainWindow.ViewScene.cpp
    ui/mainwindow/MainWindow.Circuit.cpp
    ui/mainwindow/MainWindow.Status.cpp
    ui/mainwindow/MainWindow.Logic.cpp
    ui/mainwindow/MainWindow.IoContraction.cpp
    ui/items/QCADCellItem.cpp 
    ui/view/QCADView.cpp             
    ui/view/QCADScene.cpp 
    ui/items/QCADClockScheme.cpp 
    controllers/SimulationManager.cpp
    controllers/VerilogHandler.cpp
    controllers/GateLevelMapping.cpp
    controllers/CellLevelIoContraction.cpp
    controllers/MappingExecutor.cpp
    ui/widgets/LayerComboBox.cpp             
    ui/widgets/CircuitSchematicView.cpp
    ui/widgets/LayeredStructure3DView.h
    ui/widgets/LayeredStructure3DView.cpp
    ui/widgets/CustomStatusBar.cpp
    ui/widgets/plotWidget.cpp
    ui/widgets/qcustomplot.cpp
    ui/widgets/waveformwindow.cpp
    ui/widgets/typewindow.cpp
    ui/widgets/GaChessboardInputDialog.cpp
    ${RES_RCC})
    
target_link_libraries(fcnx_gui PRIVATE autopr simon  ${Boost_LIBRARIES} Qt5::Widgets  Qt5::PrintSupport Qt5::Svg 
                       ${GVC_LIBRARIES} ${CGRAPH_LIBRARIES})

target_compile_definitions(fcnx_gui PRIVATE IFCN_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_executable(ifcn_mapping_metrics app/ifcn_mapping_metrics.cpp)
target_link_libraries(ifcn_mapping_metrics PRIVATE autopr)
target_compile_definitions(ifcn_mapping_metrics PRIVATE IFCN_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_executable(ifcn_energy_analysis app/ifcn_energy_analysis.cpp)
target_link_libraries(ifcn_energy_analysis PRIVATE autopr simon)
target_compile_definitions(ifcn_energy_analysis PRIVATE IFCN_PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_executable(ifcn_physical_benchmark app/ifcn_physical_benchmark.cpp)
target_link_libraries(ifcn_physical_benchmark PRIVATE simon)
