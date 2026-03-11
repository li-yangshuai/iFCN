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
#include "controllers/VerilogHandler.h"
#include "controllers/GateLevelMapping.h"
#include "ui/widgets/CustomStatusBar.h"

using namespace fcngraph;

#include <string>

class TabbedMainWindow;

class MainWindow : public QMainWindow
{

        Q_OBJECT
public:
        MainWindow(QWidget *parent = 0);
        ~MainWindow();

        void loadFile(const QString &fileName);    //加载.qca文件
        void mapIfcnFile(const QString &fileName); //加载.ifcn并映射
        void centerViewOnItems(bool fitToView = true);
        void beginSceneBatchUpdate();
        void endSceneBatchUpdate(bool recenter = true);
        void setTabHost(TabbedMainWindow *host);

public:
        void printToStatusBar(QString &message);
        CustomStatusBar *customStatusBar;

private:
        void updateUi();
        static quint64 packSceneCoord(int x, int y);

private:
        void createViewAndScene();
        void createToolBox();
        QWidget* createCellWidget(const QString &text, CellType type);
        QWidget* createClockSchemeWidget(const QString &text, const QString &image);
        void setEditMode(EditMode mode);

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
        QCheckBox *checkBox;   // show grid

        //view select mode
        QLabel *viewLabel;
        QButtonGroup *viewModeButtonGroup;
        QToolButton *selectModeButton;
        QToolButton *insertModeButton;
        QToolButton *dragModeButton;

        //verilog parse and three type P&R algorithm
        QPushButton *verParseButton;
        QPushButton *graphRenderButton;
        QPushButton *forceOrientedAlgorithmButton; 

        //gate level mapping
        QPushButton *gateLevelMappingButton;

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

        /********各项菜单栏********/
        QMenu *fileMenu;
        QMenu *editMenu;
        QMenu *viewMenu;
        QMenu *toolsMenu;
        QMenu *simulationMenu;
        QMenu *helpMenu;

        /********各项工具栏********/
        QToolBar *fileTool;
        QToolBar *editTool;
        QToolBar *layersTool;
        QToolBar *clockTool;
        QToolBar *viewTool;
        QToolBar *verilogTool;

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
        QAction *zoomInAction;
        QAction *zoomOutAction;

        /********视图菜单项********/
        QAction *viewShowGridAction;
        QAction *toggleStatusBarAction;
        QAction *captureFullScreen;
        QAction *generateCellLevelLayoutGraph;

        /********仿真菜单项********/
        SimulationManager *simulationManager;
        QAction *startBistableSimAction;
        QAction *starBistableSimWithSelectiveAction;
        QAction *startCoherenceSimAction;
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
        void initialDesign();           //初始化layer[0]
        bool saveFile(const QString &fileName);    //保存.qca文件
        void setCurrentFile(const QString &fileName);   //保存文件名并setDirty(false)
        bool maybeSave();   //判断文档是否保存

        /********初始化数据********/
        QString curFile;    //文件名存储
        QVector<QString> inputname;
        TabbedMainWindow *tabHost = nullptr;
        bool isBatchUpdating = false;
        bool batchDirtyPending = false;
        QVector<QSet<quint64>> batchOccupiedByLayer;

protected:
        void closeEvent(QCloseEvent *event) override;   //重载关闭事件
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

        void slotDeleteLayer();
        void slotClockIndexChanged(int idx);
        void slotLayerActiveChanged(int idx);
        void slotViewShowGrid(bool on);
        void toggleStatusBar(bool checked);

        void slotCaptureFullWindow();

        //四种仿真模式
        void onSimulationFinished(const QString &resultFile);

        void viewModeChange();

        //toolBox：basic qca cell
        void buttonGroupClicked(int);
        void slotClockSchemeGroupClicked(QAbstractButton * button);

        //scene add cell item
        void slotCellItemInserted(QCADCellItem *cellItem);
        void slotCellItemInserted(QCADCellItem* cellItem, int layerIndex);

signals:
        void savedname(QString fileName);
        void savedinputname(QVector<QString> inputname);

};

#endif  //HFUT_GUI_MAINWINDOW_H
