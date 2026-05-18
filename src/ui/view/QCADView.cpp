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

void QCADView::ensurePanTargetVisible(const QPointF &targetCenter)
{
    if (scene() == nullptr || viewport() == nullptr) {
        return;
    }

    const QRectF visibleRect = mapToScene(viewport()->rect()).boundingRect();
    const QPointF currentCenter = mapToScene(viewport()->rect().center());
    const QRectF targetRect = visibleRect.translated(targetCenter - currentCenter);
    constexpr qreal kPanMargin = 800.0;
    const QRectF expandedTarget = targetRect.adjusted(-kPanMargin, -kPanMargin, kPanMargin, kPanMargin);
    const QRectF currentSceneRect = scene()->sceneRect();
    if (!currentSceneRect.contains(expandedTarget)) {
        scene()->setSceneRect(currentSceneRect.united(expandedTarget));
    }
}

void QCADView::mousePressEvent(QMouseEvent *event)
{
    if (dragMode() == QGraphicsView::ScrollHandDrag && event->button() == Qt::LeftButton) {
        handPanning = true;
        lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

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
    if (handPanning) {
        const QPoint delta = event->pos() - lastPanPoint;
        const QPoint viewportCenter = viewport()->rect().center();
        const QPointF targetCenter = mapToScene(viewportCenter - delta);
        ensurePanTargetVisible(targetCenter);
        centerOn(targetCenter);
        lastPanPoint = event->pos();
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
    // event->accept();
}

void QCADView::wheelEvent(QWheelEvent *event)
{
    if (viewport() == nullptr) {
        event->ignore();
        return;
    }

    const QPoint angleDelta = event->angleDelta();
    const QPoint pixelDelta = event->pixelDelta();

    qreal zoomSteps = 0.0;
    if (!angleDelta.isNull()) {
        zoomSteps = angleDelta.y() / 120.0;
    } else if (!pixelDelta.isNull()) {
        zoomSteps = pixelDelta.y() / 120.0;
    }

    if (qFuzzyIsNull(zoomSteps)) {
        event->ignore();
        return;
    }

    constexpr qreal kZoomBase = 1.08;
    constexpr qreal kMinScale = 0.05;
    constexpr qreal kMaxScale = 48.0;

    const qreal currentScale = transform().m11();
    zoomSteps = std::clamp(zoomSteps, -3.0, 3.0);
    const qreal requestedScale = currentScale * std::pow(kZoomBase, zoomSteps);
    const qreal clampedScale = std::clamp(requestedScale, kMinScale, kMaxScale);

    if (!qFuzzyCompare(currentScale, clampedScale)) {
        const QPoint viewportPos = event->pos();
        const QPoint viewportCenter = viewport()->rect().center();
        const QPointF scenePosBefore = mapToScene(viewportPos);
        const QGraphicsView::ViewportAnchor oldAnchor = transformationAnchor();
        const qreal factor = clampedScale / currentScale;

        setTransformationAnchor(QGraphicsView::NoAnchor);
        scale(factor, factor);

        const QPointF scenePosAfter = mapToScene(viewportPos);
        const QPointF centerAfter = mapToScene(viewportCenter);
        const QPointF targetCenter = centerAfter + (scenePosBefore - scenePosAfter);
        ensurePanTargetVisible(targetCenter);
        centerOn(targetCenter);
        setTransformationAnchor(oldAnchor);
    }

    event->accept();
}


void QCADView::mouseReleaseEvent(QMouseEvent *event){
    if (handPanning && event->button() == Qt::LeftButton) {
        handPanning = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}
