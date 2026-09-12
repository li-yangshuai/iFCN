#include "ui/mainwindow/MainWindow.h"
#include "ui/mainwindow/MainWindow.Constants.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace {
bool isSupportedDroppedLayoutFile(const QString &fileName)
{
    const QFileInfo info(fileName);
    if (!info.isFile()) {
        return false;
    }

    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("ifcn") || suffix == QStringLiteral("qca");
}

QString firstSupportedDroppedLayoutFile(const QMimeData *mimeData)
{
    if (mimeData == nullptr || !mimeData->hasUrls()) {
        return QString();
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString fileName = url.toLocalFile();
        if (isSupportedDroppedLayoutFile(fileName)) {
            return fileName;
        }
    }
    return QString();
}

void enableDropTarget(QWidget *widget, QObject *filter)
{
    if (widget == nullptr) {
        return;
    }
    widget->setAcceptDrops(true);
    widget->installEventFilter(filter);
}
}

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

    enableDropTarget(this, this);
    enableDropTarget(centralWidget, this);
    enableDropTarget(splitter, this);
    enableDropTarget(toolBox, this);
    enableDropTarget(view, this);
    enableDropTarget(view != nullptr ? view->viewport() : nullptr, this);

    connect(scene, SIGNAL(cellItemInserted(QCADCellItem *)), this, SLOT(slotCellItemInserted(QCADCellItem *)));
    connect(scene, &QGraphicsScene::changed, this, [this](const QList<QRectF> &) {
        if (view != nullptr) {
            view->setEmptyStateVisible(!currentCanvasHasItemsOrData());
        }
    });
    connect(scene, &QCADScene::clockRegionsChanged, this, [this]() {
        // QCADScene may be hosted inside TabbedMainWindow, so its view's top-level
        // window is not necessarily this MainWindow.  Update the empty-state card
        // from the signal instead of relying on QCADScene's window cast.
        if (view != nullptr) {
            view->setEmptyStateVisible(!currentCanvasHasItemsOrData());
        }
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
            if (startupRestoreEnabled) {
                loadFile(fileName);
            }
        });
    }

}


MainWindow::~MainWindow()
{

}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event != nullptr) {
        switch (event->type()) {
            case QEvent::DragEnter: {
                auto *dragEvent = static_cast<QDragEnterEvent *>(event);
                if (!firstSupportedDroppedLayoutFile(dragEvent->mimeData()).isEmpty()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            case QEvent::DragMove: {
                auto *dragEvent = static_cast<QDragMoveEvent *>(event);
                if (!firstSupportedDroppedLayoutFile(dragEvent->mimeData()).isEmpty()) {
                    dragEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            case QEvent::Drop: {
                auto *dropEvent = static_cast<QDropEvent *>(event);
                const QString fileName = firstSupportedDroppedLayoutFile(dropEvent->mimeData());
                if (!fileName.isEmpty()) {
                    openLayoutFilePath(fileName);
                    dropEvent->acceptProposedAction();
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event != nullptr && !firstSupportedDroppedLayoutFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event != nullptr && !firstSupportedDroppedLayoutFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dragMoveEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QString fileName = event != nullptr
        ? firstSupportedDroppedLayoutFile(event->mimeData())
        : QString();
    if (!fileName.isEmpty()) {
        openLayoutFilePath(fileName);
        event->acceptProposedAction();
        return;
    }
    QMainWindow::dropEvent(event);
}

void MainWindow::setTabHost(TabbedMainWindow *host)
{
    tabHost = host;
}

void MainWindow::disableStartupRestore()
{
    startupRestoreEnabled = false;
    setCurrentFile(QString());
    refreshLayoutInfoPanel();
}
