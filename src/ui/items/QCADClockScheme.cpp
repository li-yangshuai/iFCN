#include"QCADClockScheme.h"

QCADClockScheme::QCADClockScheme(ClockPhaseType _clockPhaseType){
    setFlags(ItemIsFocusable);
    setCacheMode(DeviceCoordinateCache);
    myClockPhaseType = _clockPhaseType;
}

QCADClockScheme::QCADClockScheme(int _phase)
{
    setFlags(ItemIsFocusable);
    setCacheMode(DeviceCoordinateCache);
    myClockPhaseType = phaseTypeFromInt(_phase);
}

QCADClockScheme::ClockPhaseType QCADClockScheme::phaseTypeFromInt(int _phase)
{
    switch (_phase) {
        case -1: return Phase_None;
        case 0: return Phase_0;
        case 1: return Phase_1;
        case 2: return Phase_2;
        case 3: return Phase_3;
        default: return Phase_0;
    }
}

int QCADClockScheme::phase() const
{
    return static_cast<int>(myClockPhaseType);
}

void QCADClockScheme::setClockPhaseType(ClockPhaseType _clockPhaseType)
{
    if (myClockPhaseType == _clockPhaseType) {
        return;
    }
    myClockPhaseType = _clockPhaseType;
    update();
}

void QCADClockScheme::setPhase(int _phase)
{
    setClockPhaseType(phaseTypeFromInt(_phase));
}

void QCADClockScheme ::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget){
    Q_UNUSED(option);
    Q_UNUSED(widget);
    QColor zoneColor;
    const bool isNoPhase = myClockPhaseType == Phase_None;
    switch(myClockPhaseType){
        case Phase_None:
            zoneColor = QColor(255, 255, 255, 0);
            break;
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
    if (isNoPhase) {
        painter->setPen(QPen(QColor(80, 80, 80, 150), 1, Qt::DashLine));
    } else {
        painter->setPen(Qt::NoPen);
    }
    painter->setBrush(QBrush(zoneColor));
    painter->drawPath(rectPath);
    if (isNoPhase) {
        painter->drawLine(QPointF(-CLOCK_SCHEME_SIZE_5 / 2, -CLOCK_SCHEME_SIZE_5 / 2),
                          QPointF(CLOCK_SCHEME_SIZE_5 / 2, CLOCK_SCHEME_SIZE_5 / 2));
    }
}


QRectF QCADClockScheme ::boundingRect() const{
    qreal penwidth = 0.5;
    return QRectF(-CLOCK_SCHEME_SIZE_5/2-penwidth/2, -CLOCK_SCHEME_SIZE_5/2-penwidth/2, 
                 CLOCK_SCHEME_SIZE_5+penwidth, CLOCK_SCHEME_SIZE_5+penwidth);
}
