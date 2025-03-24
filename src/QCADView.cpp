#include"QCADView.h"
#include"QCADCellItem.h"

#include"MainWindow.h"
#include<QStandardItem>

#include <QDebug>

QCADView::QCADView(QWidget *parent) : QGraphicsView(parent)
{
    setDragMode(RubberBandDrag);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    // setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setStyleSheet("padding: 0px; border: 20px;");
    setMouseTracking(true);
    // setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    
    
    parentWindow = parent;
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
    // 获取当前鼠标相对于view的位置;
    QPointF cursorPoint = event->pos();
    // 获取当前鼠标相对于scene的位置;
    QPointF scenePos = this->mapToScene(QPoint(cursorPoint.x(), cursorPoint.y()));
    // 获取view的宽高;
    qreal viewWidth = this->viewport()->width();
    qreal viewHeight = this->viewport()->height();
    // 获取当前鼠标位置相当于view大小的横纵比例;
    qreal hScale = cursorPoint.x() / viewWidth;
    qreal vScale = cursorPoint.y() / viewHeight;
    int wheelDeltaValue = event->delta();
    // 向上滚动，放大;
    if (wheelDeltaValue > 0){
        this->scale(1.2, 1.2);
    }else{
        this->scale(1.0 / 1.2, 1.0 / 1.2);
    }
    // 将scene坐标转换为放大缩小后的坐标;
    QPointF viewPoint = this->matrix().map(scenePos);
    // 通过滚动条控制view放大缩小后的展示scene的位置;
    horizontalScrollBar()->setValue(int(viewPoint.x() - viewWidth * hScale));
    verticalScrollBar()->setValue(int(viewPoint.y() - viewHeight * vScale));

}


void QCADView::mouseReleaseEvent(QMouseEvent *event){
    QGraphicsView::mouseReleaseEvent(event);
}