#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainWindow.Constants.h"
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

    connect(scene, SIGNAL(cellItemInserted(QCADCellItem *)), this, SLOT(slotCellItemInserted(QCADCellItem *)));
    // connect(scene, SIGNAL(clockPhaseInserted(QCADClockScheme *)), this, SLOT(slotQCADClockScheme(QCADClockScheme *)));

    QSettings settings;
    viewShowGridAction->setChecked(settings.value(kShowGrid, false).toBool());
    QString fileName = settings.value(kMostRecentFile).toString();
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

void MainWindow::setTabHost(TabbedMainWindow *host)
{
    tabHost = host;
}
