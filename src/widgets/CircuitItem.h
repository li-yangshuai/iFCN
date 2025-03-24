#ifndef CIRCUITITEM_H
#define CIRCUITITEM_H

#include <QGraphicsPixmapItem>
#include <QList>
#include <QGraphicsScene>

#include <QPainter>


QT_BEGIN_NAMESPACE
class QPixmap;
class QGraphicsItem;
class QGraphicsScene;
class QTextEdit;
class QGraphicsSceneMouseEvent;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;
class QPolygonF;
class Arrow;
QT_END_NAMESPACE


class CircuitItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 20 };
    enum CircuitItemType { INPUT_NODE, OUTPUT_NODE, AND_GATE, OR_GATE, NOT_GATE, MAJ_GATE, REDUNDANCY_NODE};

    CircuitItem(CircuitItemType itemType, QString _nodeName);
    QPolygonF polygon() const { return myPolygon; }
    CircuitItemType itemType() const { return myItemType; }
    void addArrow(Arrow *arrow);


protected:
    int type() const override { return Type;}
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    QRectF boundingRect() const override;

private:
    CircuitItemType myItemType;
    QString itemName;
    QPolygonF myPolygon;
    QList<Arrow *> arrows;
};

#endif // CIRCUITITEM_H
