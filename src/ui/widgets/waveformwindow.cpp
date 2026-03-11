#include "waveformwindow.h"

WaveformWindow::WaveformWindow(QWidget *parent, QString filePath)
    : QMainWindow(parent)
{
    loadDataFromFile(filePath);
    // this->setStyleSheet("background-color: #272b33;");
    this->setWindowTitle(simWindowTitleName);
    this->resize(1600, 600);
    createToolBar();
    createCentralWidget();

    connect(openAction, SIGNAL(triggered()), this, SLOT(slotOpenFile()));
    connect(saveAction, SIGNAL(triggered()), this, SLOT(slotSaveFile()));
    connect(zoomInAction, SIGNAL(triggered()), this, SLOT(slotZoomIn()));
    connect(zoomOutAction, SIGNAL(triggered()), this, SLOT(slotZoomOut()));
    connect(shootScreenAction, SIGNAL(triggered()), this, SLOT(slotShootScreen()));
    connect(zoomResetAction, SIGNAL(triggered()), this, SLOT(slotZoomReset()));
    connect(moveLeft, SIGNAL(triggered()), this, SLOT(slotMoveLeft()));
    connect(moveRight, SIGNAL(triggered()), this, SLOT(slotMoveRight()));

    // 隐藏、显示
    connect(treeWidget, &QTreeWidget::itemChanged, this, &WaveformWindow::slotTreeItemChanged);
    // connect(treeWidget, &QTreeWidget::itemMoved, this, &WaveformWindow::slotUpdatePlotOrder);
}

void WaveformWindow::createToolBar()
{
    toolBar = addToolBar(tr("Main Toolbar"));
    openAction = new QAction(tr("Open"), this);
    saveAction = new QAction(tr("Save"), this);
    shootScreenAction = new QAction(tr("ShootScreen"), this);

    zoomInAction = new QAction(tr("ZoomIn"), this);
    zoomOutAction = new QAction(tr("ZoomOut"), this);
    zoomResetAction = new QAction(tr("ZoomReset"), this);
    moveLeft = new QAction(tr("MoveLeft"), this);
    moveRight = new QAction(tr("MoveRight"), this);

    toolBar->addAction(openAction);
    toolBar->addAction(saveAction);
    toolBar->addAction(shootScreenAction);
    toolBar->addSeparator();
    toolBar->addAction(zoomInAction);
    toolBar->addAction(zoomOutAction);
    toolBar->addAction(zoomResetAction);
    toolBar->addSeparator();
    toolBar->addAction(moveLeft);
    toolBar->addAction(moveRight);
}

void WaveformWindow::createCentralWidget()
{
    // 中央部件
    QWidget *centralWidget = new QWidget(this);
    this->setCentralWidget(centralWidget);

    // 分隔线
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet("QSplitter::handle { background-color: gray; }");

    treeWidget = new QTreeWidget(this);
    //treeWidget->setMaximumWidth(200);
    treeWidget->setMinimumWidth(20);
    treeWidget->setHeaderLabels({"Trace", "Visible"});
    treeWidget->header()->setSectionResizeMode(QHeaderView::Interactive);

    // 启动拖放功能
    treeWidget->setDragDropMode(QAbstractItemView::InternalMove);

    QWidget *rightWidget = new QWidget(this);

    // 为右侧布局设置布局
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightWidget->setLayout(rightLayout);

    splitter->addWidget(treeWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1); // 左侧区域比例为1 
    splitter->setStretchFactor(1, 4); // 右侧区域比例为4

    // 将 QSplitter 作为 centralWidget 的唯一元素
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addWidget(splitter);
    centralWidget->setLayout(mainLayout);

    // 创建图表布局
    QWidget *plotWidgetContainer = new QWidget();
    plotLayout = new QVBoxLayout(plotWidgetContainer);
    plotLayout->setAlignment(Qt::AlignTop);
    plotLayout->setContentsMargins(0, 0, 0, 0);
    plotLayout->setSpacing(0.1);

    addTreeItem();
    
    // 创建垂直滚动条
    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(plotWidgetContainer);
    scrollArea->setWidgetResizable(true);
    rightLayout->addWidget(scrollArea);

    // 设置初始分割位置
    QList<int> sizes;
    sizes << 200 << width() - 200;
    splitter->setSizes(sizes);
}

void WaveformWindow::addTreeItem()
{

    // 波形颜色
    QVector<QColor> colors = {Qt::blue, Qt::yellow, Qt::red};

    // 创建横坐标的数据
    QVector<double> vecX(numberSamples);
    for (int i = 0; i < numberSamples; ++i)
    {
        vecX[i] = i;
    }

    if (!traceInputDataMap.empty())
    {
        for (auto it = traceInputDataMap.begin(); it != traceInputDataMap.end(); ++it)
        {
            // 创建树结构的item
            QTreeWidgetItem *item = new QTreeWidgetItem(treeWidget);
            QString signalName = it.key();
            item->setText(0, signalName);
            item->setCheckState(1, Qt::Checked);
            treeWidget->addTopLevelItem(item);
            
            //字体大小设置
            QFont font = item->font(0);  // 获取列0的字体
            font.setPointSize(14);  // 设置字体大小为12，可以根据需要调整
            item->setFont(0, font);  // 应用字体到列0

            // 创建plot
            auto vecY = it.value();
            auto minMaxPair = std::minmax_element(vecY.begin(), vecY.end());
            double minValue = *minMaxPair.first;
            double maxValue = *minMaxPair.second;

            QString labelText = "max: 1.00e+000\n" + signalName + "\nmin: -1.00e+000";
            PlotWidget *plotWidget = new PlotWidget(labelText, colors[0], vecX, it.value(), minValue, maxValue);
            plotLayout->addWidget(plotWidget);
            plotWidgets.append(plotWidget);

            connect(plotWidget, &PlotWidget::customPlot_changed, this, &WaveformWindow::customPlot_changed);// for 鼠标选中放大同步
            connect(plotWidget, &PlotWidget::tracer_changed, this, &WaveformWindow::tracer_changed);
        }
    }

    if (!traceOutputDataMap.empty())
    {
        for (auto it = traceOutputDataMap.begin(); it != traceOutputDataMap.end(); ++it)
        {
            // 创建树结构的item
            QTreeWidgetItem *item = new QTreeWidgetItem(treeWidget);
            QString signalName = it.key();
            item->setText(0, signalName);
            item->setCheckState(1, Qt::Checked);
            treeWidget->addTopLevelItem(item);

            //字体大小设置
            QFont font = item->font(0);  // 获取列0的字体
            font.setPointSize(14);  // 设置字体大小为12，可以根据需要调整
            item->setFont(0, font);  // 应用字体到列0

            // 创建plot
            auto vecY = it.value();
            auto minMaxPair = std::minmax_element(vecY.begin(), vecY.end());
            double minValue = *minMaxPair.first;
            double maxValue = *minMaxPair.second;

            QString labelText = "max: 9.54e-001\n" + signalName + "\nmin: -9.54e-001";
            PlotWidget *plotWidget = new PlotWidget(labelText, colors[1], vecX, it.value(), minValue, maxValue);
            plotLayout->addWidget(plotWidget);
            plotWidgets.append(plotWidget);

            connect(plotWidget, &PlotWidget::customPlot_changed, this, &WaveformWindow::customPlot_changed);// for 鼠标选中放大同步
            connect(plotWidget, &PlotWidget::tracer_changed, this, &WaveformWindow::tracer_changed);
        }
    }
    if (!traceClockDataMap.empty())
    {
        for (auto it = traceClockDataMap.begin(); it != traceClockDataMap.end(); ++it)
        {
            // 创建树结构的item
            QTreeWidgetItem *item = new QTreeWidgetItem(treeWidget);
            QString signalName = it.key();
            item->setText(0, signalName);
            item->setCheckState(1, Qt::Checked);
            treeWidget->addTopLevelItem(item);

            //字体大小设置
            QFont font = item->font(0);  // 获取列0的字体
            font.setPointSize(14);  // 设置字体大小为12，可以根据需要调整
            item->setFont(0, font);  // 应用字体到列0

            // 创建plot
            auto vecY = it.value();
            // qDebug() << vecY;
            auto minMaxPair = std::minmax_element(vecY.begin(), vecY.end());
            double minValue = *minMaxPair.first;
            double maxValue = *minMaxPair.second;
            // qDebug()<< minValue << "\n";
            // qDebug()<< maxValue << "\n";

            // qDebug()<< vecX.size() << "\n";
            // qDebug()<< vecY.size() << "\n";

            QString labelText = "max: 9.80e-002\n" + signalName + "\nmin: 3.80e-023";
            PlotWidget *plotWidget = new PlotWidget(labelText, colors[2], vecX, it.value(), minValue, maxValue);
            plotLayout->addWidget(plotWidget);
            plotWidgets.append(plotWidget);

            connect(plotWidget, &PlotWidget::customPlot_changed, this, &WaveformWindow::customPlot_changed);// for 鼠标选中放大同步
            connect(plotWidget, &PlotWidget::tracer_changed, this, &WaveformWindow::tracer_changed);
        }
    }
}

void WaveformWindow::loadDataFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "Cannot open file for reading:" << file.errorString();
        return;
    }
    // 保存文件名作为窗口标题
    QFileInfo fileInfo(filePath);
    simWindowTitleName = fileInfo.baseName();

    // 如果是新文件清空数据
    plotWidgets.clear();
    traceInputDataMap.clear();
    traceOutputDataMap.clear();
    traceClockDataMap.clear();
    numberSamples = 0;
    numberOfTraces = 0;

    QTextStream in(&file);
    QString line;

    while (!in.atEnd())
    {
        line = in.readLine().trimmed();

        // Parse number_samples
        if (line.startsWith("number_samples="))
        {
            numberSamples = line.mid(QString("number_samples=").length()).toInt();
        }

        if (line.startsWith("number_of_traces="))
        {
            numberOfTraces = line.mid(QString("number_of_traces=").length()).toInt();
        }

        // Parse TRACE data
        if (line == "[TRACE]")
        {
            QString dataLabel;
            int traceFunction = 0;
            bool drawTrace = false;
            QVector<double> traceData;

            while (!(line = in.readLine().trimmed()).startsWith("[#TRACE]"))
            {
                if (line.startsWith("data_labels="))
                {
                    dataLabel = line.mid(QString("data_labels=").length());
                }
                else if (line.startsWith("trace_function="))
                {
                    traceFunction = line.mid(QString("trace_function=").length()).toInt();
                }
                else if (line.startsWith("drawtrace="))
                {
                    drawTrace = line.mid(QString("drawtrace=").length()) == "TRUE";
                }
                else if (line.startsWith("[TRACE_DATA]"))
                {
                    traceData.reserve(numberSamples); // Reserve space for performance
                    while (!(line = in.readLine().trimmed()).startsWith("[#TRACE_DATA]"))
                    {
                        QStringList dataList = line.split(" ", QString::SkipEmptyParts);
                        for (const QString &dataStr : dataList)
                        {
                            traceData.append(dataStr.toDouble());
                        }
                    }
                }
            }

            if (drawTrace)
            {
                if (dataLabel.startsWith("Clock 0") || dataLabel.startsWith("Clock 1") || dataLabel.startsWith("Clock 2") || dataLabel.startsWith("Clock 3"))
                {
                    traceClockDataMap[dataLabel] = traceData;
                }
                else if (traceFunction == 1)
                {
                    traceInputDataMap[dataLabel] = traceData;
                }
                else if (traceFunction == 2)
                {
                    traceOutputDataMap[dataLabel] = traceData;
                }
            }
        }
    }
    file.close();
}

void WaveformWindow::slotTreeItemChanged(QTreeWidgetItem *item, int column)
{
    if (column == 1)
    {
        int index = treeWidget->indexOfTopLevelItem(item);
        if (item->checkState(1) == Qt::Checked)
        {
            plotWidgets[index]->show();
        }
        else
        {
            plotWidgets[index]->hide();
        }
    }
}

void WaveformWindow::slotUpdatePlotOrder()
{
    // //test
    // qDebug() << "Layout changed, updating plot order";

    // // 创建一个新的 QVector 来保存重新排序的 PlotWidgets
    // QVector<PlotWidget*> newPlotWidgets(treeWidget->topLevelItemCount());

    // // 按照 TreeWidget 中的顺序重新排序 PlotWidgets
    // for (int i = 0; i < treeWidget->topLevelItemCount(); ++i) {
    //     QTreeWidgetItem *item = treeWidget->topLevelItem(i);
    //     int index = treeWidget->indexOfTopLevelItem(item);
    //     newPlotWidgets[i] = plotWidgets[index];
    //     qDebug() << "Tree item: " << item->text(0) << ", index: " << index;
    // }

    // // 清空布局中的所有控件
    // QLayoutItem *child;
    // while ((child = plotLayout->takeAt(0)) != nullptr) {
    //     child->widget()->setParent(nullptr); // 避免内存泄漏
    //     delete child;
    //     qDebug() << "Removed child from layout";  // 调试输出
    // }

    // // 按照新的顺序重新添加 PlotWidget
    // for (PlotWidget* plotWidget : newPlotWidgets) {
    //     plotLayout->addWidget(plotWidget);
    //     qDebug() << "Added plotWidget: " << plotWidget;  // 调试输出
    // }

    // // 更新 plotWidgets
    // plotWidgets = newPlotWidgets;

    // // 调试输出重新排序后的 plotWidgets 顺序
    // for (int i = 0; i < plotWidgets.size(); ++i) {
    //     qDebug() << "PlotWidget " << i << ": " << plotWidgets[i];
    // }
}

void WaveformWindow::slotOpenFile()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Text Files (*.rst)"));
    loadDataFromFile(filePath);
    // this->setStyleSheet("background-color: #272b33;");
    this->setWindowTitle(simWindowTitleName);
    createCentralWidget();

}

void WaveformWindow::slotSaveFile()
{
    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(this, "Save as PNG or SVG", "", "PNG Files (*.png);;SVG Files (*.svg)", &selectedFilter);

    if (!fileName.isEmpty())
    {
        if (selectedFilter == "PNG Files (*.png)" && !fileName.endsWith(".png"))
        {
            fileName += ".png";
        }
        else if (selectedFilter == "SVG Files (*.svg)" && !fileName.endsWith(".svg"))
        {
            fileName += ".svg";
        }

        int totalHeight = 0;
        int maxWidth = 0;

        for (PlotWidget *plotWidget : plotWidgets)
        {
            totalHeight += plotWidget->height();
            if (plotWidget->width() > maxWidth)
            {
                maxWidth = plotWidget->width();
            }
        }

        if (fileName.endsWith(".png"))
        {
            QImage image(QSize(maxWidth, totalHeight), QImage::Format_ARGB32);
            image.fill(Qt::white);

            QPainter painter(&image);

            int yOffset = 0;
            for (PlotWidget *plotWidget : plotWidgets)
            {
                QSize widgetSize = plotWidget->size();
                plotWidget->render(&painter, QPoint(0, yOffset), QRegion(0, 0, widgetSize.width(), widgetSize.height()));
                yOffset += widgetSize.height();
            }
            painter.end();

            image.save(fileName);
        }
        else if (fileName.endsWith(".svg"))
        {
            QSvgGenerator svgGenerator;
            svgGenerator.setFileName(fileName);
            svgGenerator.setSize(QSize(maxWidth, totalHeight));
            svgGenerator.setViewBox(QRect(0, 0, maxWidth, totalHeight));
            svgGenerator.setTitle("SVG Export");
            svgGenerator.setDescription("An SVG drawing created by the application.");

            QPainter painter(&svgGenerator);

            int yOffset = 0;
            for (PlotWidget *plotWidget : plotWidgets)
            {
                QSize widgetSize = plotWidget->size();
                plotWidget->render(&painter, QPoint(0, yOffset), QRegion(0, 0, widgetSize.width(), widgetSize.height()));
                yOffset += widgetSize.height();
            }
            painter.end();
        }
    }
}

void WaveformWindow::slotShootScreen(){
    // 创建 QPixmap，大小与窗口相同
    for (PlotWidget *plotWidget : plotWidgets)
    {
        plotWidget->customPlot->axisRect()->setBackground(Qt::white);
    }

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
    }
}


void WaveformWindow::slotZoomOut()
{
    for (PlotWidget *plotWidget : plotWidgets)
    {
        plotWidget->customPlot->xAxis->scaleRange(0.8, plotWidget->customPlot->xAxis->range().center());
        plotWidget->customPlot->replot();
    }
}

void WaveformWindow::slotZoomIn()
{
    for (PlotWidget *plotWidget : plotWidgets)
    {
        plotWidget->customPlot->xAxis->scaleRange(1.25, plotWidget->customPlot->xAxis->range().center());
        plotWidget->customPlot->replot();
    }
}

void WaveformWindow::slotZoomReset()
{
    double a = 0;
    double b = 12800;
    for (PlotWidget *plotWidget : plotWidgets)
    {
        plotWidget->customPlot->xAxis->setRange(a, b);
        plotWidget->customPlot->replot();
    }
}

void WaveformWindow::slotMoveLeft()
{
    if (selectedWidget)
    {
        selectedWidget->customPlot->xAxis->moveRange(-100);
        selectedWidget->customPlot->replot();
    }
    else
    {
        for (PlotWidget *plotWidget : plotWidgets)
        {
            plotWidget->customPlot->xAxis->moveRange(-100);
            plotWidget->customPlot->replot();
        }
    }
}

void WaveformWindow::slotMoveRight()
{
    if (selectedWidget)
    {
        selectedWidget->customPlot->xAxis->moveRange(100);
        selectedWidget->customPlot->replot();
    }
    else
    {
        for (PlotWidget *plotWidget : plotWidgets)
        {
            plotWidget->customPlot->xAxis->moveRange(100);
            plotWidget->customPlot->replot();
        }
    }
}
// for 鼠标选中放大同步
void WaveformWindow::customPlot_changed(double lower, double upper)
{
    for (PlotWidget *plotWidget : plotWidgets)
    {
        plotWidget->customPlot->xAxis->setRange(lower, upper);
        plotWidget->customPlot->replot();
    }
}

// for 游标同步
void WaveformWindow::tracer_changed(double x)
{
    for (PlotWidget *plotWidget : plotWidgets)
    {
        QCPGraph *mGraph = plotWidget->customPlot->graph(0);
        plotWidget->tracer->setGraph(mGraph);       // 将锚点设置到被选中的曲线上
        plotWidget->tracer->setGraphKey(x);         // 将游标横坐标设置成刚获得的横坐标数据x
        plotWidget->tracer->setInterpolating(true); // 游标的纵坐标可以通过曲线数据线性插值自动获得
        plotWidget->tracer->updatePosition();       // 使得刚设置游标的横纵坐标位置生效
        plotWidget->tracer->setVisible(true);
        plotWidget->customPlot->replot();
    }
}
void WaveformWindow::widgetDoubleClicked(PlotWidget *widget)
{
    selectedWidget = widget;

}
void WaveformWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    QWidget *child = childAt(event->pos());
    while (child && !qobject_cast<PlotWidget *>(child))
    {
        child = child->parentWidget();
    }

    PlotWidget *widget = qobject_cast<PlotWidget *>(child);

    if (widget)
    {
        QLabel* label = widget->findChild<QLabel*>(); // 通过findChild查找QLabel

        if (widget && selectedWidget == widget)
        {
            label->setStyleSheet("border: none;"); // 取消选中状态，移除边框
            selectedWidget = nullptr;
        }
        else
        {
            if (selectedWidget)
            {
                QLabel* prevLabel = selectedWidget->findChild<QLabel*>();
                if (prevLabel)
                {
                    prevLabel->setStyleSheet("border: none;"); // 移除之前选中Widget的边框
                }
            }
            selectedWidget = widget;
            if (label)
            {
                label->setStyleSheet("border: 2px dashed black;"); // 为当前选中的QLabel添加虚线框
            }
            widgetDoubleClicked(widget);
        }
    }
    else
    {
        if (selectedWidget)
        {
            QLabel* prevLabel = selectedWidget->findChild<QLabel*>();
            if (prevLabel)
            {
                prevLabel->setStyleSheet("border: none;"); // 取消选中状态，移除边框
            }
        }
        selectedWidget = nullptr;
    }

    QMainWindow::mouseDoubleClickEvent(event);
}

