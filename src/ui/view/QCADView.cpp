#include "QCADView.h"
#include "ui/items/QCADCellItem.h"

#include "ui/mainwindow/MainWindow.h"
#include <QStandardItem>

#include <QDebug>
#include <algorithm>
#include <cmath>
QCADView::QCADView(QWidget *parent) : QGraphicsView(parent)
{
    setDragMode(RubberBandDrag);
    setStyleSheet("padding: 0px; border: 20px;");
    setMouseTracking(true);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setCacheMode(QGraphicsView::CacheNone);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    setBackgroundBrush(QColor("#FFFFFF"));
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);

    parentWindow = parent;
    setHighQualityMode(false);
}

void QCADView::setHighQualityMode(bool enabled)
{
    highQualityMode = enabled;
    setRenderHint(QPainter::Antialiasing, enabled);
    setRenderHint(QPainter::TextAntialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, enabled);
    setOptimizationFlag(QGraphicsView::DontSavePainterState, !enabled);
    setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, !enabled);
    setViewportUpdateMode(enabled ? QGraphicsView::BoundingRectViewportUpdate
                                  : QGraphicsView::SmartViewportUpdate);
    viewport()->update();
}

bool QCADView::isHighQualityMode() const
{
    return highQualityMode;
}

void QCADView::mousePressEvent(QMouseEvent *event)
{
    // qDebug() << event->pos().x() << "====" << event->pos().y() << "\n";
    // qDebug() << map
    // // QStandardItem* item = static_cast<MainWindow *>(parentWindow)->layerComboBox->GetSelItem();  
    // if (nullptr == item) 
    //     return; 

    // if(item->checkState() == Qt::Checked)
    // {
    //     int layerIdx = static_cast<MainWindow *>(parentWindow)->layerComboBox->currentIndex();
    //     //qDebug() << tr("idx:") << idx;
    //     int clockIdx = static_cast<MainWindow *>(parentWindow)->clockComboBox->currentIndex();

    //     QPoint scenePoint = mapToScene(event->pos()).toPoint();
    //     QCADCellItem *cellItem = new QCADCellItem(scenePoint.x(), scenePoint.y(), layerIdx, clockIdx);
    //     cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加
    //     cellItem->setZValue(layerIdx);     //由layerComboBox的索引号决定

    //     //qDebug() << static_cast<MainWindow *>(parentWindow)->layers.size();
    //     //qDebug() << static_cast<MainWindow *>(parentWindow)->gridGroup->childItems().size();
    //     static_cast<MainWindow *>(parentWindow)->layers[layerIdx]->addToGroup(cellItem);
    //     static_cast<MainWindow *>(parentWindow)->setDirty(true);
    //     //qDebug() << cellItem << tr("zValue:") << cellItem->zValue();
    //     //qDebug() << static_cast<MainWindow *>(parentWindow)->layers[0]->scene();
    //     //qDebug() << scene();
    //     //qDebug() << cellItem->zValue();
    //     //qDebug() << parentWindow << static_cast<MainWindow *>(parentWindow)->layers[0]->childItems().size();

    //     //scene()->addItem(cellItem);   //已经通过layers添加入scene了，不用再添加
    //     //qDebug() << scene()->items().size();
    // }

    QGraphicsView::mousePressEvent(event);
    // event->accept();
}

void QCADView::mouseMoveEvent(QMouseEvent *event)
{
    QGraphicsView::mouseMoveEvent(event);
    // event->accept();
}

void QCADView::wheelEvent(QWheelEvent *event)
{
    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();

    qreal zoomSteps = 0.0;
    if (!angleDelta.isNull()) {
        zoomSteps = angleDelta.y() / 120.0;
    } else if (!pixelDelta.isNull()) {
        zoomSteps = pixelDelta.y() / 90.0;
    }

    if (qFuzzyIsNull(zoomSteps)) {
        event->ignore();
        return;
    }

    constexpr qreal kZoomBase = 1.12;
    constexpr qreal kMinScale = 0.05;
    constexpr qreal kMaxScale = 48.0;

    const qreal currentScale = transform().m11();
    const qreal requestedScale = currentScale * std::pow(kZoomBase, zoomSteps);
    const qreal clampedScale = std::clamp(requestedScale, kMinScale, kMaxScale);

    if (!qFuzzyCompare(currentScale, clampedScale)) {
        const qreal factor = clampedScale / currentScale;
        scale(factor, factor);
    }

    event->accept();
}


void QCADView::mouseReleaseEvent(QMouseEvent *event){
    QGraphicsView::mouseReleaseEvent(event);
}
