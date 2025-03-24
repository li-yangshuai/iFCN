#include "QCADScene.h"
#include <QGraphicsSceneMouseEvent>
#include<QStandardItem>

bool QCADScene::is_gridVisible()
{
    return isGridVisible;
}

void QCADScene::setGridVisible(bool _isGridVisible)
{
    if(_isGridVisible != isGridVisible){
        isGridVisible = _isGridVisible;
        update();
    }
}

QCADScene::~QCADScene()
{

}

void QCADScene::setItemType(CellType type){
    myCellType = type;
}

void QCADScene::setCurrentClockIndex(int _clockPhase){
    currentClockIndex = _clockPhase;
}

void QCADScene::setCurrentLayerIndex(int _layer){
    currentLayerIndex = _layer;
}


void QCADScene::drawEmptyBackGround(QPainter *painter)
{
    for (int x = -SCENE_WIDTH/2 ; x <= SCENE_WIDTH/2; x+= GRID_SIZE)
    {
       for(int y = -SCENE_HEIGHT/2; y <= SCENE_HEIGHT/2; y+= GRID_SIZE){
           painter->drawPoint(x, y);
       }
    }
}

void QCADScene::placeClockScheme( const int _clock_scheme [4][4] ) {

    //坐标起点(40,40)是校准过的，更换会无法正常识别相位
    QPoint startPoint(40,40);
    uint8_t i = 0,j =0;
    for(int xs = startPoint.x(); xs <= SCENE_WIDTH/2; xs+= CLOCK_SCHEME_SIZE_5){
        if(i==4)
            i = 0;
        for(int ys = startPoint.y(); ys <= SCENE_HEIGHT/2; ys+= CLOCK_SCHEME_SIZE_5){
            if(j == 4)
                j=0;
            int _phase = _clock_scheme[j][i];
            QCADClockScheme *item = new QCADClockScheme( _phase);
            item->setPos(xs, ys);
            item->setZValue(-1);
            addItem(item);
            j++;
        }
        i++;
    }
}


void QCADScene::clearPhaseRecord(){
    foreach(QGraphicsItem *item, items()){
        if(item->type() == QCADClockScheme::Type){
            removeItem(item);
            delete item;
        }
    }
}

void QCADScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);
    if(isGridVisible) 
        drawEmptyBackGround(painter);

}

// 鼠标点击布局空间，能够获取离鼠标最近的网格点的位置，来放置cell
QPointF QCADScene::caculateRealPostion(int _posx, int _posy){
    
    int temp_x = qAbs(_posx % GRID_SIZE);
    int temp_y = qAbs(_posy % GRID_SIZE);

    int index_x = _posx / GRID_SIZE;
    int index_y = _posy / GRID_SIZE;

    int pos_x, pos_y;

    if(temp_x <= GRID_SIZE/2){
        pos_x = index_x * GRID_SIZE;
    }else{
        if(index_x >=0){
            pos_x = (index_x + 1) * GRID_SIZE;
        }else{
            pos_x = (index_x -1) * GRID_SIZE;
        }
    }

    if(temp_y <= GRID_SIZE/2){
        pos_y = index_y * GRID_SIZE;
    }else{
        if(index_y >=0){
            pos_y = (index_y + 1) * GRID_SIZE;
        }else{
            pos_y = (index_y -1) * GRID_SIZE;
        }
    }
    return QPointF(pos_x, pos_y);

}

/* 冲突检测*/
void QCADScene::mousePressEvent(QGraphicsSceneMouseEvent *event){

    if(currentMode == EditMode::Insert){

        QPointF scenePoint = event->scenePos();
        QList<QGraphicsItem*> itemsAtPos = items(scenePoint, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform()); 
        
        for(QGraphicsItem* item : itemsAtPos){
            if(item->type() == QCADClockScheme::Type){
                QCADClockScheme* clockItem = dynamic_cast<QCADClockScheme*>(item);
                currentClockIndex = clockItem->clockPhaseType();
            }

            if(item->type() == QCADCellItem::Type){
                if(currentLayerIndex == item->zValue())
                    return;
            }
        }

        QPointF realPos = caculateRealPostion(scenePoint.x(),scenePoint.y());
        QCADCellItem * cellItem = new QCADCellItem( realPos.x(), realPos.y(), currentLayerIndex, currentClockIndex, myCellType);
        cellItem->setPos(simon::x(*cellItem), simon::y(*cellItem));     //在scene层添加

        emit cellItemInserted(cellItem);

    }else if (currentMode == EditMode::Select) {
        foreach (QGraphicsItem *item, selectedItems()){
            if(item->type() == QCADCellItem::Type){
                item->setFlag(QGraphicsItem::ItemIsMovable, true);
                // originalItemPositions[item] = item->pos();
            }
        }
    } 
    QGraphicsScene::mousePressEvent(event);
}

void QCADScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event){

    QGraphicsScene::mouseMoveEvent(event);
}

void QCADScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if(currentMode == EditMode::Select){
        foreach (QGraphicsItem *item, selectedItems()){
            QPointF nextpos = caculateRealPostion(item->pos().x(), item->pos().y());
            item->setPos(nextpos);
        }
    }

    QGraphicsScene::mouseReleaseEvent(event);
}


