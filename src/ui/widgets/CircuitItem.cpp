#include "CircuitItem.h"

CircuitItem::CircuitItem(CircuitItemType itemType,QString _nodeName)
{
    myItemType = itemType;
    itemName = _nodeName;
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    // setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
}


void CircuitItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget){
    Q_UNUSED(widget);
    Q_UNUSED(option);
    QPen pen;
    pen.setColor(qRgb(95,164,213));
    pen.setWidth(3);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(pen);
    painter->setBrush(QColor(228,191,122));
    painter->drawRect(-50,-50,100,100);
    myPolygon << QPointF(-50, 50) << QPointF(50,50) << QPointF(50, -50) << QPointF(-50, -50);

    QFont font("Times", 30);
    
    painter->setFont(font);
    switch (myItemType) {
        case INPUT_NODE:
            painter->drawText(-25,-10,itemName);
            break;
        case OUTPUT_NODE:
            painter->drawText(-25,-10,itemName);
            break;
        case AND_GATE:
            painter->drawText(-25,-10,itemName);
            break;
        case OR_GATE:
            painter->drawText(-25,-10,itemName);
        break;
        case NOT_GATE:
            painter->drawText(-25,-10,itemName);
            break;
        case MAJ_GATE:
            painter->drawText(-25,-10,itemName);
            break;
        case REDUNDANCY_NODE:
            painter->drawText(-25,-10,itemName);
            break;
        default:
            painter->drawText(-25,-10,itemName);
            break;
    }

}

void CircuitItem::addArrow(Arrow *arrow){
    arrows.append(arrow);
}

QRectF CircuitItem::boundingRect() const{
    return QRectF(-50, -50, 50,50);
}
