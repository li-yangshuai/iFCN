#ifndef HFUT_GUI_MAINWINDOW_H
#define HFUT_GUI_MAINWINDOW_H

#include <QMainWindow>
#include <QMenu>
#include <QToolBar>
#include <QAction>
#include <QComboBox>
#include <QVector>
#include <QGraphicsScene>
#include <QDebug>
#include "ui/view/QCADView.h"
#include "ui/items/QCADCellItem.h"
#include "ui/widgets/LayerComboBox.h"
#include <QToolBox>
#include <QBoxLayout>
#include <QSplitter>
#include "ui/view/QCADScene.h"
#include <QCheckBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QLabel>
#include "ui/items/QCADClockScheme.h"
#include <simon/simon.hpp>
#include <QTableWidget>
#include <QSet>
#include "ui/widgets/CircuitItem.h"
#include "ui/widgets/Arrow.h"
#include "ui/widgets/waveformwindow.h"
#include "controllers/SimulationManager.h"
#include <QPainter>
#include <QPair>
#include "controllers/VerilogHandler.h"
#include "controllers/GateLevelMapping.h"
#include "ui/widgets/CustomStatusBar.h"

using namespace fcngraph;

#include <string>

class TabbedMainWindow;
class QTextStream;
class CircuitSchematicView;
class LayeredStructure3DView;
class QDockWidget;
class QDialog;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QPlainTextEdit;

class MainWindow : public QMainWindow
{

        Q_OBJECT
public:
        MainWindow(QWidget *parent = 0);
        ~MainWindow();

        void loadFile(const QString &fileName);    //加载.qca文件
        void mapIfcnFile(const QString &fileName, bool showStatusMessage = true); //加载.ifcn并映射
        void centerViewOnItems(bool fitToView = true);
        void beginSceneBatchUpdate();
        void endSceneBatchUpdate(bool recenter = true);
        void setInputNames(const QVector<QString> &names);
        QString currentFilePath() const;
        void setHighQualityMode(bool on);
        void setTabHost(TabbedMainWindow *host);
        void pushUndoSnapshot();
        void updateLayoutInfoFromMapping(const GateLevelMapping &mapping);
        void refreshLayoutInfoPanel();
        void updateCircuitSchematicFromMapping(const GateLevelMapping &mapping);
        void updateCircuitSchematicFromRawData(const QString &circuitName,
                                               QMap<int, GateLevelMapping::NodeInfo> nodes,
                                               QMap<QPair<int,int>, QVector<QPoint>> routes,
                                               QHash<QPoint, int> coordPhaseMap,
                                               QMap<QPair<int,int>, QVector<QPoint>> mappedRouteCells,
                                               QMap<QString, QString> metadata = {});
        void updateVerilogSourceFile(const QString &fileName);
        void setVerilogSourceContent(const QString &sourceText, const QString &filePath = QString());
        QString verilogSourceContent() const;
        QString verilogSourcePath() const;
        void disableStartupRestore();
        bool saveCellLevelLayoutGraphic(const QString &filePath);
        bool saveCircuitSchematicGraphic(const QString &filePath);
        bool saveStructure3DGraphic(const QString &filePath);
        VerilogHandler *layoutHandler() const { return verilogHandler; }

public:
        void printToStatusBar(const QString &message);
        CustomStatusBar *customStatusBar;

private:
        void updateUi();
        static quint64 packSceneCoord(int x, int y);

private:
        void createViewAndScene();
        void createToolBox();
        QWidget* createPhaseCodecPanel();
        QWidget* createLayoutInfoPanel();
        void createCircuitSchematicDock();
        void createVerilogSourceDock();
        void createPhaseCodecDock();
        void createLayoutInfoDock();
        void createStructure3DDock();
        void floatDockContent(QDockWidget *dock,
                              QDialog **floatWindowPtr,
                              const QString &title,
                              const QSize &size);
        void restoreDockContent(QDockWidget *dock, QDialog **floatWindowPtr);
        void closeDockContent(QDockWidget *dock, QDialog **floatWindowPtr);
        bool currentCanvasHasItemsOrData() const;
        void clearCanvasAndMappingData();
        QString writeVerilogSourceRunFile(QString *errorMessage) const;
        QWidget* createCellWidget(const QString &text, CellType type);
        QWidget* createClockSchemeWidget(const QString &text, const QString &image);
        void setEditMode(EditMode mode);
        void setLayoutInfoRows(const QVector<QPair<QString, QString>> &rows);
        qulonglong currentSceneCellCount() const;
        void exportCircuitSchematicSvg();
        void exportStructure3DGraphic();

protected:
        void updateLayerAndCellZValue();

public:
        //ToolBox
        QToolBox *toolBox;
        QVBoxLayout *verticalLayout;
        QButtonGroup *buttonGroup;
        QButtonGroup* clockSchemeGroup;
        QSplitter *splitter;
        QWidget* centralWidget;
        QWidget* phaseCodecPanel;
        QWidget* layoutInfoPanel = nullptr;
        //view select mode
        QLabel *viewLabel;
        QButtonGroup *viewModeButtonGroup;
        QToolButton *selectModeButton;
        QToolButton *insertModeButton;
        QToolButton *dragModeButton;

        // Placement-and-routing workflow selector.
        QToolButton *placeRouteButton = nullptr;
        QMenu *placeRouteMenu = nullptr;
        QToolButton *phaseCodecEncodeButton;
        QToolButton *phaseCodecCancelButton;
        QToolButton *phaseCodecShowAllButton;
        QToolButton *phaseCodec3DButton;
        QComboBox *phaseCodecModeComboBox;
        QTableWidget *phaseCodecTable;
        QLabel *phaseCodecStatusLabel;
        QTableWidget *layoutInfoTable = nullptr;
        QDockWidget *circuitSchematicDock = nullptr;
        QDockWidget *verilogSourceDock = nullptr;
        QDockWidget *phaseCodecDock = nullptr;
        QDockWidget *layoutInfoDock = nullptr;
        QDockWidget *structure3DDock = nullptr;
        QDialog *circuitSchematicFloatWindow = nullptr;
        QDialog *verilogSourceFloatWindow = nullptr;
        QDialog *phaseCodecFloatWindow = nullptr;
        QDialog *layoutInfoFloatWindow = nullptr;
        QDialog *structure3DFloatWindow = nullptr;
        CircuitSchematicView *circuitSchematicView = nullptr;
        LayeredStructure3DView *structure3DView = nullptr;
        QPlainTextEdit *verilogSourceEditor = nullptr;
        QString verilogSourceFilePath;

        //view & scene
        QCADView *view;
        QCADScene *scene;          //通过QGraphicsItem的Z值设置层位置 setZValue() zValue()

        QVector<QVector<QGraphicsItem*>> layers;

        LayerComboBox *layerComboBox;   //用于存储layer信息
        QComboBox *clockComboBox;       //用于存储clock信息

        void checkCellInserted(QVector<QVector<QGraphicsItem*>> &_layers, QCADCellItem* cellItem, int cell_layer, int x_coord, int y_coord);

private:
        EditMode currentMode;

private:
        void createActions();
        void createMenus();
        void createToolBars();
        int selectedClockPhase() const;
        void loadClockRegionsFromFile(const QString &fileName);
        void writeClockRegions(QTextStream &out) const;

        /********各项菜单栏********/
        QMenu *fileMenu;
        QMenu *editMenu;
        QMenu *viewMenu;
        QMenu *toolsMenu;
        QMenu *simulationMenu;

        /********各项工具栏********/
        QToolBar *fileTool = nullptr;
        QToolBar *editTool = nullptr;
        QToolBar *layersTool = nullptr;
        QToolBar *clockTool = nullptr;
        QToolBar *viewTool = nullptr;
        QToolBar *workspaceTool = nullptr;
        QToolBar *verilogTool = nullptr;

        /********各项菜单项*********************************/
        /********文件菜单项********/
        QAction *newAction;
        QAction *openAction;
        QAction *saveAction;
        QAction *saveAsAction;

        /********编辑菜单项********/
        QAction *copyAction;
        QAction *cutAction;
        QAction *pasteAction;
        QAction *deleteAction;
        QAction *undoAction;
        QAction *redoAction;

        /********视图菜单项********/
        QAction *toggleClockGridAction;
        QAction *toggleHighQualityViewAction;
        QAction *toggleStatusBarAction;
        QAction *captureFullScreen;
        QAction *generateCellLevelLayoutGraph;

        /********仿真菜单项********/
        SimulationManager *simulationManager;
        QAction *startBistableSimAction;
        QAction *startAcceleratedBistableSimAction;
        QAction *starBistableSimWithSelectiveAction;
        QAction *startCoherenceSimAction;
        QAction *startAcceleratedCoherenceSimAction;
        QAction *startCoherenceSimWithSelectiveAction;
        QAction *energyAnalysisAction;
        QAction *SimWithSelective;
        // QThread *sthread;              // 线程指针  
        /********帮助菜单项********/
        //添加帮助action
        
        /********layer combobox工具项********/
        QAction *addLayerAction;
        QAction *deleteLayerAction;

        /****** auto P&R *******/
        VerilogHandler *verilogHandler; 

        /***** Mapping */
        GateLevelMapping *gateLevelMapping;

private:
        bool prepareSimulationInput();
        void initialDesign();           //初始化layer[0]
        bool shouldMapIfcnFile(const QString &fileName) const;
        bool saveFile(const QString &fileName, bool updateCurrentFile = true, bool showStatus = true);    //保存.qca文件
        void setCurrentFile(const QString &fileName);   //保存文件名并setDirty(false)
        QString defaultQcaSavePath() const;
        void openLayoutFilePath(const QString &fileName);
        bool maybeSave();   //判断文档是否保存

        /********初始化数据********/
        QString curFile;    //文件名存储
        QVector<QString> inputname;
        TabbedMainWindow *tabHost = nullptr;
        bool isBatchUpdating = false;
        bool batchDirtyPending = false;
        bool startupRestoreEnabled = true;
        QVector<QSet<quint64>> batchOccupiedByLayer;

        struct SnapshotCell {
            int x = 0;
            int y = 0;
            int layer = 0;
            int phase = 0;
            CellType type = CellType::NormalCell;
            QString name;
        };

        struct DesignSnapshot {
            QVector<QString> layerNames;
            QVector<QVector<SnapshotCell>> cellsByLayer;
            QVector<QCADScene::ClockRegionRecord> clockRegions;
        };

        struct ClipboardCell {
            SnapshotCell cell;
        };

        QVector<ClipboardCell> clipboardCells;
        QPoint clipboardAnchor;
        int clipboardPasteCount = 0;
        QVector<DesignSnapshot> undoSnapshots;
        int undoSnapshotIndex = -1;
        bool restoringSnapshot = false;

        struct PhaseCodecTilePreview {
            unsigned int tileX = 0;
            unsigned int tileY = 0;
            QString hex;
        };

        QVector<PhaseCodecTilePreview> phaseCodecTiles;
        QPoint phaseCodecOriginGrid;
        int phaseCodecBlockSize = 4;
        bool phaseCodecPreviewActive = false;
        QVector<QGraphicsItem*> phaseCodecHighlightItems;
        QVector<QGraphicsItem*> circuitNodeHighlightItems;

        QVector<ClipboardCell> selectedCellsForClipboard() const;
        DesignSnapshot captureDesignSnapshot() const;
        void restoreDesignSnapshot(const DesignSnapshot &snapshot, bool markDirty);
        void resetUndoHistory();
        void updateUndoRedoActions();
        bool snapshotsEqual(const DesignSnapshot &lhs, const DesignSnapshot &rhs) const;
        bool snapshotCellsEqual(const SnapshotCell &lhs, const SnapshotCell &rhs) const;
        bool clockRegionsEqual(const QCADScene::ClockRegionRecord &lhs,
                               const QCADScene::ClockRegionRecord &rhs) const;
        bool positionOccupied(int layer, int x, int y) const;
        void ensureLayerExists(int layer);
        void addCellToScene(QCADCellItem *cellItem, int layerIndex);
        int selectedPhaseCodecCount(const QVector<QCADScene::ClockRegionRecord> &regions) const;
        QPoint clockRegionGridCoord(const QCADScene::ClockRegionRecord &region) const;
        QRectF phaseCodecTileSceneRect(const PhaseCodecTilePreview &tile) const;
        void updatePhaseCodecPreview();
        void updateStructure3DView();
        void showStructure3DView();
        void clearPhaseCodecHighlight();
        void highlightPhaseCodecTile(const PhaseCodecTilePreview &tile);
        void highlightAllPhaseCodecTiles();
        void clearCircuitNodeHighlight();
        void highlightCircuitNode(int nodeIndex);
        void highlightCircuitEdge(int sourceNodeIndex, int sinkNodeIndex);
        QRectF circuitNodeSceneBlock(const GateLevelMapping::NodeInfo &node) const;

protected:
        void closeEvent(QCloseEvent *event) override;   //重载关闭事件
        bool eventFilter(QObject *watched, QEvent *event) override;
        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dropEvent(QDropEvent *event) override;
public slots:
        void setDirty(bool on=true);
        void slotAddLayer();
        void slotAddLayer(std::string layerName);

private slots:
        void slotNew();
        void slotOpen();
        bool slotSave();
        bool slotSaveAs();
        void slotDeleteItem();
        void slotCopyItems();
        void slotCutItems();
        void slotPasteItems();
        void slotUndo();
        void slotRedo();

        void slotDeleteLayer();
        void slotClockIndexChanged(int idx);
        void slotLayerActiveChanged(int idx);
        void slotToggleClockGrid(bool on);
        void slotToggleHighQualityView(bool on);
        void toggleStatusBar(bool checked);

        void slotCaptureFullWindow();

        //四种仿真模式
        void onSimulationFinished(const QString &resultFile);
        void onSimulationFailed(const QString &message);
        void slotEnergyAnalysis();
        void onEnergyAnalysisFinished(const QString &message,
                                      const QString &waveformFile,
                                      const QString &reportFile,
                                      const QString &distributionImage);
        void onEnergyAnalysisFailed(const QString &message);

        void viewModeChange();

        //toolBox：basic qca cell
        void buttonGroupClicked(int);
        void slotClockSchemeGroupClicked(QAbstractButton * button);
        void slotEncodeClockRegions();
        void slotCancelPhaseCodecEncoding();
        void slotPhaseCodecModeChanged(int idx);
        void slotPhaseCodecTileActivated(int row, int column);
        void slotCircuitNodeActivated(int nodeIndex);
        void slotCircuitEdgeActivated(int sourceNodeIndex, int sinkNodeIndex);
        void slotClearCircuitSelection();
        void slotGenerateFromVerilogSource();

        //scene add cell item
        void slotCellItemInserted(QCADCellItem *cellItem);
        void slotCellItemInserted(QCADCellItem* cellItem, int layerIndex);

signals:
        void savedname(QString fileName);
        void savedinputname(QVector<QString> inputname);

};

#endif  //HFUT_GUI_MAINWINDOW_H
