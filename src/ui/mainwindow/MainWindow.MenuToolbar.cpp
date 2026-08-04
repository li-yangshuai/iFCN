#include "ui/mainwindow/MainWindow.h"
#include <QMenuBar>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QSizePolicy>
#include <QTemporaryFile>
#include <QToolButton>

namespace {
QWidget *createToolbarSpacer(QWidget *parent)
{
    auto *spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setStyleSheet(QStringLiteral("background: transparent;"));
    return spacer;
}

QToolButton *createWorkspaceToolButton(QWidget *parent,
                                       const QString &text,
                                       const QString &toolTip)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setAutoRaise(false);
    button->setMinimumSize(58, 28);
    return button;
}
} // namespace

bool MainWindow::prepareSimulationInput()
{
    QTemporaryFile snapshot(
            QDir::temp().filePath(QStringLiteral("ifcn_simulation_XXXXXX.qca")));
    snapshot.setAutoRemove(false);
    if (!snapshot.open()) {
        printToStatusBar(tr("Simulation failed: cannot create a temporary QCA snapshot."));
        return false;
    }

    const QString snapshotFileName = snapshot.fileName();
    snapshot.close();
    if (!saveFile(snapshotFileName, false, false)) {
        QFile::remove(snapshotFileName);
        return false;
    }

    QString resultBasePath;
    if (!curFile.isEmpty() && curFile != tr("Unnamed")) {
        const QFileInfo sourceInfo(curFile);
        resultBasePath = sourceInfo.absoluteDir().filePath(
                sourceInfo.completeBaseName());
    } else {
        const QFileInfo snapshotInfo(snapshotFileName);
        resultBasePath = snapshotInfo.absoluteDir().filePath(
                QStringLiteral("untitled_") + snapshotInfo.completeBaseName());
    }

    if (!simulationManager->setSimulationInputFile(
                snapshotFileName, resultBasePath, snapshotFileName)) {
        QFile::remove(snapshotFileName);
        return false;
    }

    printToStatusBar(tr("Simulation input snapshot generated from the current canvas."));
    return true;
}

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
    connect(copyAction, &QAction::triggered, this, &MainWindow::slotCopyItems);

    /******** "剪切"动作 *********/
    cutAction = new QAction(QIcon(QDir::toNativeSeparators(":/cut.png")), tr("&Cut"), this);
    cutAction->setShortcut(tr("Ctrl+X"));
    cutAction->setShortcuts(QKeySequence::Cut);
    cutAction->setStatusTip(tr("cut file"));
    connect(cutAction, &QAction::triggered, this, &MainWindow::slotCutItems);

    /******** "剪切"动作 *********/
    pasteAction = new QAction(QIcon(QDir::toNativeSeparators(":/paste.png")), tr("&Paste"), this);
    pasteAction->setShortcut(tr("Ctrl+V"));
    pasteAction->setShortcuts(QKeySequence::Paste);
    pasteAction->setStatusTip(tr("paste file"));
    connect(pasteAction, &QAction::triggered, this, &MainWindow::slotPasteItems);

    /******** "删除"动作 *********/
    deleteAction = new QAction(QIcon(QDir::toNativeSeparators(":/delete.png")), tr("&Delete"), this);
    deleteAction->setShortcut(tr("Delete"));
    deleteAction->setStatusTip(tr("delete cell item from scene"));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(slotDeleteItem()));

    undoAction = new QAction(tr("&Undo"), this);
    undoAction->setShortcuts(QKeySequence::Undo);
    undoAction->setStatusTip(tr("undo last edit"));
    connect(undoAction, &QAction::triggered, this, &MainWindow::slotUndo);

    redoAction = new QAction(tr("&Redo"), this);
    redoAction->setShortcuts(QKeySequence::Redo);
    redoAction->setStatusTip(tr("redo last undone edit"));
    connect(redoAction, &QAction::triggered, this, &MainWindow::slotRedo);

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
    connect(simulationManager, &SimulationManager::simulationFinished,
            this, &MainWindow::onSimulationFinished);
    connect(simulationManager, &SimulationManager::simulationFailed,
            this, &MainWindow::onSimulationFailed);
    connect(this, &MainWindow::savedname, simulationManager, &SimulationManager::slotSavedname);//for 仿真文件名
    connect(simulationManager, &SimulationManager::energyAnalysisFinished,
            this, &MainWindow::onEnergyAnalysisFinished);
    connect(simulationManager, &SimulationManager::energyAnalysisFailed,
            this, &MainWindow::onEnergyAnalysisFailed);
    connect(simulationManager, &SimulationManager::operationStarted,
            customStatusBar, &CustomStatusBar::startOperation);
    connect(simulationManager, &SimulationManager::operationProgress,
            customStatusBar, &CustomStatusBar::updateOperation);
    connect(simulationManager, &SimulationManager::operationFinished,
            customStatusBar, &CustomStatusBar::finishOperation);
    connect(simulationManager, &SimulationManager::operationFailed,
            customStatusBar, &CustomStatusBar::failOperation);
    connect(verilogHandler, &VerilogHandler::operationStarted,
            customStatusBar, &CustomStatusBar::startOperation);
    connect(verilogHandler, &VerilogHandler::operationProgress,
            customStatusBar, &CustomStatusBar::updateOperation);
    connect(verilogHandler, &VerilogHandler::operationFinished,
            customStatusBar, &CustomStatusBar::finishOperation);
    connect(verilogHandler, &VerilogHandler::operationFailed,
            customStatusBar, &CustomStatusBar::failOperation);

    startBistableSimAction = new QAction(tr("&Start Bistable Simulation"), this);
    startBistableSimAction->setShortcut(tr("Ctrl+B"));
    startBistableSimAction->setStatusTip(tr("Start Bistable Simulation"));
    connect(startBistableSimAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotBistableSim();
        }
    });

    startAcceleratedBistableSimAction = new QAction(
            tr("Start &Accelerated Bistable Simulation"), this);
    startAcceleratedBistableSimAction->setShortcut(tr("Ctrl+Alt+B"));
    startAcceleratedBistableSimAction->setStatusTip(
            tr("Start strict-equivalent accelerated Bistable simulation"));
    connect(startAcceleratedBistableSimAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotAcceleratedBistableSim();
        }
    });

    startCoherenceSimAction = new QAction(tr("&Start Coherence Simulation"), this);
    startCoherenceSimAction->setShortcut(tr("Ctrl+Shift+C"));
    startCoherenceSimAction->setStatusTip(tr("Start Coherence Simulation"));
    connect(startCoherenceSimAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotCoherenceSim();
        }
    });

    startAcceleratedCoherenceSimAction = new QAction(
            tr("Start A&ccelerated Coherence Simulation"), this);
    startAcceleratedCoherenceSimAction->setShortcut(tr("Ctrl+Alt+C"));
    startAcceleratedCoherenceSimAction->setStatusTip(
            tr("Start strict-equivalent accelerated Coherence simulation"));
    connect(startAcceleratedCoherenceSimAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotAcceleratedCoherenceSim();
        }
    });

    starBistableSimWithSelectiveAction = new QAction(tr("&Start Bistable With Selective Simulation"));
    starBistableSimWithSelectiveAction->setShortcut(tr("Ctrl+G"));
    starBistableSimWithSelectiveAction->setStatusTip(tr("Start Bistable With Selective Simulation"));
    connect(starBistableSimWithSelectiveAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotBistableSimWithSelective();
        }
    });

    startCoherenceSimWithSelectiveAction = new QAction(tr("&Start Coherence With Selective Simulation"));
    startCoherenceSimWithSelectiveAction->setShortcut(tr("Ctrl+H"));
    startCoherenceSimWithSelectiveAction->setStatusTip(tr("Start Coherence With Selective Simulation"));
    connect(startCoherenceSimWithSelectiveAction, &QAction::triggered, this, [this]() {
        if (prepareSimulationInput()) {
            simulationManager->slotCoherenceSimWithSelective();
        }
    });

    energyAnalysisAction = new QAction(tr("&Energy Analysis"), this);
    energyAnalysisAction->setShortcut(tr("Ctrl+E"));
    energyAnalysisAction->setStatusTip(tr("Start Energy Analysis"));
    connect(energyAnalysisAction, &QAction::triggered, this, &MainWindow::slotEnergyAnalysis);

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
    layerComboBox->setObjectName(QStringLiteral("layerComboBox"));
    layerComboBox->setMinimumSize(148, 30);
    layerComboBox->setMaximumWidth(210);
    connect(layerComboBox, SIGNAL(currentActiveIndex(int)), this, SLOT(slotLayerActiveChanged(int)));
    connect(layerComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(slotLayerActiveChanged(int)));
    
    /******** "clock comboBox" *********/
    clockComboBox = new QComboBox(this); 
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock0.png")), tr("Clock0"), 0);
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock1.png")), tr("Clock1"), 1);
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock2.png")), tr("Clock2"), 2);
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock3.png")), tr("Clock3"), 3);
    clockComboBox->addItem(tr("No Phase (-1)"), -1);
    connect(clockComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT( slotClockIndexChanged(int) ));

    //view mode
    viewLabel = new QLabel(tr("Mode"));
    viewLabel->setObjectName(QStringLiteral("viewModeLabel"));
    viewLabel->setMinimumHeight(28);
    viewLabel->setStyleSheet(QStringLiteral("background: transparent;"));

    selectModeButton = new QToolButton;
    selectModeButton->setText(tr("Select"));
    selectModeButton->setCheckable(true);
    selectModeButton->setChecked(true);

    insertModeButton = new QToolButton;
    insertModeButton->setText(tr("Insert"));
    insertModeButton->setCheckable(true);
    insertModeButton->setChecked(false);

    dragModeButton = new QToolButton;
    dragModeButton->setText(tr("Drag"));
    dragModeButton->setCheckable(true);
    dragModeButton->setChecked(false);

    auto configureViewModeButton = [](QToolButton *button) {
        button->setMinimumSize(50, 28);
        button->setAutoRaise(false);
    };
    configureViewModeButton(selectModeButton);
    configureViewModeButton(insertModeButton);
    configureViewModeButton(dragModeButton);

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

    placeRouteMenu = new QMenu(tr("Place && Route"), this);

    placeRouteMenu->addSection(tr("Layout && Routing"));

    auto *heuristicAction = placeRouteMenu->addAction(tr("Heuristic P&&R"));
    heuristicAction->setStatusTip(tr("Run heuristic placement and routing"));
    connect(heuristicAction, &QAction::triggered,
            verilogHandler, &VerilogHandler::handleParseVerilogFile);

    auto *normalGraphAction = placeRouteMenu->addAction(tr("2DDWave Fixed-Clock P&&R"));
    normalGraphAction->setStatusTip(
        tr("Run Normal Graph Draw under the fixed, full-coverage 2DDWave clock template"));
    connect(normalGraphAction, &QAction::triggered,
            verilogHandler, &VerilogHandler::handleNormalGraphDrawLayout);

    auto *graphAction = placeRouteMenu->addAction(tr("Compact Graph Draw P&&R (recommended)"));
    graphAction->setStatusTip(
        tr("Run area-first graph drawing, integrated phase-aware routing, and legality-checked compaction"));
    connect(graphAction, &QAction::triggered,
            verilogHandler, &VerilogHandler::handleGraphRender);

    placeRouteButton = new QToolButton(this);
    placeRouteButton->setObjectName(QStringLiteral("primaryAlgorithmButton"));
    placeRouteButton->setDefaultAction(graphAction);
    placeRouteButton->setText(tr("Compact Graph Draw"));
    placeRouteButton->setMenu(placeRouteMenu);
    placeRouteButton->setPopupMode(QToolButton::MenuButtonPopup);
    placeRouteButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    placeRouteButton->setMinimumWidth(156);
    placeRouteButton->setToolTip(tr("Run Compact Graph Draw; use the arrow for Heuristic or fixed-clock 2DDWave P&R"));
    connect(verilogHandler, &VerilogHandler::operationStarted,
            placeRouteButton, [this](const QString &, const QString &) {
        if (placeRouteButton != nullptr) {
            placeRouteButton->setEnabled(false);
        }
        if (placeRouteMenu != nullptr) {
            placeRouteMenu->menuAction()->setEnabled(false);
        }
    });
    connect(verilogHandler, &VerilogHandler::operationFinished,
            placeRouteButton, [this](const QString &) {
        if (placeRouteButton != nullptr) {
            placeRouteButton->setEnabled(true);
        }
        if (placeRouteMenu != nullptr) {
            placeRouteMenu->menuAction()->setEnabled(true);
        }
    });
    connect(verilogHandler, &VerilogHandler::operationFailed,
            placeRouteButton, [this](const QString &) {
        if (placeRouteButton != nullptr) {
            placeRouteButton->setEnabled(true);
        }
        if (placeRouteMenu != nullptr) {
            placeRouteMenu->menuAction()->setEnabled(true);
        }
    });

    // cell-level layout graph generation
    generateCellLevelLayoutGraph = new QAction(QIcon(QDir::toNativeSeparators(":/cameraColor.png")), tr("Save cell-level layout"), this);
    generateCellLevelLayoutGraph->setStatusTip(tr("Save cell-level layout as SVG or cropped PDF"));
    connect(generateCellLevelLayoutGraph, &QAction::triggered, verilogHandler, &VerilogHandler::generateSVG);

    contractCellLevelIoAction = new QAction(tr("Contract Cell-level IO"), this);
    contractCellLevelIoAction->setCheckable(true);
    contractCellLevelIoAction->setStatusTip(
        tr("Toggle reversible IO contraction for the current cell-level layout"));
    connect(contractCellLevelIoAction, &QAction::toggled,
            this, [this](bool checked) {
                setCellLevelIoContractionEnabled(checked);
            });
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
    editMenu->addSeparator();
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);

    /******** "视图"菜单 *********/
    viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(toggleClockGridAction);
    viewMenu->addAction(toggleHighQualityViewAction);
    viewMenu->addAction(toggleStatusBarAction);
    viewMenu->addAction(captureFullScreen);
    viewMenu->addAction(generateCellLevelLayoutGraph);

    /******** "工具"菜单 *********/
    toolsMenu = menuBar()->addMenu("&Tools");
    if (placeRouteMenu != nullptr) {
        toolsMenu->addMenu(placeRouteMenu);
        toolsMenu->addSeparator();
    }
    toolsMenu->addAction(contractCellLevelIoAction);
    toolsMenu->addSeparator();
    if (verilogSourceDock != nullptr ||
        circuitSchematicDock != nullptr ||
        phaseCodecDock != nullptr ||
        layoutInfoDock != nullptr ||
        structure3DDock != nullptr) {
        if (verilogSourceDock != nullptr) {
            toolsMenu->addAction(verilogSourceDock->toggleViewAction());
        }
        if (circuitSchematicDock != nullptr) {
            toolsMenu->addAction(circuitSchematicDock->toggleViewAction());
        }
        if (phaseCodecDock != nullptr) {
            toolsMenu->addAction(phaseCodecDock->toggleViewAction());
        }
        if (layoutInfoDock != nullptr) {
            toolsMenu->addAction(layoutInfoDock->toggleViewAction());
        }
        if (structure3DDock != nullptr) {
            toolsMenu->addAction(structure3DDock->toggleViewAction());
        }
    }

    /******** "仿真"菜单 *********/
    simulationMenu = menuBar()->addMenu("&Simulation");
    simulationMenu->addAction(startBistableSimAction);
    simulationMenu->addAction(startAcceleratedBistableSimAction);
    simulationMenu->addAction(starBistableSimWithSelectiveAction);
    // simulationMenu = menuBar()->addMenu("&Coherence Simulation");
    simulationMenu->addAction(startCoherenceSimAction);
    simulationMenu->addAction(startAcceleratedCoherenceSimAction);
    simulationMenu->addAction(startCoherenceSimWithSelectiveAction);
    // simulationMenu = menuBar()->addMenu("&Energy Analysis");
    simulationMenu->addAction(energyAnalysisAction);

    simulationMenu->addAction(SimWithSelective);
}

void MainWindow::createToolBars()
{
    /******** "文件"工具栏 *********/
    fileTool = addToolBar("File");
    fileTool->setObjectName(QStringLiteral("fileToolBar"));
    fileTool->setFloatable(false);
    fileTool->setMovable(false);
    fileTool->setIconSize(QSize(20, 20));
    fileTool->addAction(newAction);
    fileTool->addAction(openAction);
    fileTool->addAction(saveAction);

    /******** "编辑"工具栏 *********/
    editTool = addToolBar("Edit");
    editTool->setObjectName(QStringLiteral("editToolBar"));
    editTool->setFloatable(false);
    editTool->setMovable(false);
    editTool->setIconSize(QSize(20, 20));
    editTool->addAction(copyAction);
    editTool->addAction(cutAction);
    editTool->addAction(pasteAction);
    editTool->addAction(deleteAction);
    editTool->addAction(undoAction);
    editTool->addAction(redoAction);

    auto *encodeButton = createWorkspaceToolButton(this,
                                                   tr("Encode"),
                                                   tr("Encode clock regions"));
    encodeButton->setObjectName(QStringLiteral("workspaceEncodeButton"));
    connect(encodeButton, &QToolButton::clicked,
            this, &MainWindow::slotEncodeClockRegions);

    auto *structureButton = createWorkspaceToolButton(this,
                                                      tr("3D"),
                                                      tr("Show layered 3D clock and cell structure"));
    structureButton->setObjectName(QStringLiteral("workspaceStructureButton"));
    connect(structureButton, &QToolButton::clicked,
            this, &MainWindow::showStructure3DView);

    ioContractionCheckBox = new QCheckBox(tr("IO Contract"), this);
    ioContractionCheckBox->setObjectName(QStringLiteral("workspaceIoContractCheckBox"));
    ioContractionCheckBox->setToolTip(
        tr("Checked: contract IO stems and remove obsolete crossovers; unchecked: restore the original layout"));
    ioContractionCheckBox->setMinimumWidth(100);
    connect(ioContractionCheckBox, &QCheckBox::toggled,
            this, [this](bool checked) {
                setCellLevelIoContractionEnabled(checked);
            });

    /******** central workspace toolbar *********/
    addToolBarBreak(Qt::TopToolBarArea);
    workspaceTool = addToolBar("Workspace");
    workspaceTool->setObjectName(QStringLiteral("workspaceToolBar"));
    workspaceTool->setAllowedAreas(Qt::TopToolBarArea | Qt::BottomToolBarArea);
    workspaceTool->setFloatable(false);
    workspaceTool->setMovable(false);
    workspaceTool->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    workspaceTool->addWidget(placeRouteButton);
    workspaceTool->addWidget(ioContractionCheckBox);
    workspaceTool->addSeparator();
    workspaceTool->addWidget(viewLabel);
    workspaceTool->addWidget(selectModeButton);
    workspaceTool->addWidget(insertModeButton);
    workspaceTool->addWidget(dragModeButton);
    workspaceTool->addAction(toggleClockGridAction);
    workspaceTool->addAction(toggleHighQualityViewAction);
    workspaceTool->addSeparator();
    workspaceTool->addAction(addLayerAction);
    workspaceTool->addAction(deleteLayerAction);
    workspaceTool->addWidget(layerComboBox);
    workspaceTool->addWidget(clockComboBox);
    workspaceTool->addSeparator();
    workspaceTool->addWidget(encodeButton);
    workspaceTool->addWidget(structureButton);
    workspaceTool->addWidget(createToolbarSpacer(workspaceTool));

    layersTool = workspaceTool;
    clockTool = workspaceTool;
    viewTool = workspaceTool;
    verilogTool = workspaceTool;
}
