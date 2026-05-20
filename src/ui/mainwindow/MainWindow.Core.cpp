#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainWindow.Constants.h"
#include <QDockWidget>
#include <QSettings>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), 
                                          currentMode(EditMode::Select),
                                          simulationManager(new SimulationManager),
                                          verilogHandler(new VerilogHandler(this)),
                                          gateLevelMapping(new GateLevelMapping(this))
{
    createViewAndScene();
    createActions();
    createMenus();
    createToolBars();
    initialDesign();
    createToolBox();
    resetUndoHistory();

    connect(scene, SIGNAL(cellItemInserted(QCADCellItem *)), this, SLOT(slotCellItemInserted(QCADCellItem *)));
    connect(scene, &QCADScene::clockRegionsChanged, this, [this]() {
        if (phaseCodecPreviewActive) {
            updatePhaseCodecPreview();
        }
        if (structure3DDock != nullptr && structure3DDock->isVisible()) {
            updateStructure3DView();
        }
    });
    connect(gateLevelMapping, &GateLevelMapping::mappingLoaded, this, [this]() {
        updateCircuitSchematicFromMapping(*gateLevelMapping);
        if (structure3DDock != nullptr && structure3DDock->isVisible()) {
            updateStructure3DView();
        }
    });
    // connect(scene, SIGNAL(clockPhaseInserted(QCADClockScheme *)), this, SLOT(slotQCADClockScheme(QCADClockScheme *)));

    QSettings settings;
    QString fileName = settings.value(kMostRecentFile).toString();
    if(fileName.isEmpty() || fileName == tr("Unnamed"))
        setCurrentFile(QString());
    else {
        setCurrentFile(fileName);
        QTimer::singleShot(0, this, [this, fileName]() {
            loadFile(fileName);
        });
    }

}


MainWindow::~MainWindow()
{

}

void MainWindow::setTabHost(TabbedMainWindow *host)
{
    tabHost = host;
}
