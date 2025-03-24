#include "MainWindow.h"


#include <QPen>
#include <QBrush>
#include <QMenuBar>
#include <QGraphicsLineItem>
#include <QImage>
#include <QMessageBox>
#include <QFileDialog>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QStatusBar>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QThread>

namespace {
    const QString ShowGrid("ShowGrid");
    const QString MostRecentFile("MostRecentFile");

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), 
                                          currentMode(EditMode::Select),
                                          simulationManager(new SimulationManager),
                                          verilogHandler(new VerilogHandler(this))
{
    createViewAndScene();
    createActions();
    createMenus();
    createToolBars();
    initialDesign();
    createToolBox();



    connect(scene, SIGNAL(cellItemInserted(QCADCellItem *)), this, SLOT(slotCellItemInserted(QCADCellItem *)));
    // connect(scene, SIGNAL(clockPhaseInserted(QCADClockScheme *)), this, SLOT(slotQCADClockScheme(QCADClockScheme *)));

    QSettings settings;
    viewShowGridAction->setChecked(settings.value(ShowGrid, false).toBool());
    QString fileName = settings.value(MostRecentFile).toString();
    if(fileName.isEmpty() || fileName == tr("Unnamed"))
        setCurrentFile(QString());
    else {
        setCurrentFile(fileName);
        QTimer::singleShot(0, this, SLOT(loadFile(fileName)));
    }

}


MainWindow::~MainWindow()
{

}

void MainWindow::createViewAndScene()
{
    // layout
    centralWidget = new QWidget(this);  
    verticalLayout = new QVBoxLayout(centralWidget);
    splitter = new QSplitter(centralWidget);
    splitter->setOrientation(Qt::Horizontal);
    toolBox = new QToolBox(splitter);

    scene = new QCADScene(this); 
    scene->setSceneRect(QRectF(0,0,SCENE_WIDTH, SCENE_HEIGHT));
    // scene->setBackgroundBrush(Qt::black);
    view = new QCADView(this);
    view->setScene(scene);
    view->resize(800,600);

    splitter->addWidget(toolBox);
    splitter->addWidget(view);   
    verticalLayout->addWidget(splitter);
    this->setCentralWidget(centralWidget);
    view->centerOn(0, 0);

    customStatusBar = new CustomStatusBar(this);
    verticalLayout->addWidget(customStatusBar, 1);

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
    connect(openAction, SIGNAL(triggered()), this, SLOT( slotOpen() ));

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
    connect(saveAsAction, SIGNAL(triggered()), this, SLOT( slotSaveAs() ));

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

    /******** "网格线视图"动作 *********/
    viewShowGridAction = new QAction(tr("Show&Grid"), this);
    viewShowGridAction->setCheckable(true);
    viewShowGridAction->setChecked(false);
    connect(viewShowGridAction, SIGNAL(toggled(bool)), this, SLOT( slotViewShowGrid(bool) ));

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
    // connect(this, &MainWindow::savedname, simulationManager, &SimulationManager::savedname);//for 仿真文件名

    startBistableSimAction = new QAction(tr("&Start Bistable Simulation"), this);
    startBistableSimAction->setShortcut(tr("Ctrl+B"));
    startBistableSimAction->setStatusTip(tr("Start Bistable Simulation"));
    connect(startBistableSimAction, &QAction::triggered, simulationManager, &SimulationManager::slotBistableSim);

    startCoherenceSimAction = new QAction(tr("&Start Coherence Simulation"), this);
    startCoherenceSimAction->setShortcut(tr("Ctrl+C"));
    startCoherenceSimAction->setStatusTip(tr("Start Coherence Simulation"));

    sthread = new QThread;
    simulationManager->moveToThread(sthread);
    connect(sthread, &QThread::started, simulationManager, &SimulationManager::slotCoherenceSim);
    connect(startCoherenceSimAction, &QAction::triggered, this, [=] () {
        sthread->start();
        simulationManager->curfileName = simfileName;
        ;});
    qDebug() << "主线程对象的地址: " << QThread::currentThread();
    connect(simulationManager, &SimulationManager::simulationFinished, this, &MainWindow::onSimulationFinished);
    connect(simulationManager, &SimulationManager::simulationFinished, sthread, &QThread::quit);
    connect(sthread, &QThread::finished, sthread, &QThread::deleteLater);
    // connect(startCoherenceSimAction, &QAction::triggered, simulationManager, &SimulationManager::slotCoherenceSim);

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

    connect(this, &MainWindow::savedinputname, simulationManager, &SimulationManager::savedinputname);
    
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
    
    /******** "clock comboBox" *********/
    clockComboBox = new QComboBox(this); 
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock0.png")), tr("Clock0"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock1.png")), tr("Clock1"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock2.png")), tr("Clock2"));
    clockComboBox->addItem(QIcon(QDir::toNativeSeparators(":/clock3.png")), tr("Clock3"));
    connect(clockComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT( slotClockIndexChanged(int) ));

    //show Grid
    checkBox = new QCheckBox("show grid");
    checkBox->setCheckable(true);
    checkBox->setChecked(false);

    connect(checkBox, &QCheckBox::stateChanged,[=](int state){ scene->setGridVisible(state != Qt::Unchecked);});

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
    thread = new QThread(this);
    verilogHandler->moveToThread(thread);

    verParseButton = new QPushButton("Heuristic");
    verParseButton->setCheckable(true);
    connect(thread, &QThread::started, verilogHandler, &VerilogHandler::handleParseVerilogFile);
    connect(verParseButton, &QPushButton::toggled, this, [=] () {
        thread->start();});


    graphRenderButton = new QPushButton("Graph Render");
    graphRenderButton->setCheckable(true);
    connect(graphRenderButton, &QPushButton::toggled, verilogHandler, &VerilogHandler::handleGraphRender);

    generateCellLevelLayoutGraph = new QAction(QIcon(QDir::toNativeSeparators(":/cameraColor.png")), tr("Save cell-level layout"), this);
    generateCellLevelLayoutGraph->setStatusTip(tr("Save cell-level layout"));
    connect(generateCellLevelLayoutGraph, &QAction::triggered, verilogHandler, &VerilogHandler::generateSVG);
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
    viewMenu->addAction(viewShowGridAction);
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
    viewTool->addWidget(checkBox);
    viewTool->addWidget(viewLabel);
    viewTool->addWidget(selectModeButton);
    viewTool->addWidget(insertModeButton);
    viewTool->addWidget(dragModeButton);


    /* verilog parse tool*/
    verilogTool = addToolBar("verilog parse");
    verilogTool->addWidget(verParseButton);
    verilogTool->addWidget(graphRenderButton);
}


void MainWindow::createToolBox(){
    buttonGroup = new QButtonGroup(this);
    buttonGroup->setExclusive(false);
    connect(buttonGroup, SIGNAL(buttonClicked(int)), this, SLOT(buttonGroupClicked(int)));  
    QGridLayout *layout = new QGridLayout;
    layout->addWidget(createCellWidget(tr("Normal"), CellType::NormalCell), 0,0);
    layout->addWidget(createCellWidget(tr("Fixed_0"), CellType::FixedCell_0), 0,1);
    layout->addWidget(createCellWidget(tr("Fixed_1"), CellType::FixedCell_1), 1,0);
    layout->addWidget(createCellWidget(tr("Input"), CellType::InputCell), 1,1);
    layout->addWidget(createCellWidget(tr("Output"), CellType::OutputCell), 2,0);
    layout->addWidget(createCellWidget(tr("Vertical"), CellType::VerticalCell), 2,1);
    layout->addWidget(createCellWidget(tr("Crossover"), CellType::CrossoverCell), 3,0);
    layout->setRowStretch(4, 10);
    layout->setColumnStretch(4, 10);
    QWidget *itemWidget = new QWidget;
    itemWidget->setLayout(layout);
    toolBox->setMinimumWidth(itemWidget->sizeHint().width());
    toolBox->addItem(itemWidget, tr("Basic QCA Cell"));

    //
    // qcaDevidceGroup = new QButtonGroup(this);
    // QGridLayout *qcaDeviceLayout = new QGridLayout;

    //
    clockSchemeGroup = new QButtonGroup(this);
    connect(clockSchemeGroup, SIGNAL(buttonClicked(QAbstractButton*)),  this, SLOT(slotClockSchemeGroupClicked(QAbstractButton*)));    //还没写
    QGridLayout *clockSchemeLayout = new QGridLayout;
    // clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Select"),":/csSelect.svg")     ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Clean"),":/cleanCS.svg")       ,0,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("Custom"),":/custom.svg")       ,0,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("ONE-D"),":/2dd.svg")           ,1,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("2DDwave"),":/2dd.svg")         ,1,1);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("USE"),":/use.svg")             ,2,0);
    clockSchemeLayout->addWidget(createClockSchemeWidget(tr("RES"),":/res.svg")             ,2,1);
    clockSchemeLayout->setRowStretch(3, 8);
    clockSchemeLayout->setColumnStretch(3, 8);
    QWidget *clockSchemeWidget = new QWidget;
    clockSchemeWidget->setLayout(clockSchemeLayout);
    toolBox->addItem(clockSchemeWidget, tr("Clock Schemes"));

}

void MainWindow::buttonGroupClicked(int id){
    QList<QAbstractButton *> buttons = buttonGroup->buttons();
    for (QAbstractButton *button : buttons) {
        if (buttonGroup->button(id) != button)
            button->setChecked(false);
    }
    scene->setItemType(CellType(id));
    // scene->setMode(QCADScene::InsertCell);
}

void MainWindow::slotClockSchemeGroupClicked(QAbstractButton* button){
    QList<QAbstractButton *> buttons = clockSchemeGroup->buttons();
    for (QAbstractButton *myButton: buttons) {
        if (myButton != button)
            button->setChecked(false);
    }
    // reset
    QString text = button->text();
    if(text == tr("Clean")){
        scene->clearPhaseRecord();
    }else if(text == tr("Custom")){
        //待设计
    }else if(text == tr("ONE-D")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(ONEDIMEN_CLOKC_SCHEME);
    }else if(text == tr("2DDwave")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(TDDWAVE_CLOCK_SCHEME);
    }else if(text == tr("USE")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(USE_CLOKC_SCHEME);
    }else if(text == tr("RES")){
        scene->clearPhaseRecord();
        scene->placeClockScheme(RES_CLOKC_SCHEME);
    }
    update();
    // setDirty(true);
}


QWidget* MainWindow::createCellWidget(const QString &text, CellType type){
    int clockIdx = this->clockComboBox->currentIndex();
    QCADCellItem item(type);
    QIcon icon(item.image(clockIdx));
    QToolButton *button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(20, 20));
    button->setCheckable(true);
    buttonGroup->addButton(button, int(type));

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    layout->addWidget(new QLabel(text), 1, 0, Qt::AlignCenter);

    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}

QWidget* MainWindow::createClockSchemeWidget(const QString &text, const QString &image){
    QToolButton *button = new QToolButton;
    button->setText(text);
    button->setIcon(QIcon(image));
    button->setIconSize(QSize(50,50));
    button->setCheckable(true);
    clockSchemeGroup->addButton(button);

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(button, 0, 0, Qt::AlignHCenter);
    layout->addWidget(new QLabel(text), 1, 0, Qt::AlignCenter);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);

    return widget;
}


void MainWindow::initialDesign()
{
    /******** 初始化layer comboBox, 添加主层"Main Cell Layer" *********/
    layerComboBox->AddItem(tr("Main Cell Layer"), true); 

    // int idx = layerComboBox->GetNumRows() - 1;

    QVector<QGraphicsItem*> cells_layer;
    layers.push_back(cells_layer); 
    // layers[idx]->setZValue(idx);     //由layerComboBox的索引号决定
    // layers[idx]->setVisible(true);

    // scene->addItem(layers[idx]);
    //qDebug() << tr("layer:") << idx << tr(" zValue:") << layers[idx]->zValue();
    setDirty(true);
}

void MainWindow::loadFile(const QString &fileName)
{
    QCADesign design;
    parse_design(fileName.toStdString(), design);

    int layer_size = simon::size(design);

    for(int i = 0; i < layer_size; ++i)
    {
        auto &layer = simon::layers(design)[i];
        if(i == 0)
        {
            if(layers.size() == 0)
            {
                layerComboBox->AddItem(QString::fromStdString( simon::description(layer) ), true); 

                QVector<QGraphicsItem*> cells_layer;

                layers.push_back(cells_layer); 
                // layers[i]->setZValue(i);     //由layerComboBox的索引号决定
                // layers[i]->setVisible(true);

                // scene->addItem(layers[i]);
            }
            else    //layers.size() == 1
                layerComboBox->GetItem(0)->setText( QString::fromStdString( simon::description(layer) ) );
        }
        else
        {
            layerComboBox->AddItem(QString::fromStdString( simon::description(layer) ), true); 

            QVector<QGraphicsItem*> cells_layer;
            layers.push_back(cells_layer); 
            // layers[i]->setZValue(i);     //由layerComboBox的索引号决定
            // layers[i]->setVisible(true);

            // scene->addItem(layers[i]);
        }

        for(auto &cell : layer)
        {
            QCADCellItem *cellItem = new QCADCellItem(cell);
            cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加
            cellItem->setZValue(i);     //由layerComboBox的索引号决定
            cellItem->setVisible(true);
            scene->addItem(cellItem);
            layers[i].push_back(cellItem);
        }
    }

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("Loaded %1").arg(fileName), 2000);
    emit savedname(fileName);
    simfileName = fileName;//for 仿真文件名
}

void MainWindow::printToStatusBar(QString &message)
{
    customStatusBar->addMessage(message);
    QCoreApplication::processEvents();
}


bool MainWindow::saveFile(const QString &fileName)
{
    QFile file(fileName);
    if(!file.open(QFile::WriteOnly))
    {
        QMessageBox::warning( this, tr("警告！"), tr("不能写入文件 %1:\n%2.")
                .arg( QDir::toNativeSeparators(fileName), file.errorString() ) );
        return false;
    }

    QTextStream out(&file);
    /*********文本保存************/
    //version信息
    out << "[VERSION]\n";
    out << "qcadesigner_version=2.000000\n";
    out << "[#VERSION]\n";

    //design信息
    out << "[TYPE:DESIGN]\n";

    //drawing layer
    out << "[TYPE:QCADLayer]\n";
    out << "type=3\n";
    out << "status=1\n";
    out << "pszDescription=Drawing Layer\n";
    out << "[#TYPE:QCADLayer]\n";

    //substrate
    out << "[TYPE:QCADLayer]\n";
    out << "type=0\n";
    out << "status=1\n";
    out << "pszDescription=Substrate\n";
    out << "[TYPE:QCADSubstrate]\n";
    out << "[TYPE:QCADStretchyObject]\n";
    out << "[TYPE:QCADDesignObject]\n";
    out << "x=3000.000000\n";
    out << "y=1500.000000\n";
    out << "bSelected=FALSE\n";
    out << "clr.red=65535\n";
    out << "clr.green=65535\n";
    out << "clr.blue=65535\n";
    out << "bounding_box.xWorld=0.000000\n";
    out << "bounding_box.yWorld=0.000000\n";
    out << "bounding_box.cxWorld=6000.000000\n";
    out << "bounding_box.cyWorld=3000.000000\n";
    out << "[#TYPE:QCADDesignObject]\n";
    out << "[#TYPE:QCADStretchyObject]\n";
    out << "grid_spacing=20.000000\n";
    out << "[#TYPE:QCADSubstrate]\n";
    out << "[#TYPE:QCADLayer]\n";
    //cell layers
    int layer_n = layers.size();
    for(int i = 0; i < layer_n; ++i)
    {
        out << "[TYPE:QCADLayer]\n";
        out << "type=1\n";
        out << "status=0\n";
        out << "pszDescription=" << layerComboBox->GetItem(i)->text() << "\n";

        //cells
        auto itemGroup = layers[i];
        // QList<QGraphicsItem *> items = itemGroup->childItems();
        for(QGraphicsItem *item: itemGroup)
        {
            const QCADCellItem *cell = static_cast<QCADCellItem *>(item);
            out << "[TYPE:QCADCell]\n";
            //designObject
            out << "[TYPE:QCADDesignObject]\n";
            out << "x=" << simon::x(*cell) << "\n";
            out << "y=" << simon::y(*cell) << "\n";
            out << "bSelected=FALSE\n";
            switch(simon::function(*cell)) {
                case FCNCellFunction::INPUT : 
                    out << "clr.red=0\n"; 
                    out << "clr.green=0\n"; 
                    out << "clr.blue=65535\n"; 
                    break;
                case FCNCellFunction::OUTPUT : 
                    out << "clr.red=65535\n"; 
                    out << "clr.green=65535\n"; 
                    out << "clr.blue=0\n"; 
                    break;
                case FCNCellFunction::FIXED : 
                    out << "clr.red=65535\n"; 
                    out << "clr.green=32768\n"; 
                    out << "clr.blue=0\n"; 
                    break;
                default :
                    switch(simon::timezone(*cell)) {
                        case 0 :
                            out << "clr.red=0\n"; 
                            out << "clr.green=65535\n"; 
                            out << "clr.blue=0\n"; 
                            break;
                        case 1 :
                            out << "clr.red=65535\n"; 
                            out << "clr.green=0\n"; 
                            out << "clr.blue=65535\n"; 
                            break;
                        case 2 :
                            out << "clr.red=0\n"; 
                            out << "clr.green=65535\n"; 
                            out << "clr.blue=65535\n"; 
                            break;
                        case 3 :
                            out << "clr.red=65535\n"; 
                            out << "clr.green=65535\n"; 
                            out << "clr.blue=65535\n"; 
                            break;
                        default:
                            out << "clr.red=0\n"; 
                            out << "clr.green=0\n"; 
                            out << "clr.blue=0\n"; 
                            break;
                    }
            }
            out << "bounding_box.xWorld=" << simon::x(*cell) - cell->boundingRect().width()/2 << "\n";
            out << "bounding_box.yWorld=" << simon::y(*cell) - cell->boundingRect().height()/2 << "\n";
            out << "bounding_box.cxWorld=" << cell->boundingRect().width() << "\n";
            out << "bounding_box.cyWorld=" << cell->boundingRect().height() << "\n";
            out << "[#TYPE:QCADDesignObject]\n";

            out << "cell_options.cxCell=" << simon::width(*cell) << "\n";
            out << "cell_options.cyCell=" << simon::height(*cell) << "\n";
            out << "cell_options.dot_diameter=5.000000\n";
            out << "cell_options.clock=" << simon::timezone(*cell) << "\n";
            switch (simon::cellMode(*cell))
            {
            case QCACellMode::VERTICAL:
                out << "cell_options.mode=QCAD_CELL_MODE_VERTICAL\n";  
                break;
            case QCACellMode::CLUSTER:
                out << "cell_options.mode=QCAD_CELL_MODE_CLUSTER\n";  
                break;
            case QCACellMode::CROSSOVER:
                out << "cell_options.mode=QCAD_CELL_MODE_CROSSOVER\n";  
                break;
            default:
                out << "cell_options.mode=QCAD_CELL_MODE_NORMAL\n";  
                break;
            }
            // out << "cell_options.mode=QCAD_CELL_MODE_NORMAL\n";              //warning: cell信息未存储
            switch(simon::function(*cell)) {
                case FCNCellFunction::NORMAL :
                    out << "cell_function=QCAD_CELL_NORMAL\n"; 
                    break;
                case FCNCellFunction::INPUT :
                    out << "cell_function=QCAD_CELL_INPUT\n"; 
                    break;
                case FCNCellFunction::OUTPUT :
                    out << "cell_function=QCAD_CELL_OUTPUT\n"; 
                    break;
                case FCNCellFunction::FIXED :
                    out << "cell_function=QCAD_CELL_FIXED\n"; 
                    break;
                case FCNCellFunction::LAST_FUNCTION :
                    out << "cell_function=QCAD_CELL_LAST_FUNCTION\n"; 
                    break;
                default : 
                    out << "cell_function=QCAD_CELL_NORMAL\n"; 
            }
            out << "number_of_dots=4\n"; 

            //cell dot
            for(int i=0; i<4; ++i)
            {
                out << "[TYPE:CELL_DOT]\n";
                out << "x=" << simon::x(*cell) + simon::x(cell->dots[i]) << "\n";
                out << "y=" << simon::y(*cell) + simon::y(cell->dots[i]) << "\n";
                out << "diameter=" << simon::diameter(cell->dots[i]) << "\n";
                out << "charge=" << simon::charge(cell->dots[i]) << "\n"; //charge值需要初始化
                out << "spin=" << simon::spin(cell->dots[i]) << "\n";
                out << "potential=" << simon::potential(cell->dots[i]) << "\n";
                out << "[#TYPE:CELL_DOT]\n";
            }

            //cell label
            if( simon::name(*cell) !="" )
            {
                out << "[TYPE:QCADLabel]\n";
                out << "[TYPE:QCADStretchyObject]\n";
                out << "[TYPE:QCADDesignObject]\n";
                out << "x=" << simon::x(*cell) << "\n";
                out << "y=" << simon::y(*cell)-20 << "\n";
                out << "bSelected=FALSE\n";
                switch(simon::function(*cell)) {
                    case FCNCellFunction::INPUT : 
                        out << "clr.red=0\n"; 
                        out << "clr.green=0\n"; 
                        out << "clr.blue=65535\n"; 
                        break;
                    case FCNCellFunction::OUTPUT : 
                        out << "clr.red=65535\n"; 
                        out << "clr.green=65535\n"; 
                        out << "clr.blue=0\n"; 
                        break;
                    case FCNCellFunction::FIXED : 
                        out << "clr.red=65535\n"; 
                        out << "clr.green=32768\n"; 
                        out << "clr.blue=0\n"; 
                        break;
                    default :
                        switch(simon::timezone(*cell)) {
                            case 0 :
                                out << "clr.red=0\n"; 
                                out << "clr.green=65535\n"; 
                                out << "clr.blue=0\n"; 
                                break;
                            case 1 :
                                out << "clr.red=65535\n"; 
                                out << "clr.green=0\n"; 
                                out << "clr.blue=65535\n"; 
                                break;
                            case 2 :
                                out << "clr.red=0\n"; 
                                out << "clr.green=65535\n"; 
                                out << "clr.blue=65535\n"; 
                                break;
                            case 3 :
                                out << "clr.red=65535\n"; 
                                out << "clr.green=65535\n"; 
                                out << "clr.blue=65535\n"; 
                                break;
                            default:
                                out << "clr.red=0\n"; 
                                out << "clr.green=0\n"; 
                                out << "clr.blue=0\n"; 
                                break;
                        }
                }
                out << "bounding_box.xWorld=" << simon::x(*cell) - 10 << "\n";
                out << "bounding_box.yWorld=" << simon::y(*cell) - 20 - 11 << "\n";
                out << "bounding_box.cxWorld=20\n"; //默认长为20，宽为22
                out << "bounding_box.cyWorld=22\n";
                out << "[#TYPE:QCADDesignObject]\n";

                out << "[#TYPE:QCADStretchyObject]\n";
                out << "psz=" << QString::fromStdString(simon::name(*cell)) << "\n";
                out << "[#TYPE:QCADLabel]\n";
            }
            
            out << "[#TYPE:QCADCell]\n";
        }

        out << "[#TYPE:QCADLayer]\n";
    }
    //添加 bus layout

    out << "[#TYPE:DESIGN]\n";


    /*********文本保存结束********/

    file.close();
    setCurrentFile(fileName);
    statusBar()->showMessage(tr("文本保存成功"), 2000);
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // 调用保存文件的操作
    if (maybeSave()) {
        QSettings settings;
        // 保存窗口的设置
        settings.setValue(MostRecentFile, windowFilePath());

        // 如果线程正在运行，先退出线程
        if (thread->isRunning()) {
            thread->quit();  // 请求线程退出
            thread->wait();  // 等待线程完成
        }

        event->accept();  // 允许窗口关闭
    } else {
        event->ignore();  // 如果取消了保存，则忽略窗口关闭事件
    }
}


void MainWindow::updateLayerAndCellZValue()
{
    int num = layers.size();
    for(int i = 0; i < num; ++i)
    {
        auto itemGroup = layers[i];
        for(QGraphicsItem* item :itemGroup){
            item->setZValue(i);
            simon::layer_index(*static_cast<QCADCellItem *>(item)) = i;
        }
    }
}

bool MainWindow::maybeSave()
{
    if(!isWindowModified())
        return true; 
    const QMessageBox::StandardButton ret 
        = QMessageBox::warning(this, tr("警告！"), tr("文档有更改\n""是否保存？"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    switch (ret) {
        case QMessageBox::Save :
            return slotSave();
        case QMessageBox::Cancel :
            return false;
        default :
            break;
    }
    return true;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = fileName;
    setDirty(false);

    QString shownName = curFile;
    if(curFile.isEmpty())
        shownName = "Unnamed";
    //setWindowFilePath(shownName);
    setWindowTitle(tr("%1[*] - Spars2.0-HFUT").arg(shownName));
}
        
void MainWindow::setDirty(bool on)
{
    //禁止其他页面响应
    setWindowModified(on);
    updateUi();
}

void MainWindow::updateUi()
{
    saveAction->setEnabled(isWindowModified());
    //更新Action状态
}

void MainWindow::slotNew()
{
    MainWindow *newMainWindow = new MainWindow;
    newMainWindow->show();
}

void MainWindow::slotOpen()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("打开文件"), "/home/zjxiao/Projects/fcnx", tr("QCA files (*.qca);;All file (*)"));
    if(!fileName.isEmpty())
    {
        if( (layers.size() == 0) || (layers.size() == 1 && layers[0].isEmpty()))
        {
            //QMessageBox::information(this, tr("Information"), tr("打开文件!"));
            loadFile(fileName);
            emit savedname(fileName);
            simfileName = fileName;//for 仿真文件名
        }
        else
        {
            MainWindow *newMainWindow = new MainWindow;
            newMainWindow->show();
            //QMessageBox::information(newMainWindow , tr("Information"), tr("打开文件!"));
            newMainWindow->loadFile(fileName);
        }
    }

}

bool MainWindow::slotSave()
{
    if(curFile.isEmpty() || curFile == tr("Unnamed"))
        return slotSaveAs();
    else
        return saveFile(curFile);
}

bool MainWindow::slotSaveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("文件另存为"), ".", tr("QCA files (*.qca);;All file (*)"));
    if(fileName.isEmpty())
        return false;
    if(!fileName.toLower().endsWith(".qca"))
        fileName += ".qca";
    emit savedname(fileName);
    simfileName = fileName;//for 仿真文件名
    return saveFile(fileName);


}
        
void MainWindow::slotAddLayer()
{
    layerComboBox->AddItem(tr("New Layer"), true); 
    // int idx = layerComboBox->GetNumRows() - 1;

    QVector<QGraphicsItem*> cells_layer;

    layers.push_back(cells_layer); 

    // for (QGraphicsItem* cellItem : layers[idx]){
    //     if(cellItem){

    //     }
    //     cellItem->setZValue(idx);
    //     cellItem->setVisible(true);
    //     scene->addItem(cellItem);
    // }
    //qDebug() << tr("layer:") << idx << tr(" zValue:") << layers[idx]->zValue();
}

void MainWindow::slotAddLayer(std::string layerName)
{
    layerComboBox->AddItem(QString::fromStdString(layerName), true); 
    QVector<QGraphicsItem*> cells_layer;
    layers.push_back(cells_layer); 
    setDirty(true);
}



void MainWindow::slotDeleteLayer()
{
    int idx = layerComboBox->currentIndex();
    if(idx == -1)
    {
        QMessageBox::information(this, tr("Information消息框"), tr("There have no any cell layers!"));
        return;
    }
    layerComboBox->RemoveItem(idx); 
    
    // QGraphicsItemGroup *itemGroup = layers[idx];
    // auto cells_layer = layers[idx];
    // QList<QGraphicsItem *> items = itemGroup->childItems();
    for(QGraphicsItem *item: layers[idx])
    {
        // itemGroup->removeFromGroup(item);
        scene->removeItem(item);
        delete item;
    }
    
    // scene->destroyItemGroup(itemGroup);
    layers.remove(idx);    

    updateLayerAndCellZValue();
}

void MainWindow::slotClockIndexChanged(int idx)
{
    scene->setCurrentClockIndex(idx);
    QList<QGraphicsItem *> items = scene->selectedItems();

    if(items.isEmpty()) 
        return;

    for(QGraphicsItem *item: items)
    {
        simon::timezone(*static_cast<QCADCellItem *>(item)) = idx; //时钟域，由控制面板传递
        //添加更新操作
    }
}

void MainWindow::slotLayerActiveChanged(int idx){
    scene->setCurrentLayerIndex(idx);
}

void MainWindow::slotViewShowGrid(bool on)
{
    checkBox->setChecked(on);
    scene->setGridVisible(on);
}

void MainWindow::toggleStatusBar(bool checked)
{
    if (checked) {
        customStatusBar->show();
    } else {
        customStatusBar->hide();
    }
}

void MainWindow::slotCaptureFullWindow()
{
    // 创建 QPixmap，大小与窗口相同
    QPixmap screenshot(this->size());

    // 使用 QPainter 将窗口内容渲染到 QPixmap
    QPainter painter(&screenshot);
    this->render(&painter);

    // 保存截图
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save Screenshot"),
                                                    "."+ QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png",
                                                    tr("Images (*.png *.xpm *.jpg *.pdf)"));
    if (!fileName.isEmpty()) {
        screenshot.save(fileName);
        QString message = "The full screen has been captured and the file is saved in " + fileName +" ;";
        printToStatusBar(message);
    }
}


void MainWindow::viewModeChange() {
    if (selectModeButton->isChecked()) {
        setEditMode(EditMode::Select);
        scene->setEditMode(EditMode::Select);
    } else if (insertModeButton->isChecked()) {
        setEditMode(EditMode::Insert);
        scene->setEditMode(EditMode::Insert);
    } else if (dragModeButton->isChecked()) {
        setEditMode(EditMode::DragScene);
        scene->setEditMode(EditMode::DragScene);
    }
}

void MainWindow::setEditMode(EditMode mode) {
    currentMode = mode;
    switch (mode) {
        case EditMode::Select:
            view->setDragMode(QGraphicsView::RubberBandDrag);
            view->setInteractive(true);  // 允许选择和移动item
            break;
        case EditMode::Insert:
            view->setDragMode(QGraphicsView::NoDrag);
            view->setInteractive(true);  // 不允许交互，以便在鼠标点击时插入新item
            break;
        case EditMode::DragScene:
            view->setDragMode(QGraphicsView::ScrollHandDrag);
            view->setInteractive(false);  // 允许拖动场景，但不允许选择或移动item
            break;
    }
}




void MainWindow::slotCellItemInserted(QCADCellItem *cellItem){
    if(cellItem->myCellType == CellType::InputCell)
    {
        this->inputname.append(cellItem->IOName);
    }
    emit savedinputname(inputname);//for 输入lable可选择仿真
    int idx = layerComboBox->currentIndex();
    layers[idx].push_back(cellItem);
    cellItem->setZValue(idx);
    cellItem->setVisible(true);
    scene->addItem(cellItem);
    setDirty(true); 

    // 获取 cellItem 的坐标
    QPointF pos = cellItem->pos();
    QString message = QString("Inserted item at Layer %1, Position (%2, %3)")
                      .arg(idx)
                      .arg(pos.x())
                      .arg(pos.y());

    // 在状态栏打印消息
    customStatusBar->addMessage(message);
}


void MainWindow::slotCellItemInserted(QCADCellItem* cellItem, int layerIndex){
    if(cellItem->myCellType == CellType::InputCell)
    {
        this->inputname.append(cellItem->IOName);
    }
    emit savedinputname(inputname);//for 输入lable可选择仿真
    cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加
    layers[layerIndex].push_back(cellItem);
    cellItem->setZValue(layerIndex);
    cellItem->setVisible(true);
    scene->addItem(cellItem);
    setDirty(true); //这行代码很重要，否则操作的界面无法保存

}

void MainWindow::slotDeleteItem()
{
    QList<QGraphicsItem *> selectedItems = scene->selectedItems();

    // qDebug()<< "lys";
    for (QGraphicsItem *item : selectedItems)
    {
        scene->removeItem(item);
        

        delete item;

    }
}

void MainWindow::checkCellInserted(QVector<QVector<QGraphicsItem*>> &_layers, QCADCellItem* cellItem, int cell_layer, int x_coord, int y_coord)
{
    if (cell_layer < 0 || cell_layer >= _layers.size()) {  
        return;  
    } 

    for (QGraphicsItem* cellItem : layers[cell_layer]) {  
        // 检查坐标是否匹配  
        if (cellItem->x() == x_coord && cellItem->y() == y_coord) {  
            return; // 找到存在的cell
        }  
    }
    slotCellItemInserted(cellItem, cell_layer);
}


void MainWindow::onSimulationFinished(const QString &outputFileName) {
        // 在主线程中更新UI
    WaveformWindow* waveWindow = new WaveformWindow(nullptr, outputFileName);
    waveWindow->show();
    // QMessageBox::information(this, tr("Simulation Complete"), tr("The simulation result has been saved to %1").arg(resultFile));
}

