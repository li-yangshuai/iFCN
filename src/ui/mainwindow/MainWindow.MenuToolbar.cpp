#include "ui/mainwindow/MainWindow.h"
#include <QMenuBar>
#include <QDir>
#include <QKeySequence>

void MainWindow::createActions()
{

    /******** "新建"动作 *********/
    newAction = new QAction(QIcon(QDir::toNativeSeparators(":/new.png")), tr("&New"), this);
    //newAction->setShortcuts(tr("Ctrl+N"));
    newAction->setShortcuts(QKeySequence::New);
    newAction->setStatusTip(tr("Create a new file"));
    connect(newAction, SIGNAL(triggered()), this, SLOT( slotNew() ));

    /******** "打开"动作 *********/
    openAction = new QAction(QIcon(QDir::toNativeSeparators(":/open.png")), tr("&Open"), this);
    //openAction->setShortcuts(tr("Ctrl+O"));
    openAction->setShortcuts(QKeySequence::Open);
    openAction->setStatusTip(tr("Open a file"));
    connect(openAction, &QAction::triggered, this, &MainWindow::slotOpen);

    /******** "保存"动作 *********/
    saveAction = new QAction(QIcon(QDir::toNativeSeparators(":/save.png")), tr("&Save"), this);
    saveAction->setShortcut(tr("Ctrl+S"));
    saveAction->setShortcuts(QKeySequence::Save);
    saveAction->setStatusTip(tr("Save a file"));
    connect(saveAction, SIGNAL(triggered()), this, SLOT( slotSave() ));

    /******** "另存为"动作 *********/
    saveAsAction = new QAction(QIcon(QDir::toNativeSeparators(":/save.png")), tr("Save&As"), this);
    saveAsAction->setShortcuts(QKeySequence::SaveAs);
    saveAsAction->setStatusTip(tr("Save as a file"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::slotSaveAs);

    /******** "复制"动作 *********/
    copyAction = new QAction(QIcon(QDir::toNativeSeparators(":/copy.png")), tr("&Copy"), this);
    copyAction->setShortcut(tr("Ctrl+C"));
    copyAction->setShortcuts(QKeySequence::Copy);
    copyAction->setStatusTip(tr("copy file"));

    /******** "剪切"动作 *********/
    cutAction = new QAction(QIcon(QDir::toNativeSeparators(":/cut.png")), tr("&Cut"), this);
    cutAction->setShortcut(tr("Ctrl+X"));
    cutAction->setShortcuts(QKeySequence::Cut);
    cutAction->setStatusTip(tr("cut file"));

    /******** "剪切"动作 *********/
    pasteAction = new QAction(QIcon(QDir::toNativeSeparators(":/paste.png")), tr("&Paste"), this);
    pasteAction->setShortcut(tr("Ctrl+V"));
    pasteAction->setShortcuts(QKeySequence::Paste);
    pasteAction->setStatusTip(tr("paste file"));

    /******** "删除"动作 *********/
    deleteAction = new QAction(QIcon(QDir::toNativeSeparators(":/delete.png")), tr("&Delete"), this);
    deleteAction->setShortcut(tr("Delete"));
    deleteAction->setStatusTip(tr("delete cell item from scene"));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(slotDeleteItem()));

    /******** "放大"动作 *********/
    zoomInAction = new QAction(QIcon(QDir::toNativeSeparators(":/zoomIn.png")), tr("Zoom&In"), this);
    zoomInAction->setShortcuts(QKeySequence::ZoomIn);
    zoomInAction->setStatusTip(tr("zoomIn file"));

    /******** "缩小"动作 *********/
    zoomOutAction = new QAction(QIcon(QDir::toNativeSeparators(":/zoomOut.png")), tr("Zoom&Out"), this);
    zoomOutAction->setShortcuts(QKeySequence::ZoomOut);
    zoomOutAction->setStatusTip(tr("zoomOut file"));

    /******** "时钟网格视图"动作 *********/
    toggleClockGridAction = new QAction(tr("Clock Grid"), this);
    toggleClockGridAction->setCheckable(true);
    toggleClockGridAction->setChecked(true);
    toggleClockGridAction->setStatusTip(tr("Show or hide clock grid"));
    connect(toggleClockGridAction, &QAction::toggled, this, &MainWindow::slotToggleClockGrid);

    /******** "高清视图"动作 *********/
    toggleHighQualityViewAction = new QAction(tr("HD"), this);
    toggleHighQualityViewAction->setCheckable(true);
    toggleHighQualityViewAction->setChecked(false);
    toggleHighQualityViewAction->setStatusTip(tr("Toggle high-quality rendering for the view"));
    connect(toggleHighQualityViewAction, &QAction::toggled, this, &MainWindow::slotToggleHighQualityView);

    /******** "状态栏视图"动作 *********/
    toggleStatusBarAction = new QAction(tr("Show Status Bar"), this);
    toggleStatusBarAction->setCheckable(true);
    toggleStatusBarAction->setChecked(true);  // 默认不勾选（状态栏隐藏）
    connect(toggleStatusBarAction, &QAction::toggled, this, &MainWindow::toggleStatusBar);

    /******** "全屏截图"动作 *********/
    captureFullScreen = new QAction(QIcon(QDir::toNativeSeparators(":/camera.png")), tr("Capture"), this);
    captureFullScreen->setStatusTip(tr("Capture Full Screen"));
    captureFullScreen->setShortcut(tr("Ctrl+P"));
    connect(captureFullScreen, &QAction::triggered, this, &MainWindow::slotCaptureFullWindow);

    /******** "仿真"动作 *********/
    // connect(simulationManager, &SimulationManager::simulationFinished, this, &MainWindow::onSimulationFinished);
    connect(this, &MainWindow::savedname, simulationManager, &SimulationManager::slotSavedname);//for 仿真文件名

    startBistableSimAction = new QAction(tr("&Start Bistable Simulation"), this);
    startBistableSimAction->setShortcut(tr("Ctrl+B"));
    startBistableSimAction->setStatusTip(tr("Start Bistable Simulation"));
    connect(startBistableSimAction, &QAction::triggered, simulationManager, &SimulationManager::slotBistableSim);

    startCoherenceSimAction = new QAction(tr("&Start Coherence Simulation"), this);
    startCoherenceSimAction->setShortcut(tr("Ctrl+C"));
    startCoherenceSimAction->setStatusTip(tr("Start Coherence Simulation"));
    connect(startCoherenceSimAction, &QAction::triggered, simulationManager, &SimulationManager::slotCoherenceSim);

    starBistableSimWithSelectiveAction = new QAction(tr("&Strar Bistable With Selective Simulation"));
    starBistableSimWithSelectiveAction->setShortcut(tr("Ctrl+G"));
    starBistableSimWithSelectiveAction->setStatusTip(tr("Strar Bistable With Selective Simulation"));
    connect(starBistableSimWithSelectiveAction, &QAction::triggered, simulationManager, &SimulationManager::slotBistableSimWithSelective);

    startCoherenceSimWithSelectiveAction = new QAction(tr("&Strar Coherence With Selective Simulation"));
    startCoherenceSimWithSelectiveAction->setShortcut(tr("Ctrl+H"));
    startCoherenceSimWithSelectiveAction->setStatusTip(tr("Strar Coherence With Selective Simulation"));
    connect(startCoherenceSimWithSelectiveAction, &QAction::triggered, simulationManager, &SimulationManager::slotCoherenceSimWithSelective);
    
    energyAnalysisAction = new QAction(tr("&Energy Analysis"), this);
    energyAnalysisAction->setShortcut(tr("Ctrl+E"));
    energyAnalysisAction->setStatusTip(tr("Start Energy Analysis"));
    connect(energyAnalysisAction, &QAction::triggered, simulationManager, &SimulationManager::slotEnergyAnalysis);

    SimWithSelective = new QAction(tr("&SimWithSelective"), this);
    SimWithSelective->setShortcut(tr("Ctrl+F"));
    SimWithSelective->setStatusTip(tr("SimWithSelective"));
    connect(SimWithSelective, &QAction::triggered, simulationManager, &SimulationManager::slotSimWithSelective);

    connect(this, &MainWindow::savedinputname, simulationManager, &SimulationManager::slotSavedinputname);
    
    /******** "添加元胞层"动作 *********/
    addLayerAction = new QAction(QIcon(QDir::toNativeSeparators(":/addLayerAction.png")), tr("&AddLayer"), this);
    addLayerAction->setStatusTip(tr("add cell layer"));
    connect(addLayerAction, SIGNAL(triggered()), this, SLOT( slotAddLayer() ));
    
    /******** "删除元胞层"动作 *********/
    deleteLayerAction = new QAction(QIcon(QDir::toNativeSeparators(":/deleteLayerAction.png")), tr("&DeleteLayer"), this);
    deleteLayerAction->setStatusTip(tr("delete cell layer"));
    connect(deleteLayerAction, SIGNAL(triggered()), this, SLOT( slotDeleteLayer() ));
    
    /******** "layer comboBox" *********/
    layerComboBox = new LayerComboBox(this); 
    connect(layerComboBox, SIGNAL(currentActiveIndex(int)), this, SLOT(slotLayerActiveChanged(int)));
    connect(layerComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotLayerActiveChanged(int)));
    
    /******** "clock comboBox" *********/
    clockComboBox = new QComboBox(this); 
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock0.png")), tr("Clock0"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock1.png")), tr("Clock1"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock2.png")), tr("Clock2"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock3.png")), tr("Clock3"));
    connect(clockComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT( slotClockIndexChanged(int) ));

    //view mode
    viewLabel =  new QLabel(tr("view mode : "));

    selectModeButton = new QToolButton;
    selectModeButton->setText(tr("Select"));
    selectModeButton->setCheckable(true);
    selectModeButton->setChecked(false);

    insertModeButton = new QToolButton;
    insertModeButton->setText(tr("Insert"));
    insertModeButton->setCheckable(true);
    insertModeButton->setChecked(false);

    dragModeButton = new QToolButton;
    dragModeButton->setText(tr("Drag"));
    dragModeButton->setCheckable(true);
    dragModeButton->setChecked(false);

    viewModeButtonGroup = new QButtonGroup(this);
    viewModeButtonGroup->setExclusive(true);
    viewModeButtonGroup->addButton(selectModeButton);
    viewModeButtonGroup->addButton(insertModeButton);
    viewModeButtonGroup->addButton(dragModeButton);

    connect(selectModeButton, &QToolButton::toggled, this, &MainWindow::viewModeChange);
    connect(insertModeButton, &QToolButton::toggled, this, &MainWindow::viewModeChange);
    connect(dragModeButton, &QToolButton::toggled, this, &MainWindow::viewModeChange);

    // verilog parse
    // thread = new QThread(this);
    // verilogHandler->moveToThread(thread);

    verParseButton = new QPushButton("Heuristic");
    verParseButton->setCheckable(true);
    connect(verParseButton, &QPushButton::toggled, verilogHandler, &VerilogHandler::handleParseVerilogFile);


    graphRenderButton = new QPushButton("Graph Render");
    graphRenderButton->setCheckable(true);
    connect(graphRenderButton, &QPushButton::toggled, verilogHandler, &VerilogHandler::handleGraphRender);

    //gate level mapping
    gateLevelMappingButton = new QPushButton("Gate Level Mapping");
    gateLevelMappingButton->setCheckable(true);
    connect(gateLevelMappingButton, &QPushButton::toggled, gateLevelMapping, [this](bool checked) {
        if (!checked) {
            return;
        }
        gateLevelMapping->parseGateLevelMappingFile();
    });

    // cell-level layout graph generation
    generateCellLevelLayoutGraph = new QAction(QIcon(QDir::toNativeSeparators(":/cameraColor.png")), tr("Save cell-level layout"), this);
    generateCellLevelLayoutGraph->setStatusTip(tr("Save cell-level layout"));
    connect(generateCellLevelLayoutGraph, &QAction::triggered, verilogHandler, &VerilogHandler::generateSVG);

    // force oriented algorithm
    forceOrientedAlgorithmButton = new QPushButton("Force Oriented Algorithm");
    forceOrientedAlgorithmButton->setCheckable(true);
    connect(forceOrientedAlgorithmButton, &QPushButton::toggled, verilogHandler, &VerilogHandler::slotForceOrientedAlgorithm);


}

void MainWindow::createMenus()
{
    /******** "文件"菜单 *********/
    fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    
    fileMenu->addSeparator();

    /******** "编辑"菜单 *********/
    editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(copyAction);
    editMenu->addAction(cutAction);
    editMenu->addAction(pasteAction);
    editMenu->addAction(deleteAction);
    editMenu->addAction(zoomInAction);
    editMenu->addAction(zoomOutAction);

    /******** "视图"菜单 *********/
    viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(toggleClockGridAction);
    viewMenu->addAction(toggleHighQualityViewAction);
    viewMenu->addAction(toggleStatusBarAction);
    viewMenu->addAction(captureFullScreen);
    viewMenu->addAction(generateCellLevelLayoutGraph);

    /******** "工具"菜单 *********/
    toolsMenu = menuBar()->addMenu("&Tools");

    /******** "仿真"菜单 *********/
    simulationMenu = menuBar()->addMenu("&Simulation");
    simulationMenu->addAction(startBistableSimAction);
    simulationMenu->addAction(starBistableSimWithSelectiveAction);
    // simulationMenu = menuBar()->addMenu("&Coherence Simulation");
    simulationMenu->addAction(startCoherenceSimAction);
    simulationMenu->addAction(startCoherenceSimWithSelectiveAction);
    // simulationMenu = menuBar()->addMenu("&Energy Analysis");
    simulationMenu->addAction(energyAnalysisAction);

    simulationMenu->addAction(SimWithSelective);

    /******** "帮助"菜单 *********/
    helpMenu = menuBar()->addMenu("&Help");

}

void MainWindow::createToolBars()
{
    /******** "文件"工具栏 *********/
    fileTool = addToolBar("File");
    fileTool->addAction(newAction);
    fileTool->addAction(openAction);
    fileTool->addAction(saveAction);

    /******** "编辑"工具栏 *********/
    editTool = addToolBar("Edit");
    editTool->addAction(copyAction);
    editTool->addAction(cutAction);
    editTool->addAction(pasteAction);
    editTool->addAction(deleteAction);

    editTool->addAction(zoomInAction);
    editTool->addAction(zoomOutAction);

    /******** "layers"工具栏 *********/
    layersTool = addToolBar("Layers");
    layersTool->addAction(addLayerAction);
    layersTool->addAction(deleteLayerAction);
    layersTool->addWidget(layerComboBox);

    /******** "clock"工具栏 *********/
    clockTool = addToolBar("Clock");
    clockTool->addWidget(clockComboBox);

    /* viewTool*/
    viewTool = addToolBar("view");
    viewTool->addWidget(viewLabel);
    viewTool->addWidget(selectModeButton);
    viewTool->addWidget(insertModeButton);
    viewTool->addWidget(dragModeButton);
    viewTool->addAction(toggleClockGridAction);
    viewTool->addAction(toggleHighQualityViewAction);


    /* verilog parse tool*/
    verilogTool = addToolBar("verilog parse");
    verilogTool->addWidget(verParseButton);
    verilogTool->addWidget(graphRenderButton);
    verilogTool->addWidget(gateLevelMappingButton);
    verilogTool->addWidget(forceOrientedAlgorithmButton);
}
