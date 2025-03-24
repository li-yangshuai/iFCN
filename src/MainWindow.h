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
#include "QCADView.h"
#include "QCADCellItem.h"
#include "widgets/LayerComboBox.h"
#include <QToolBox>
#include <QBoxLayout>
#include <QSplitter>
#include "QCADScene.h"
#include <QCheckBox>
#include <QToolButton>
#include <QButtonGroup>
#include <QLabel>
#include "QCADClockScheme.h"
#include <simon/simon.hpp>
#include <QTableWidget>
#include "widgets/CircuitItem.h"
#include "widgets/Arrow.h"
#include "widgets/waveformwindow.h"
#include "SimulationManager.h"
// #include <QSvgGenerator>
#include <QPainter>
#include "VerilogHandler.h"
#include "widgets/CustomStatusBar.h"

using namespace fcngraph;
class MainWindow : public QMainWindow
{

        Q_OBJECT
public:
        // 在 MainWindow.h 或其他合适的位置
        MainWindow(QWidget *parent = 0);
        ~MainWindow();

        void loadFile(const QString &fileName);    //加载.qca文件
        void printToStatusBar(QString &message);

private:
        EditMode currentMode;

        void createViewAndScene();

        void createActions();
        void createMenus();
        void createToolBars();

        //toolBox
        void createToolBox();
        QWidget* createCellWidget(const QString &text, CellType type);
        // QWidget* createQcaDeviceWidget(const QString &text, CellType type);
        QWidget* createClockSchemeWidget(const QString &text, const QString &image);
        // QWidget* createClockSchemeWidget(const QString &text, )

        // void createGridGroup();       //创建背景网格线 
        void initialDesign();           //初始化layer[0]

        bool saveFile(const QString &fileName);    //保存.qca文件
        void setCurrentFile(const QString &fileName);   //保存文件名并setDirty(false)
        bool maybeSave();   //判断文档是否保存

        /********初始化数据********/
        QString curFile;    //文件名存储
        //QString fileName;

        //设置控制模式
        void setEditMode(EditMode mode);

public:

        //ToolBox
        QToolBox *toolBox;
        QVBoxLayout *verticalLayout;
        QButtonGroup *buttonGroup;
        // QButtonGroup * qcaDevidceGroup;
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

        //verilog parse
        QPushButton * verParseButton;
        QPushButton * graphRenderButton;

        //view & scene
        QCADView *view;
        QCADScene *scene;          //通过QGraphicsItem的Z值设置层位置 setZValue() zValue()


        QVector<QVector<QGraphicsItem*>> layers;
        // QGraphicsItemGroup * clockPhaseItemGroup;

        LayerComboBox *layerComboBox;   //用于存储layer信息
        QComboBox *clockComboBox;       //用于存储clock信息

        void checkCellInserted(QVector<QVector<QGraphicsItem*>> &_layers, QCADCellItem* cellItem, int cell_layer, int x_coord, int y_coord);
        

public slots:
        void setDirty(bool on=true);
        void slotAddLayer();
        void slotAddLayer(std::string layerName);


protected:
        void closeEvent(QCloseEvent *event) override;   //重载关闭事件
        //删除层后更新layer和cell的Z值
        void updateLayerAndCellZValue();

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
        // void slotQCADClockScheme(QCADClockScheme *item);
        void slotCellItemInserted(QCADCellItem* cellItem, int layerIndex);

        void updateUi();
        //void documentWasModified();
public:
        //状态栏
        CustomStatusBar *customStatusBar;

private:
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
        //添加仿真action
        SimulationManager *simulationManager;
        QAction *startBistableSimAction;
        QAction *starBistableSimWithSelectiveAction;
        QAction *startCoherenceSimAction;
        QAction *startCoherenceSimWithSelectiveAction;
        QAction *energyAnalysisAction;
        QAction *SimWithSelective;
                QThread *sthread;              // 线程指针  
        /********帮助菜单项********/
        //添加帮助action
        
        /********layer combobox工具项********/
        QAction *addLayerAction;
        QAction *deleteLayerAction;

        /****** auto P&R *******/
        VerilogHandler *verilogHandler; 
        QThread *thread;              // 线程指针     

        /********状态栏********/   

signals:
        void savedname(QString fileName);
        void savedinputname(QVector<QString> inputname);

private:
        QVector<QString> inputname;
        QString simfileName;//多线程仿真文件名
};

#endif  //HFUT_GUI_MAINWINDOW_H
