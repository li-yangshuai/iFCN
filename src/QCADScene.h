#ifndef QCADSCENE_H
#define QCADSCENE_H

#include <QWidget>
#include <QGraphicsScene>
#include <QPainter>
#include "config.h"
#include <QSet>
#include "QCADCellItem.h"
#include "QCADClockScheme.h"
#include<QDebug>


class QCADScene : public QGraphicsScene
{

   Q_OBJECT
public:
    QCADScene(QObject *parent = Q_NULLPTR): QGraphicsScene(parent),
                                            isGridVisible(true),
                                            currentLayerIndex(0),
                                            currentClockIndex(0)
    {

    }

    void setEditMode(EditMode mode) {
        currentMode = mode;
    }

    void setItemType(CellType type);
    void setCurrentClockIndex(int _clockPhase);
    void setCurrentLayerIndex(int _layer);

    ~QCADScene();

    bool is_gridVisible();
    void setGridVisible(bool _isGridVisible);
    void drawEmptyBackGround(QPainter *painter);

    static QPointF caculateRealPostion(int _posx, int _posy);

    void placeClockScheme(const int _clock_scheme [4][4] );
    // int caculateCellatCsPhase(QPointF _cellPos);

    void clearPhaseRecord();

protected:
    void drawBackground(QPainter* painter, const QRectF &rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event);
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);

private:

    bool isGridVisible;
    
    EditMode currentMode;
    CellType myCellType;

    int currentLayerIndex;
    int currentClockIndex;


private:

signals:
    void cellItemInserted(QCADCellItem * item);
    void clockPhaseInserted(QCADClockScheme * item);


};

#endif