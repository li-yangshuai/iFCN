#include"QCADClockScheme.h"

QCADClockScheme::QCADClockScheme(ClockPhaseType _clockPhaseType){
    setFlags(ItemIsFocusable);
    myClockPhaseType = _clockPhaseType;
}

void QCADClockScheme ::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget){
    Q_UNUSED(option);
    Q_UNUSED(widget);
    QColor zoneColor;
    switch(myClockPhaseType){
        case 0:
            zoneColor = CLOCK_ZONE_0;
            break;
        case 1:
            zoneColor = CLOCK_ZONE_1;
            break;
        case 2:
            zoneColor = CLOCK_ZONE_2;
            break;
        case 3:
            zoneColor = CLOCK_ZONE_3;
            break;
    }

    QPainterPath rectPath;
    rectPath.addRect(-CLOCK_SCHEME_SIZE_5/2, -CLOCK_SCHEME_SIZE_5/2, CLOCK_SCHEME_SIZE_5 , CLOCK_SCHEME_SIZE_5);
    // rectPath.addRect( twoGirdSize,  -twoGirdSize, GRID_SIZE, threeGridSize);
    // rectPath.addRect( -twoGirdSize, twoGirdSize, threeGridSize, GRID_SIZE);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(zoneColor));
    painter->drawPath(rectPath);
}


QRectF QCADClockScheme ::boundingRect() const{
    qreal penwidth = 0.5;
    return QRectF(-CLOCK_SCHEME_SIZE_5/2-penwidth/2, -CLOCK_SCHEME_SIZE_5/2-penwidth/2, 
                 CLOCK_SCHEME_SIZE_5+penwidth, CLOCK_SCHEME_SIZE_5+penwidth);
}

