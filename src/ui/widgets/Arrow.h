#ifndef ARROW_H
#define ARROW_H

#include <QGraphicsLineItem>

#include "CircuitItem.h"

QT_BEGIN_NAMESPACE
class QGraphicsPolygonItem;
class QGraphicsLineItem;
class QGraphicsScene;
class QRectF;
class QGraphicsSceneMouseEvent;
class QPainterPath;
QT_END_NAMESPACE

//! [0]
class Arrow : public QGraphicsLineItem
{
public:
    enum { Type = UserType + 40 };

    Arrow(CircuitItem *startItem, CircuitItem *endItem,
      QGraphicsItem *parent = 0);


    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void setColor(const QColor &color) { myColor = color; }
    CircuitItem *startItem() const { return myStartItem; }
    CircuitItem *endItem() const { return myEndItem; }

    void updatePosition();

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = 0) override;
    int type() const override { return Type; }
private:
    CircuitItem *myStartItem;
    CircuitItem *myEndItem;
    QColor myColor;
    QPolygonF arrowHead;
};
//! [0]

#endif // ARROW_H
