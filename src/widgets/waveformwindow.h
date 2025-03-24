#ifndef WAVEFORMWINDOW_H
#define WAVEFORMWINDOW_H

#include "waveformwindow.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QCheckBox>
#include <QLabel>
#include <QSvgGenerator>
#include "plotWidget.h"
#include <QSvgGenerator>

class WaveformWindow : public QMainWindow
{
    Q_OBJECT

public:
    WaveformWindow(QWidget *parent, QString filePath);
    ~WaveformWindow() {}

    void addTreeItem();

private slots:
    void loadDataFromFile(const QString &filePath);
    void slotTreeItemChanged(QTreeWidgetItem *item, int column);
    void slotUpdatePlotOrder();

    void slotOpenFile();
    void slotSaveFile();
    void slotShootScreen();
    void slotZoomIn();
    void slotZoomOut();
    void slotZoomReset();
    void slotMoveLeft();
    void slotMoveRight();

    void customPlot_changed(double lower, double upper);
    void tracer_changed(double x);
    void widgetDoubleClicked(PlotWidget* widget);
    void mouseDoubleClickEvent(QMouseEvent* event) override;;

private:
    void createToolBar();
    void createCentralWidget();
    // void drawWaveform();

    QAction *openAction;
    QAction *saveAction;
    QAction *shootScreenAction;

    QAction *zoomInAction;
    QAction *zoomOutAction;
    QAction *zoomResetAction;
    QAction *moveLeft;
    QAction *moveRight;

private:
    QGraphicsScene * scene;
    QGraphicsView * view;
    QToolBar *toolBar;
    QTreeWidget *treeWidget;
    QWidget *plotWidgetContainer;
    QVBoxLayout *plotLayout;
    QVector<PlotWidget *> plotWidgets;

    // QVector<QCustomPlot*> vecSimPlots;

    // QCustomPlot *customPlot;

    QString simWindowTitleName; // 界面标题
    int numberSamples = 0;
    int numberOfTraces = 0;
    QMap<QString, QVector<double>> traceInputDataMap;
    QMap<QString, QVector<double>> traceOutputDataMap;
    QMap<QString, QVector<double>> traceClockDataMap;
    PlotWidget* selectedWidget = nullptr;
};
#endif // WAVEFORMWINDOW_H
