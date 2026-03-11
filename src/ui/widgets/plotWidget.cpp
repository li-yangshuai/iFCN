#include "plotWidget.h"
#include <iostream>
#include <algorithm> // for std::min_element and std::max_element
PlotWidget::PlotWidget(const QString &labelText, const QColor &lineColor, QVector<double> vecX, QVector<double> vecY, double yMin, double yMax)
{

    yMin -= 0.1 * (yMax - yMin);
    yMax += 0.1 * (yMax - yMin);
    this->yMinp = yMin;
    this->yMaxp = yMax;
    QHBoxLayout *layout = new QHBoxLayout(this);
    this->label = new QLabel(labelText, this);
    label->setFixedWidth(120);
    label->setFixedHeight(100);
    label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    label->setStyleSheet("border: none;");  // 初始无边框
    this->customPlot = new QCustomPlot(this);
    layout->addWidget(label);
    layout->addWidget(customPlot);
    // 设置轨迹的颜色
    customPlot->addGraph();
    customPlot->graph(0)->setPen(QPen(lineColor));

    // 设置图表的大小
    customPlot->setMinimumHeight(100);
    customPlot->setMaximumHeight(100);

    // 设置坐标轴的显示
    customPlot->xAxis->setVisible(false);
    customPlot->xAxis->setTickLabels(false);
    customPlot->xAxis2->setVisible(true);
    customPlot->xAxis2->setTickLabels(true);
    customPlot->yAxis->setVisible(false);
    customPlot->yAxis->setTickLabels(false);
    customPlot->yAxis2->setVisible(false);
    customPlot->yAxis2->setTickLabels(false);

    // 隐藏网格线
    customPlot->xAxis->grid()->setVisible(false);
    customPlot->xAxis2->grid()->setVisible(false);
    customPlot->yAxis->grid()->setVisible(false);
    customPlot->yAxis2->grid()->setVisible(false);

    // 设置背景的渐变色
    QLinearGradient gradient(0, 0, 0, 400);
    gradient.setColorAt(0, QColor(90, 90, 90));
    gradient.setColorAt(0.38, QColor(105, 105, 105));
    gradient.setColorAt(1, QColor(70, 70, 70)); 
    customPlot->axisRect()->setBackground(gradient);

    // 设置刻度的长度
    customPlot->xAxis2->setTickLengthIn(5);
    customPlot->xAxis2->setTickLengthOut(6);

    // 绘制三条水平线
    QCPItemStraightLine *line1 = new QCPItemStraightLine(customPlot);
    line1->point1->setCoords(customPlot->xAxis->range().lower, yMin);
    line1->point2->setCoords(customPlot->xAxis->range().upper, yMax);
    line1->setPen(QPen(Qt::gray, 1, Qt::DashLine));

    QCPItemStraightLine *line2 = new QCPItemStraightLine(customPlot);
    line2->point1->setCoords(customPlot->xAxis->range().lower, 0);
    line2->point2->setCoords(customPlot->xAxis->range().upper, 0);
    line2->setPen(QPen(Qt::gray, 1, Qt::DashLine));

    QCPItemStraightLine *line3 = new QCPItemStraightLine(customPlot);
    line3->point1->setCoords(customPlot->xAxis->range().lower, yMin);
    line3->point2->setCoords(customPlot->xAxis->range().upper, yMax);
    line3->setPen(QPen(Qt::gray, 1, Qt::DashLine));

    // 绘制x轴的刻度
    QSharedPointer<QCPAxisTickerFixed> xTicker(new QCPAxisTickerFixed);
    xTicker->setTickStep(1000);
    xTicker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    customPlot->xAxis2->setTicker(xTicker);

    customPlot->axisRect()->setAutoMargins(QCP::msNone) ;
    customPlot->axisRect()->setMargins(QMargins(5,25,5,5));
    // 自适应坐标轴
    connect(customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), customPlot->xAxis2, SLOT(setRange(QCPRange)));
    connect(customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), this, SLOT(axischanged()));// for 轴刻度改变
    // 写入数据
    if (!vecX.isEmpty() && !vecY.isEmpty() && vecX.size() == vecY.size())
    {
        customPlot->graph(0)->setData(vecX, vecY);
        customPlot->xAxis->setRange(*std::min_element(vecX.begin(), vecX.end()), *std::max_element(vecX.begin(), vecX.end()));

        customPlot->yAxis->setRange(yMin, yMax);
        customPlot->replot();
    }
    else
    {
        qWarning() << "Data vectors are empty or of unequal length!";
    }
    //生成游标
    tracer = new QCPItemTracer(customPlot);
    tracer->setStyle(QCPItemTracer::tsCircle);
    tracer->setPen(QPen(Qt::black));
    tracer->setBrush(Qt::black);
    tracer->setSize(6);
    tracer->setVisible(false);

    //鼠标选中框
    selectionRect = new QCPItemRect(customPlot);
    selectionRect->setPen(QPen(Qt::DashLine));
    selectionRect->setBrush(QBrush(QColor(0, 0, 255, 50))); // 半透明蓝色
    selectionRect->setVisible(false);

    // 鼠标操作
    connect(customPlot, &QCustomPlot::mousePress, this, &PlotWidget::onMousePress);
    connect(customPlot, &QCustomPlot::mouseMove, this, &PlotWidget::onMouseMove);
    connect(customPlot, &QCustomPlot::mouseRelease, this, &PlotWidget::onMouseRelease);

}
// for 轴刻度改变
void PlotWidget::axischanged()
{
    QCPRange range = customPlot->xAxis->range();
    double left = range.lower;
    double right = range.upper;
    int diff = static_cast<int>(right - left);

    int tickStep = calculateY(diff);
    QSharedPointer<QCPAxisTickerFixed> xTicker(new QCPAxisTickerFixed);
    xTicker->setTickStep(tickStep);
    xTicker->setScaleStrategy(QCPAxisTickerFixed::ssNone);
    customPlot->xAxis2->setTicker(xTicker);
}
int PlotWidget::calculateY(int x) {
    if (x >= 8000) {
        return 1000;
    } else if (x >= 4000) {
        return 500;
    } else if (x >= 1500) {
        return 250;
    } else if (x >= 800) {
        return 100;
    } else if (x >= 400) {
        return 50;
    } else if (x >= 150) {
        return 25;
    } else if (x >= 80) {
        return 10;
    } else if (x >= 30) {
        return 5;
    } else {
        return 1;
    }
}
void PlotWidget::onMousePress(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_isSelecting = true;
        m_selectionStart = event->pos();
        m_selectionEnd = event->pos();

        // 设置选择框的起始位置
        double xStart = customPlot->xAxis->pixelToCoord(m_selectionStart.x());
        double yStart = yMaxp;
        double yEnd = yMinp;
        selectionRect->topLeft->setCoords(xStart, yStart);
        selectionRect->bottomRight->setCoords(xStart, yEnd);
        selectionRect->setVisible(true);
        customPlot->replot();
    }
}

void PlotWidget::onMouseMove(QMouseEvent *event)
{
    if (m_isSelecting)
    {
        m_selectionEnd = event->pos();

        double xStart = customPlot->xAxis->pixelToCoord(m_selectionStart.x());
        double yStart = yMaxp;
        double xEnd = customPlot->xAxis->pixelToCoord(m_selectionEnd.x());
        double yEnd = yMinp;
        selectionRect->topLeft->setCoords(xStart, yStart);
        selectionRect->bottomRight->setCoords(xEnd, yEnd);

        customPlot->replot();
    }
    QCPGraph *mGraph = customPlot->graph(0);
    // 将像素点转换成qcustomplot中的坐标值，并通过setGraphKey将锚点值设为真实数据值。tracer->setGraphKey(xAxis->pixelToCoord(event->pos().x()));
    double x = customPlot->xAxis->pixelToCoord(event->pos().x());
    // 显示锚点
    tracer->setVisible(true);
    tracer->setGraph(mGraph);       // 将锚点设置到被选中的曲线上
    tracer->setGraphKey(x);         // 将游标横坐标设置成刚获得的横坐标数据x
    tracer->setInterpolating(true); // 游标的纵坐标可以通过曲线数据线性插值自动获得
    tracer->updatePosition();       // 使得刚设置游标的横纵坐标位置生效
    customPlot->replot();
    emit tracer_changed(x);
}

void PlotWidget::onMouseRelease(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isSelecting)
    {
        m_isSelecting = false;
        selectionRect->setVisible(false);

        // 获取选择框的坐标范围
        double xStart = selectionRect->topLeft->coords().x();
        double xEnd = selectionRect->bottomRight->coords().x();

        // 设置x坐标轴范围
        customPlot->xAxis->setRange(std::min(xStart, xEnd), std::max(xStart, xEnd));

        customPlot->replot();

        emit customPlot_changed(customPlot->xAxis->range().lower, customPlot->xAxis->range().upper);
    }
}
