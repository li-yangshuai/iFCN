#include "QCADView.h"
#include "ui/items/QCADCellItem.h"

#include "ui/mainwindow/MainWindow.h"
#include <QStandardItem>

#include <QDebug>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
QCADView::QCADView(QWidget *parent) : QGraphicsView(parent)
{
    setDragMode(RubberBandDrag);
    setStyleSheet("padding: 0px; border: none; background: #ffffff;");
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

void QCADView::setEmptyStateVisible(bool visible)
{
    if (emptyStateVisible == visible) {
        return;
    }
    emptyStateVisible = visible;
    viewport()->update();
}

bool QCADView::isEmptyStateVisible() const
{
    return emptyStateVisible;
}

void QCADView::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    if (!emptyStateVisible || viewport() == nullptr || viewport()->width() < 320 || viewport()->height() < 180) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int cardWidth = qMin(470, viewport()->width() - 48);
    const int cardHeight = 164;
    QRectF card((viewport()->width() - cardWidth) / 2.0,
                (viewport()->height() - cardHeight) / 2.0,
                cardWidth,
                cardHeight);

    painter.setPen(QPen(QColor("#d6dde8"), 1.0));
    painter.setBrush(QColor(250, 252, 255, 245));
    painter.drawRoundedRect(card, 12.0, 12.0);

    const QPointF iconCenter(card.left() + 48.0, card.top() + 48.0);
    painter.setPen(QPen(QColor("#2563eb"), 2.2));
    painter.setBrush(QColor("#eff6ff"));
    painter.drawRoundedRect(QRectF(iconCenter.x() - 20.0,
                                   iconCenter.y() - 20.0,
                                   40.0,
                                   40.0),
                            9.0,
                            9.0);
    painter.drawLine(iconCenter + QPointF(-10.0, 0.0), iconCenter + QPointF(10.0, 0.0));
    painter.drawLine(iconCenter + QPointF(0.0, -10.0), iconCenter + QPointF(0.0, 10.0));
    painter.setBrush(QColor("#2563eb"));
    painter.drawEllipse(iconCenter + QPointF(-10.0, 0.0), 3.2, 3.2);
    painter.drawEllipse(iconCenter + QPointF(10.0, 0.0), 3.2, 3.2);
    painter.drawEllipse(iconCenter + QPointF(0.0, -10.0), 3.2, 3.2);
    painter.drawEllipse(iconCenter + QPointF(0.0, 10.0), 3.2, 3.2);

    QRectF titleRect(card.left() + 82.0, card.top() + 25.0, card.width() - 108.0, 30.0);
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor("#172033"));
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, tr("Start a layout"));

    QFont bodyFont = painter.font();
    bodyFont.setPointSizeF(qMax(8.0, bodyFont.pointSizeF() - 1.5));
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor("#5b6474"));
    painter.drawText(QRectF(card.left() + 82.0, card.top() + 55.0,
                            card.width() - 108.0, 42.0),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     tr("Open or drop an .ifcn/.qca layout, or run Universal AI P&R from the toolbar."));

    painter.setPen(QColor("#7b8493"));
    painter.drawText(QRectF(card.left() + 24.0, card.bottom() - 44.0,
                            card.width() - 48.0, 24.0),
                     Qt::AlignCenter,
                     tr("Tip: choose a cell on the left to enter Insert mode."));
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
