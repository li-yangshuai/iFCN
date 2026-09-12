#include "ui/widgets/CircuitSchematicView.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QHash>
#include <QMap>
#include <QMouseEvent>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPdfWriter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QStyleOptionGraphicsItem>
#include <QSvgGenerator>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kNodeIndexRole = Qt::UserRole + 210;
constexpr int kEdgeSourceRole = Qt::UserRole + 211;
constexpr int kEdgeSinkRole = Qt::UserRole + 212;
constexpr qreal kNodeWidth = 86.0;
constexpr qreal kNodeHeight = 56.0;
constexpr qreal kXStep = 150.0;
constexpr qreal kYStep = 96.0;
constexpr qreal kClockCellSize = 96.0;
constexpr qreal kPi = 3.14159265358979323846;

enum class PinSide {
    Left = 0,
    Right = 1,
    Top = 2,
    Bottom = 3
};

enum class SegmentOrientation {
    Horizontal,
    Vertical
};

QString normalizedType(QString type)
{
    type = type.trimmed().toLower();
    if (type == QStringLiteral("majority") || type == QStringLiteral("majoritygate")) {
        return QStringLiteral("maj");
    }
    if (type == QStringLiteral("buf") || type == QStringLiteral("buffer") ||
        type == QStringLiteral("redundancynode")) {
        return QStringLiteral("redundancy");
    }
    return type;
}

QString displayType(const QString &type)
{
    const QString normalized = normalizedType(type);
    if (normalized == QStringLiteral("maj")) {
        return QStringLiteral("MAJ");
    }
    if (normalized == QStringLiteral("input")) {
        return QStringLiteral("IN");
    }
    if (normalized == QStringLiteral("output")) {
        return QStringLiteral("OUT");
    }
    if (normalized == QStringLiteral("fanout")) {
        return QStringLiteral("FAN");
    }
    if (normalized == QStringLiteral("redundancy")) {
        return QStringLiteral("BUF");
    }
    return normalized.toUpper();
}

QColor fillColorForType(const QString &type)
{
    const QString normalized = normalizedType(type);
    if (normalized == QStringLiteral("input")) {
        return QColor(218, 246, 239);
    }
    if (normalized == QStringLiteral("output")) {
        return QColor(255, 239, 199);
    }
    if (normalized == QStringLiteral("not") || normalized == QStringLiteral("nand") ||
        normalized == QStringLiteral("nor") || normalized == QStringLiteral("xnor")) {
        return QColor(255, 226, 232);
    }
    if (normalized == QStringLiteral("maj")) {
        return QColor(233, 226, 255);
    }
    if (normalized == QStringLiteral("fanout")) {
        return QColor(226, 244, 255);
    }
    if (normalized == QStringLiteral("wire") || normalized == QStringLiteral("redundancy")) {
        return QColor(238, 242, 247);
    }
    return QColor(226, 238, 255);
}

QColor strokeColorForType(const QString &type)
{
    const QString normalized = normalizedType(type);
    if (normalized == QStringLiteral("input")) {
        return QColor(25, 134, 108);
    }
    if (normalized == QStringLiteral("output")) {
        return QColor(185, 118, 0);
    }
    if (normalized == QStringLiteral("not") || normalized == QStringLiteral("nand") ||
        normalized == QStringLiteral("nor") || normalized == QStringLiteral("xnor")) {
        return QColor(192, 54, 82);
    }
    if (normalized == QStringLiteral("maj")) {
        return QColor(108, 76, 196);
    }
    if (normalized == QStringLiteral("fanout")) {
        return QColor(46, 112, 162);
    }
    if (normalized == QStringLiteral("wire") || normalized == QStringLiteral("redundancy")) {
        return QColor(91, 104, 124);
    }
    return QColor(50, 96, 160);
}

qreal pinOffset(int order, int count)
{
    if (count <= 1) {
        return 0.0;
    }

    const qreal usableHeight = kNodeHeight - 18.0;
    const qreal step = usableHeight / static_cast<qreal>(count - 1);
    return -usableHeight / 2.0 + qBound(0, order, count - 1) * step;
}

QString pinBucketKey(int nodeIndex, PinSide side, QChar role)
{
    return QStringLiteral("%1:%2:%3")
        .arg(nodeIndex)
        .arg(static_cast<int>(side))
        .arg(role);
}

QPointF schematicPointForGrid(const QPoint &point)
{
    return QPointF(90.0 + point.x() * kXStep,
                   80.0 + point.y() * kYStep);
}

QRectF clockCellRectForGrid(const QPoint &point)
{
    const QPointF center = schematicPointForGrid(point);
    return QRectF(center.x() - kClockCellSize / 2.0,
                  center.y() - kClockCellSize / 2.0,
                  kClockCellSize,
                  kClockCellSize);
}

QColor clockFillColor(int phase)
{
    switch (phase) {
        case 0:
            return QColor(225, 246, 226);
        case 1:
            return QColor(247, 228, 250);
        case 2:
            return QColor(224, 245, 249);
        case 3:
            return QColor(255, 250, 224);
        default:
            return QColor(241, 244, 248);
    }
}

PinSide sideForDelta(const QPoint &delta)
{
    if (qAbs(delta.x()) >= qAbs(delta.y())) {
        return delta.x() < 0 ? PinSide::Left : PinSide::Right;
    }
    return delta.y() < 0 ? PinSide::Top : PinSide::Bottom;
}

bool nearbySchematicNodes(const QPointF &source, const QPointF &sink)
{
    const qreal dx = qAbs(source.x() - sink.x());
    const qreal dy = qAbs(source.y() - sink.y());
    return (dx > 1.0 || dy > 1.0) &&
           dx <= kXStep + 1.0 &&
           dy <= kYStep + 1.0;
}

bool horizontalFirstFromOutput(PinSide side)
{
    return side == PinSide::Left || side == PinSide::Right;
}

bool horizontalFirstIntoInput(PinSide side)
{
    return side == PinSide::Top || side == PinSide::Bottom;
}

void appendLineIfNeeded(QPainterPath &path, const QPointF &target, QPointF &penultimate)
{
    if (QLineF(path.currentPosition(), target).length() <= 1.0) {
        return;
    }
    penultimate = path.currentPosition();
    path.lineTo(target);
}

void appendOrthogonalPath(QPainterPath &path,
                          const QPointF &target,
                          bool horizontalFirst,
                          QPointF &penultimate)
{
    const QPointF current = path.currentPosition();
    if (QLineF(current, target).length() <= 1.0) {
        return;
    }

    constexpr qreal kAxisTolerance = 0.5;
    if (qAbs(current.x() - target.x()) <= kAxisTolerance ||
        qAbs(current.y() - target.y()) <= kAxisTolerance) {
        appendLineIfNeeded(path, target, penultimate);
        return;
    }

    const QPointF bend = horizontalFirst
        ? QPointF(target.x(), current.y())
        : QPointF(current.x(), target.y());
    appendLineIfNeeded(path, bend, penultimate);
    appendLineIfNeeded(path, target, penultimate);
}


PinSide sourceSideForEdge(const CircuitSchematicView::EdgeRecord &edge,
                          const QHash<int, CircuitSchematicView::NodeRecord> &nodesByIndex)
{
    if (edge.routePath.size() >= 2) {
        return sideForDelta(edge.routePath[1] - edge.routePath[0]);
    }

    const auto source = nodesByIndex.value(edge.source);
    const auto sink = nodesByIndex.value(edge.sink);
    return sideForDelta(sink.gridPos - source.gridPos);
}

PinSide sinkSideForEdge(const CircuitSchematicView::EdgeRecord &edge,
                        const QHash<int, CircuitSchematicView::NodeRecord> &nodesByIndex)
{
    if (edge.routePath.size() >= 2) {
        return sideForDelta(edge.routePath[edge.routePath.size() - 2] -
                            edge.routePath[edge.routePath.size() - 1]);
    }

    const auto source = nodesByIndex.value(edge.source);
    const auto sink = nodesByIndex.value(edge.sink);
    return sideForDelta(source.gridPos - sink.gridPos);
}

QPolygonF arrowHeadForSegment(const QPointF &tail, const QPointF &tip)
{
    QLineF line(tail, tip);
    if (line.length() <= 0.1) {
        return {};
    }

    const qreal angle = std::atan2(line.dy(), line.dx());
    constexpr qreal arrowSize = 10.0;
    const QPointF left = tip - QPointF(std::cos(angle - kPi / 7.0) * arrowSize,
                                       std::sin(angle - kPi / 7.0) * arrowSize);
    const QPointF right = tip - QPointF(std::cos(angle + kPi / 7.0) * arrowSize,
                                        std::sin(angle + kPi / 7.0) * arrowSize);
    QPolygonF polygon;
    polygon << tip << left << right;
    return polygon;
}

bool nodeOrderLess(const CircuitSchematicView::NodeRecord &left,
                   const CircuitSchematicView::NodeRecord &right)
{
    if (left.gridPos.y() != right.gridPos.y()) {
        return left.gridPos.y() < right.gridPos.y();
    }
    if (left.gridPos.x() != right.gridPos.x()) {
        return left.gridPos.x() < right.gridPos.x();
    }
    return left.index < right.index;
}

struct DrawnEdgeRecord {
    int source = -1;
    int sink = -1;
    QPainterPath path;
};

QVector<QLineF> lineSegmentsForPath(const QPainterPath &path)
{
    QVector<QLineF> segments;
    if (path.elementCount() < 2) {
        return segments;
    }

    QPointF previous(path.elementAt(0).x, path.elementAt(0).y);
    for (int index = 1; index < path.elementCount(); ++index) {
        const QPointF current(path.elementAt(index).x, path.elementAt(index).y);
        const QLineF segment(previous, current);
        if (segment.length() > 1.0) {
            segments.push_back(segment);
        }
        previous = current;
    }
    return segments;
}

bool valueWithin(qreal value, qreal first, qreal second, qreal tolerance)
{
    const qreal low = qMin(first, second) - tolerance;
    const qreal high = qMax(first, second) + tolerance;
    return value >= low && value <= high;
}

bool orthogonalIntersection(const QLineF &first,
                            const QLineF &second,
                            QPointF &intersection)
{
    constexpr qreal kAxisTolerance = 0.5;
    constexpr qreal kBoundsTolerance = 1.0;

    const bool firstHorizontal = qAbs(first.y1() - first.y2()) <= kAxisTolerance;
    const bool firstVertical = qAbs(first.x1() - first.x2()) <= kAxisTolerance;
    const bool secondHorizontal = qAbs(second.y1() - second.y2()) <= kAxisTolerance;
    const bool secondVertical = qAbs(second.x1() - second.x2()) <= kAxisTolerance;

    if (firstHorizontal && secondVertical) {
        intersection = QPointF(second.x1(), first.y1());
        return valueWithin(intersection.x(), first.x1(), first.x2(), kBoundsTolerance) &&
               valueWithin(intersection.y(), second.y1(), second.y2(), kBoundsTolerance);
    }
    if (firstVertical && secondHorizontal) {
        intersection = QPointF(first.x1(), second.y1());
        return valueWithin(intersection.y(), first.y1(), first.y2(), kBoundsTolerance) &&
               valueWithin(intersection.x(), second.x1(), second.x2(), kBoundsTolerance);
    }

    return false;
}

bool nearPoint(const QPointF &left, const QPointF &right, qreal tolerance = 2.0)
{
    return QLineF(left, right).length() <= tolerance;
}

bool crossingAtPathTerminal(const QPointF &crossing, const QPainterPath &path)
{
    if (path.elementCount() < 1) {
        return false;
    }

    const QPointF start(path.elementAt(0).x, path.elementAt(0).y);
    const QPointF end(path.elementAt(path.elementCount() - 1).x,
                      path.elementAt(path.elementCount() - 1).y);
    return nearPoint(crossing, start, 8.0) || nearPoint(crossing, end, 8.0);
}

bool horizontalSegment(const QLineF &segment)
{
    constexpr qreal kAxisTolerance = 0.5;
    return qAbs(segment.y1() - segment.y2()) <= kAxisTolerance;
}

bool verticalSegment(const QLineF &segment)
{
    constexpr qreal kAxisTolerance = 0.5;
    return qAbs(segment.x1() - segment.x2()) <= kAxisTolerance;
}

SegmentOrientation crossingMarkerOrientation(const QLineF &first, const QLineF &second)
{
    Q_UNUSED(first);
    Q_UNUSED(second);
    return SegmentOrientation::Horizontal;
}

void addJumpMarker(QGraphicsScene *scene, const QPointF &center, SegmentOrientation orientation)
{
    Q_UNUSED(orientation);
    if (scene == nullptr) {
        return;
    }

    constexpr qreal kRadius = 10.0;
    QPainterPath bridge;
    bridge.moveTo(-kRadius, 0.0);
    bridge.cubicTo(-kRadius * 0.55, -kRadius * 0.95,
                   kRadius * 0.55, -kRadius * 0.95,
                   kRadius, 0.0);

    QPen bridgePen(QColor(78, 91, 110), 2.4);
    bridgePen.setCapStyle(Qt::RoundCap);
    bridgePen.setJoinStyle(Qt::RoundJoin);
    auto *bridgeItem = scene->addPath(bridge, bridgePen);
    bridgeItem->setPos(center);
    bridgeItem->setAcceptedMouseButtons(Qt::NoButton);
    bridgeItem->setZValue(1.8);
}

void addCrossingJumpMarkers(QGraphicsScene *scene, const QVector<DrawnEdgeRecord> &edges)
{
    QSet<quint64> seenCrossings;
    auto packCrossing = [](const QPointF &point) {
        const quint32 x = static_cast<quint32>(qRound(point.x() * 10.0));
        const quint32 y = static_cast<quint32>(qRound(point.y() * 10.0));
        return (static_cast<quint64>(x) << 32) | y;
    };

    for (int leftIndex = 0; leftIndex < edges.size(); ++leftIndex) {
        const QVector<QLineF> leftSegments = lineSegmentsForPath(edges[leftIndex].path);
        for (int rightIndex = leftIndex + 1; rightIndex < edges.size(); ++rightIndex) {
            const DrawnEdgeRecord &left = edges[leftIndex];
            const DrawnEdgeRecord &right = edges[rightIndex];
            if (left.source == right.source || left.sink == right.sink) {
                continue;
            }

            const QVector<QLineF> rightSegments = lineSegmentsForPath(edges[rightIndex].path);
            for (const QLineF &leftSegment : leftSegments) {
                for (const QLineF &rightSegment : rightSegments) {
                    QPointF crossing;
                    if (!orthogonalIntersection(leftSegment, rightSegment, crossing)) {
                        continue;
                    }
                    if (crossingAtPathTerminal(crossing, edges[leftIndex].path) ||
                        crossingAtPathTerminal(crossing, edges[rightIndex].path)) {
                        continue;
                    }

                    const quint64 key = packCrossing(crossing);
                    if (seenCrossings.contains(key)) {
                        continue;
                    }
                    seenCrossings.insert(key);
                    addJumpMarker(scene,
                                  crossing,
                                  crossingMarkerOrientation(leftSegment, rightSegment));
                }
            }
        }
    }
}
} // namespace

class CircuitSchematicNodeItem : public QGraphicsItem
{
public:
    explicit CircuitSchematicNodeItem(const CircuitSchematicView::NodeRecord &record)
        : node(record),
          normalizedNodeType(normalizedType(record.type))
    {
        setFlags(QGraphicsItem::ItemIsSelectable);
        setAcceptHoverEvents(true);
        setData(kNodeIndexRole, node.index);
        setToolTip(QStringLiteral("#%1  %2\n%3  (%4,%5)")
                       .arg(node.index)
                       .arg(node.name)
                       .arg(node.type)
                       .arg(node.gridPos.x())
                       .arg(node.gridPos.y()));
    }

    int nodeIndex() const
    {
        return node.index;
    }

    void setNodeLabelVisible(bool visible)
    {
        if (labelVisible == visible) {
            return;
        }
        labelVisible = visible;
        update();
    }

    QPointF inputAnchor(PinSide side, int order, int count) const
    {
        return anchor(side, order, count);
    }

    QPointF outputAnchor(PinSide side, int order, int count) const
    {
        return anchor(side, order, count);
    }

    QRectF boundingRect() const override
    {
        return QRectF(-kNodeWidth / 2.0,
                      -kNodeHeight / 2.0,
                      kNodeWidth,
                      kNodeHeight + 24.0);
    }

    QPainterPath shape() const override
    {
        QPainterPath path;
        path.addRoundedRect(QRectF(-kNodeWidth / 2.0,
                                   -kNodeHeight / 2.0,
                                   kNodeWidth,
                                   kNodeHeight).adjusted(-4.0, -4.0, 4.0, 4.0),
                            8.0,
                            8.0);
        return path;
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);

        painter->setRenderHint(QPainter::Antialiasing, true);

        const QColor fill = fillColorForType(normalizedNodeType);
        const QColor stroke = strokeColorForType(normalizedNodeType);
        QPen pen(stroke, isSelected() ? 3.2 : 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        pen.setCapStyle(Qt::RoundCap);

        painter->setPen(pen);
        painter->setBrush(fill);
        drawSymbol(painter);

        if (isSelected()) {
            QPen glow(QColor(224, 36, 76, 150), 4.0);
            glow.setJoinStyle(Qt::RoundJoin);
            painter->setPen(glow);
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(boundingRect().adjusted(-6.0, -6.0, 6.0, 6.0), 10.0, 10.0);
        }

        drawText(painter);
    }

private:
    QPointF anchor(PinSide side, int order, int count) const
    {
        const qreal offset = pinOffset(order, count);
        switch (side) {
            case PinSide::Left:
                return pos() + QPointF(-kNodeWidth / 2.0, offset);
            case PinSide::Right:
                return pos() + QPointF(kNodeWidth / 2.0, offset);
            case PinSide::Top:
                return pos() + QPointF(offset, -kNodeHeight / 2.0);
            case PinSide::Bottom:
                return pos() + QPointF(offset, kNodeHeight / 2.0);
        }
        return pos();
    }

    void drawAndGate(QPainter *painter, bool outputBubble)
    {
        QPainterPath path;
        path.moveTo(-34.0, -22.0);
        path.lineTo(-6.0, -22.0);
        path.cubicTo(22.0, -22.0, 36.0, -12.0, 36.0, 0.0);
        path.cubicTo(36.0, 12.0, 22.0, 22.0, -6.0, 22.0);
        path.lineTo(-34.0, 22.0);
        path.closeSubpath();
        painter->drawPath(path);
        if (outputBubble) {
            painter->drawEllipse(QPointF(42.0, 0.0), 5.2, 5.2);
        }
    }

    void drawOrGate(QPainter *painter, bool xorInputCurve, bool outputBubble)
    {
        QPainterPath path;
        path.moveTo(-36.0, -23.0);
        path.cubicTo(-12.0, -19.0, 21.0, -23.0, 38.0, 0.0);
        path.cubicTo(21.0, 23.0, -12.0, 19.0, -36.0, 23.0);
        path.cubicTo(-20.0, 8.0, -20.0, -8.0, -36.0, -23.0);
        path.closeSubpath();
        painter->drawPath(path);

        if (xorInputCurve) {
            QPainterPath xorCurve;
            xorCurve.moveTo(-43.0, -22.0);
            xorCurve.cubicTo(-27.0, -8.0, -27.0, 8.0, -43.0, 22.0);
            painter->drawPath(xorCurve);
        }
        if (outputBubble) {
            painter->drawEllipse(QPointF(44.0, 0.0), 5.2, 5.2);
        }
    }

    void drawNotGate(QPainter *painter)
    {
        QPolygonF triangle;
        triangle << QPointF(-34.0, -24.0) << QPointF(-34.0, 24.0) << QPointF(20.0, 0.0);
        painter->drawPolygon(triangle);
        painter->drawEllipse(QPointF(29.0, 0.0), 6.0, 6.0);
    }

    void drawBufferGate(QPainter *painter)
    {
        QPolygonF triangle;
        triangle << QPointF(-34.0, -24.0) << QPointF(-34.0, 24.0) << QPointF(30.0, 0.0);
        painter->drawPolygon(triangle);
    }

    void drawMajGate(QPainter *painter)
    {
        QPolygonF hex;
        hex << QPointF(-28.0, -24.0)
            << QPointF(22.0, -24.0)
            << QPointF(38.0, 0.0)
            << QPointF(22.0, 24.0)
            << QPointF(-28.0, 24.0)
            << QPointF(-40.0, 0.0);
        painter->drawPolygon(hex);
        painter->drawLine(QPointF(-20.0, 0.0), QPointF(20.0, 0.0));
        painter->drawLine(QPointF(0.0, -18.0), QPointF(0.0, 18.0));
    }

    void drawFanoutGate(QPainter *painter)
    {
        painter->drawEllipse(QPointF(0.0, 0.0), 25.0, 20.0);
        painter->drawLine(QPointF(-30.0, 0.0), QPointF(-8.0, 0.0));
        painter->drawLine(QPointF(5.0, 0.0), QPointF(34.0, -18.0));
        painter->drawLine(QPointF(5.0, 0.0), QPointF(34.0, 18.0));
    }

    void drawPort(QPainter *painter, bool output)
    {
        QPolygonF port;
        if (output) {
            port << QPointF(-38.0, -22.0)
                 << QPointF(20.0, -22.0)
                 << QPointF(38.0, 0.0)
                 << QPointF(20.0, 22.0)
                 << QPointF(-38.0, 22.0);
        } else {
            port << QPointF(-38.0, 0.0)
                 << QPointF(-20.0, -22.0)
                 << QPointF(38.0, -22.0)
                 << QPointF(38.0, 22.0)
                 << QPointF(-20.0, 22.0);
        }
        painter->drawPolygon(port);
    }

    void drawWireNode(QPainter *painter)
    {
        painter->drawRoundedRect(QRectF(-36.0, -18.0, 72.0, 36.0), 8.0, 8.0);
        painter->drawLine(QPointF(-28.0, 0.0), QPointF(28.0, 0.0));
    }

    void drawSymbol(QPainter *painter)
    {
        if (normalizedNodeType == QStringLiteral("input")) {
            drawPort(painter, false);
        } else if (normalizedNodeType == QStringLiteral("output")) {
            drawPort(painter, true);
        } else if (normalizedNodeType == QStringLiteral("and")) {
            drawAndGate(painter, false);
        } else if (normalizedNodeType == QStringLiteral("nand")) {
            drawAndGate(painter, true);
        } else if (normalizedNodeType == QStringLiteral("or")) {
            drawOrGate(painter, false, false);
        } else if (normalizedNodeType == QStringLiteral("nor")) {
            drawOrGate(painter, false, true);
        } else if (normalizedNodeType == QStringLiteral("xor")) {
            drawOrGate(painter, true, false);
        } else if (normalizedNodeType == QStringLiteral("xnor")) {
            drawOrGate(painter, true, true);
        } else if (normalizedNodeType == QStringLiteral("not")) {
            drawNotGate(painter);
        } else if (normalizedNodeType == QStringLiteral("maj")) {
            drawMajGate(painter);
        } else if (normalizedNodeType == QStringLiteral("fanout")) {
            drawFanoutGate(painter);
        } else if (normalizedNodeType == QStringLiteral("wire")) {
            drawWireNode(painter);
        } else if (normalizedNodeType == QStringLiteral("redundancy")) {
            drawBufferGate(painter);
        } else {
            painter->drawRoundedRect(QRectF(-38.0, -22.0, 76.0, 44.0), 8.0, 8.0);
        }
    }

    QRectF typeLabelRect() const
    {
        if (normalizedNodeType == QStringLiteral("not")) {
            return QRectF(-34.0, -10.0, 54.0, 20.0);
        }
        return QRectF(-31.0, -11.0, 62.0, 18.0);
    }

    void drawText(QPainter *painter)
    {
        painter->setPen(QColor(25, 32, 44));

        QFont typeFont = painter->font();
        typeFont.setBold(true);
        typeFont.setPointSize(8);
        painter->setFont(typeFont);
        painter->drawText(typeLabelRect(),
                          Qt::AlignCenter,
                          displayType(normalizedNodeType));

        QFont nameFont = painter->font();
        nameFont.setBold(false);
        nameFont.setPointSize(7);
        painter->setFont(nameFont);
        const QFontMetricsF metrics(nameFont);
        const QString label = QStringLiteral("#%1 %2").arg(node.index).arg(node.name);
        if (labelVisible) {
            painter->drawText(QRectF(-40.0, 29.0, 80.0, 18.0),
                              Qt::AlignCenter,
                              metrics.elidedText(label, Qt::ElideRight, 78.0));
        }
    }

    CircuitSchematicView::NodeRecord node;
    QString normalizedNodeType;
    bool labelVisible = true;
};

class CircuitSchematicEdgeItem : public QGraphicsPathItem
{
public:
    CircuitSchematicEdgeItem(const QPainterPath &path, int source, int sink)
        : QGraphicsPathItem(path)
    {
        setFlag(QGraphicsItem::ItemIsSelectable);
        setAcceptHoverEvents(true);
        setData(kEdgeSourceRole, source);
        setData(kEdgeSinkRole, sink);
        setToolTip(QStringLiteral("%1 -> %2").arg(source).arg(sink));
        setZValue(0.0);
    }

    QPainterPath shape() const override
    {
        QPainterPathStroker stroker;
        stroker.setWidth(12.0);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        return stroker.createStroke(path());
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen linePen(isSelected() ? QColor(224, 36, 76) : QColor(78, 91, 110),
                     isSelected() ? 3.4 : 2.0);
        linePen.setCapStyle(Qt::RoundCap);
        linePen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(linePen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path());

        if (option != nullptr && (option->state & QStyle::State_MouseOver)) {
            QPen hoverPen(QColor(224, 36, 76, 90), 7.0);
            hoverPen.setCapStyle(Qt::RoundCap);
            hoverPen.setJoinStyle(Qt::RoundJoin);
            painter->setPen(hoverPen);
            painter->drawPath(path());
        }
    }
};

CircuitSchematicView::CircuitSchematicView(QWidget *parent)
    : QGraphicsView(parent),
      schematicScene(new QGraphicsScene(this))
{
    setObjectName(QStringLiteral("circuitSchematicView"));
    setScene(schematicScene);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMinimumHeight(310);
    setBackgroundBrush(QColor(250, 252, 255));

    connect(schematicScene, &QGraphicsScene::selectionChanged,
            this, &CircuitSchematicView::handleSelectionChanged);
}

void CircuitSchematicView::setCircuit(const QString &circuitName,
                                      const QVector<NodeRecord> &nodes,
                                      const QVector<EdgeRecord> &edges,
                                      const QVector<ClockRecord> &clockGrid)
{
    Q_UNUSED(circuitName);
    clearCircuit();

    if (nodes.isEmpty() && clockGrid.isEmpty()) {
        schematicScene->setSceneRect(QRectF(0.0, 0.0, 240.0, 160.0));
        return;
    }

    QHash<int, NodeRecord> nodesByIndex;
    QHash<QString, QVector<int>> pinBuckets;
    for (const auto &node : nodes) {
        nodesByIndex.insert(node.index, node);
    }
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
        const auto &edge = edges[edgeIndex];
        if (!nodesByIndex.contains(edge.source) || !nodesByIndex.contains(edge.sink)) {
            continue;
        }
        const PinSide sourceSide = sourceSideForEdge(edge, nodesByIndex);
        const PinSide sinkSide = sinkSideForEdge(edge, nodesByIndex);
        pinBuckets[pinBucketKey(edge.source, sourceSide, QChar('o'))].push_back(edgeIndex);
        pinBuckets[pinBucketKey(edge.sink, sinkSide, QChar('i'))].push_back(edgeIndex);
    }

    QSet<quint64> drawnClockCells;
    auto packGridCoord = [](const QPoint &point) {
        const quint32 ux = static_cast<quint32>(point.x());
        const quint32 uy = static_cast<quint32>(point.y());
        return (static_cast<quint64>(ux) << 32) | uy;
    };
    auto addClockCell = [&](const QPoint &gridPos, int phase) {
        const quint64 key = packGridCoord(gridPos);
        if (drawnClockCells.contains(key)) {
            return;
        }
        drawnClockCells.insert(key);

        const QRectF rect = clockCellRectForGrid(gridPos);
        auto *tile = schematicScene->addRect(rect,
                                             QPen(QColor(188, 198, 211), 1.0),
                                             QBrush(clockFillColor(phase)));
        tile->setAcceptedMouseButtons(Qt::NoButton);
        tile->setZValue(-20.0);

        auto *label = schematicScene->addSimpleText(phase >= 0
                                                        ? QStringLiteral("P%1").arg(phase)
                                                        : QStringLiteral("P-"));
        QFont labelFont = label->font();
        labelFont.setPointSize(7);
        labelFont.setBold(true);
        label->setFont(labelFont);
        label->setBrush(QColor(103, 116, 137));
        label->setPos(rect.left() + 6.0, rect.top() + 4.0);
        label->setAcceptedMouseButtons(Qt::NoButton);
        label->setZValue(-19.0);
    };

    QHash<quint64, int> phaseByClockCell;
    for (const ClockRecord &clock : clockGrid) {
        phaseByClockCell.insert(packGridCoord(clock.gridPos), clock.phase);
        addClockCell(clock.gridPos, clock.phase);
    }
    for (const auto &node : nodes) {
        addClockCell(node.gridPos, phaseByClockCell.value(packGridCoord(node.gridPos), -1));
    }
    for (const auto &edge : edges) {
        for (const QPoint &point : edge.routePath) {
            addClockCell(point, phaseByClockCell.value(packGridCoord(point), -1));
        }
    }

    auto routeSortKey = [&edges, &nodesByIndex](int edgeIndex, bool sourceRole) {
        const auto &edge = edges[edgeIndex];
        if (edge.routePath.size() >= 2) {
            const QPoint adjacent = sourceRole
                ? edge.routePath[1]
                : edge.routePath[edge.routePath.size() - 2];
            return adjacent.y() * 10000 + adjacent.x();
        }
        const int nodeIndex = sourceRole ? edge.sink : edge.source;
        const auto node = nodesByIndex.value(nodeIndex);
        return node.gridPos.y() * 10000 + node.gridPos.x();
    };
    for (auto it = pinBuckets.begin(); it != pinBuckets.end(); ++it) {
        const bool sourceRole = it.key().endsWith(QStringLiteral(":o"));
        std::sort(it.value().begin(), it.value().end(), [&](int left, int right) {
            return routeSortKey(left, sourceRole) < routeSortKey(right, sourceRole);
        });
    }

    for (const auto &node : nodes) {
        auto *item = new CircuitSchematicNodeItem(node);
        item->setNodeLabelVisible(showNodeLabels);
        item->setPos(schematicPointForGrid(node.gridPos));
        item->setZValue(2.0);
        schematicScene->addItem(item);
        nodeItems.insert(node.index, item);
    }

    QVector<DrawnEdgeRecord> drawnEdges;
    drawnEdges.reserve(edges.size());
    for (int edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
        const auto &edge = edges[edgeIndex];
        CircuitSchematicNodeItem *sourceItem = nodeItems.value(edge.source, nullptr);
        CircuitSchematicNodeItem *sinkItem = nodeItems.value(edge.sink, nullptr);
        if (sourceItem != nullptr && sinkItem != nullptr) {
            const PinSide sourceSide = sourceSideForEdge(edge, nodesByIndex);
            const PinSide sinkSide = sinkSideForEdge(edge, nodesByIndex);
            const QVector<int> sourceBucket = pinBuckets.value(pinBucketKey(edge.source, sourceSide, QChar('o')));
            const QVector<int> sinkBucket = pinBuckets.value(pinBucketKey(edge.sink, sinkSide, QChar('i')));
            const QPainterPath path = addEdge(sourceItem,
                                              sinkItem,
                                              edge,
                                              sourceBucket.indexOf(edgeIndex),
                                              qMax(1, sourceBucket.size()),
                                              sinkBucket.indexOf(edgeIndex),
                                              qMax(1, sinkBucket.size()));
            drawnEdges.push_back({edge.source, edge.sink, path});
        }
    }
    addCrossingJumpMarkers(schematicScene, drawnEdges);

    schematicScene->setSceneRect(schematicScene->itemsBoundingRect().adjusted(-28.0, -28.0, 28.0, 28.0));
    fitToCircuit();
}

void CircuitSchematicView::clearCircuit()
{
    suppressSelectionSignal = true;
    schematicScene->clear();
    nodeItems.clear();
    suppressSelectionSignal = false;
}

void CircuitSchematicView::selectNode(int nodeIndex)
{
    CircuitSchematicNodeItem *item = nodeItems.value(nodeIndex, nullptr);
    if (item == nullptr) {
        return;
    }

    suppressSelectionSignal = true;
    schematicScene->clearSelection();
    item->setSelected(true);
    suppressSelectionSignal = false;
    centerOn(item);
}

void CircuitSchematicView::zoomIn()
{
    zoomBy(1.18);
}

void CircuitSchematicView::zoomOut()
{
    zoomBy(1.0 / 1.18);
}

void CircuitSchematicView::fitToCircuit()
{
    userAdjustedZoom = false;
    fitCircuit();
}

void CircuitSchematicView::setNodeLabelsVisible(bool visible)
{
    if (showNodeLabels == visible) {
        return;
    }

    showNodeLabels = visible;
    for (CircuitSchematicNodeItem *item : nodeItems) {
        if (item != nullptr) {
            item->setNodeLabelVisible(visible);
        }
    }
    schematicScene->setSceneRect(schematicScene->itemsBoundingRect().adjusted(-28.0, -28.0, 28.0, 28.0));
}

bool CircuitSchematicView::nodeLabelsVisible() const
{
    return showNodeLabels;
}

void CircuitSchematicView::clearSelectionState()
{
    if (schematicScene == nullptr) {
        return;
    }

    suppressSelectionSignal = true;
    schematicScene->clearSelection();
    suppressSelectionSignal = false;
    pressedNodeIndex = -1;
    pressedEdgeSource = -1;
    pressedEdgeSink = -1;
}

void CircuitSchematicView::mousePressEvent(QMouseEvent *event)
{
    pressedNodeIndex = -1;
    pressedEdgeSource = -1;
    pressedEdgeSink = -1;

    if (event != nullptr && event->button() == Qt::LeftButton) {
        const QList<QGraphicsItem*> pressedItems = items(event->pos());
        for (QGraphicsItem *item : pressedItems) {
            if (item == nullptr) {
                continue;
            }

            const QVariant nodeIndex = item->data(kNodeIndexRole);
            if (nodeIndex.isValid()) {
                pressedNodeIndex = nodeIndex.toInt();
                break;
            }

            const QVariant sourceIndex = item->data(kEdgeSourceRole);
            const QVariant sinkIndex = item->data(kEdgeSinkRole);
            if (sourceIndex.isValid() && sinkIndex.isValid()) {
                pressedEdgeSource = sourceIndex.toInt();
                pressedEdgeSink = sinkIndex.toInt();
                break;
            }
        }
    }

    QGraphicsView::mousePressEvent(event);
}

bool CircuitSchematicView::exportToSvg(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty() || schematicScene == nullptr ||
        schematicScene->items().isEmpty()) {
        return false;
    }

    const QRectF contentRect = schematicScene->itemsBoundingRect().adjusted(-28.0, -28.0, 28.0, 28.0);
    if (!contentRect.isValid() || contentRect.isEmpty()) {
        return false;
    }

    const QSize svgSize(qMax(1, static_cast<int>(std::ceil(contentRect.width()))),
                        qMax(1, static_cast<int>(std::ceil(contentRect.height()))));

    QSvgGenerator svgGenerator;
    svgGenerator.setFileName(filePath);
    svgGenerator.setSize(svgSize);
    svgGenerator.setViewBox(QRect(QPoint(0, 0), svgSize));
    svgGenerator.setTitle(QStringLiteral("iFCN circuit structure"));
    svgGenerator.setDescription(QStringLiteral("Circuit structure schematic exported by iFCN."));

    QPainter painter(&svgGenerator);
    if (!painter.isActive()) {
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(QRectF(QPointF(0.0, 0.0), QSizeF(svgSize)), QColor(250, 252, 255));
    schematicScene->render(&painter,
                           QRectF(QPointF(0.0, 0.0), QSizeF(svgSize)),
                           contentRect);
    painter.end();
    return true;
}

bool CircuitSchematicView::exportToPdf(const QString &filePath) const
{
    if (filePath.trimmed().isEmpty() || schematicScene == nullptr ||
        schematicScene->items().isEmpty()) {
        return false;
    }

    const QRectF contentRect = schematicScene->itemsBoundingRect().adjusted(-28.0, -28.0, 28.0, 28.0);
    if (!contentRect.isValid() || contentRect.isEmpty()) {
        return false;
    }

    const QSizeF pageSize(qMax<qreal>(1.0, std::ceil(contentRect.width())),
                          qMax<qreal>(1.0, std::ceil(contentRect.height())));
    QPdfWriter pdfWriter(filePath);
    pdfWriter.setResolution(72);
    pdfWriter.setTitle(QStringLiteral("iFCN circuit structure"));
    pdfWriter.setCreator(QStringLiteral("iFCN"));
    pdfWriter.setPageSize(QPageSize(pageSize, QPageSize::Point, QStringLiteral("iFCN circuit structure")));
    pdfWriter.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0), QPageLayout::Point);

    QPainter painter(&pdfWriter);
    if (!painter.isActive()) {
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(QRectF(QPointF(0.0, 0.0), pageSize), QColor(250, 252, 255));
    schematicScene->render(&painter,
                           QRectF(QPointF(0.0, 0.0), pageSize),
                           contentRect,
                           Qt::IgnoreAspectRatio);
    painter.end();
    return true;
}

void CircuitSchematicView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!userAdjustedZoom) {
        fitCircuit();
    }
}

void CircuitSchematicView::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    zoomBy(delta > 0 ? 1.12 : 1.0 / 1.12);
    event->accept();
}

void CircuitSchematicView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);

    if (event == nullptr || event->button() != Qt::LeftButton) {
        return;
    }

    if (pressedNodeIndex >= 0) {
        selectNode(pressedNodeIndex);
        emit nodeActivated(pressedNodeIndex);
        pressedNodeIndex = -1;
        pressedEdgeSource = -1;
        pressedEdgeSink = -1;
        return;
    }

    if (pressedEdgeSource >= 0 && pressedEdgeSink >= 0) {
        suppressSelectionSignal = true;
        schematicScene->clearSelection();
        const QList<QGraphicsItem*> clickedItems = items(event->pos());
        for (QGraphicsItem *item : clickedItems) {
            if (item == nullptr) {
                continue;
            }
            const QVariant sourceIndex = item->data(kEdgeSourceRole);
            const QVariant sinkIndex = item->data(kEdgeSinkRole);
            if (sourceIndex.isValid() && sinkIndex.isValid() &&
                sourceIndex.toInt() == pressedEdgeSource &&
                sinkIndex.toInt() == pressedEdgeSink) {
                item->setSelected(true);
                break;
            }
        }
        suppressSelectionSignal = false;
        emit edgeActivated(pressedEdgeSource, pressedEdgeSink);
        pressedNodeIndex = -1;
        pressedEdgeSource = -1;
        pressedEdgeSink = -1;
        return;
    }

    const QList<QGraphicsItem*> clickedItems = items(event->pos());
    for (QGraphicsItem *item : clickedItems) {
        if (item == nullptr) {
            continue;
        }
        const QVariant nodeIndex = item->data(kNodeIndexRole);
        if (nodeIndex.isValid()) {
            suppressSelectionSignal = true;
            schematicScene->clearSelection();
            item->setSelected(true);
            suppressSelectionSignal = false;
            emit nodeActivated(nodeIndex.toInt());
            return;
        }

        const QVariant sourceIndex = item->data(kEdgeSourceRole);
        const QVariant sinkIndex = item->data(kEdgeSinkRole);
        if (sourceIndex.isValid() && sinkIndex.isValid()) {
            suppressSelectionSignal = true;
            schematicScene->clearSelection();
            item->setSelected(true);
            suppressSelectionSignal = false;
            emit edgeActivated(sourceIndex.toInt(), sinkIndex.toInt());
            return;
        }
    }
}

void CircuitSchematicView::handleSelectionChanged()
{
    if (suppressSelectionSignal) {
        return;
    }

    const QList<QGraphicsItem*> selected = schematicScene->selectedItems();
    for (QGraphicsItem *item : selected) {
        const QVariant nodeIndex = item->data(kNodeIndexRole);
        if (nodeIndex.isValid()) {
            emit nodeActivated(nodeIndex.toInt());
            return;
        }

        const QVariant sourceIndex = item->data(kEdgeSourceRole);
        const QVariant sinkIndex = item->data(kEdgeSinkRole);
        if (sourceIndex.isValid() && sinkIndex.isValid()) {
            emit edgeActivated(sourceIndex.toInt(), sinkIndex.toInt());
            return;
        }
    }
}

void CircuitSchematicView::fitCircuit()
{
    if (schematicScene == nullptr || schematicScene->items().isEmpty()) {
        return;
    }

    const QRectF target = schematicScene->itemsBoundingRect().adjusted(-24.0, -24.0, 24.0, 24.0);
    if (!target.isValid() || target.isEmpty()) {
        return;
    }

    resetTransform();
    fitInView(target, Qt::KeepAspectRatio);
    currentZoom = transform().m11();
}

void CircuitSchematicView::zoomBy(qreal factor)
{
    if (factor <= 0.0) {
        return;
    }

    const qreal nextZoom = qBound(0.15, currentZoom * factor, 5.0);
    const qreal appliedFactor = nextZoom / currentZoom;
    if (qFuzzyCompare(appliedFactor, 1.0)) {
        return;
    }

    userAdjustedZoom = true;
    scale(appliedFactor, appliedFactor);
    currentZoom = nextZoom;
}

QPainterPath CircuitSchematicView::addEdge(CircuitSchematicNodeItem *sourceItem,
                                           CircuitSchematicNodeItem *sinkItem,
                                           const EdgeRecord &edge,
                                           int sourceOrder,
                                           int sourceCount,
                                           int sinkOrder,
                                           int sinkCount)
{
    const PinSide sourceSide = edge.routePath.size() >= 2
        ? sideForDelta(edge.routePath[1] - edge.routePath[0])
        : (sinkItem->pos().x() >= sourceItem->pos().x() ? PinSide::Right : PinSide::Left);
    const PinSide sinkSide = edge.routePath.size() >= 2
        ? sideForDelta(edge.routePath[edge.routePath.size() - 2] -
                       edge.routePath[edge.routePath.size() - 1])
        : (sourceItem->pos().x() <= sinkItem->pos().x() ? PinSide::Left : PinSide::Right);

    const QPointF start = sourceItem->outputAnchor(sourceSide, qMax(0, sourceOrder), qMax(1, sourceCount));
    const QPointF end = sinkItem->inputAnchor(sinkSide, qMax(0, sinkOrder), qMax(1, sinkCount));

    QPainterPath path(start);
    QPointF penultimate = start;

    const bool followRoutePath = edge.routePath.size() > 2 &&
                                 !nearbySchematicNodes(sourceItem->pos(), sinkItem->pos());
    if (followRoutePath) {
        for (int index = 1; index < edge.routePath.size() - 1; ++index) {
            const QPointF routePoint = schematicPointForGrid(edge.routePath[index]);
            const bool horizontalFirst = index == 1
                ? horizontalFirstFromOutput(sourceSide)
                : qAbs(routePoint.x() - path.currentPosition().x()) >=
                      qAbs(routePoint.y() - path.currentPosition().y());
            appendOrthogonalPath(path, routePoint, horizontalFirst, penultimate);
        }
        appendOrthogonalPath(path, end, horizontalFirstIntoInput(sinkSide), penultimate);
    } else {
        appendOrthogonalPath(path, end, horizontalFirstFromOutput(sourceSide), penultimate);
    }

    auto *wire = new CircuitSchematicEdgeItem(path, edge.source, edge.sink);
    schematicScene->addItem(wire);

    const QPolygonF head = arrowHeadForSegment(penultimate, end);
    if (!head.isEmpty()) {
        auto *arrow = new QGraphicsPolygonItem(head);
        arrow->setFlag(QGraphicsItem::ItemIsSelectable);
        arrow->setData(kEdgeSourceRole, edge.source);
        arrow->setData(kEdgeSinkRole, edge.sink);
        arrow->setPen(Qt::NoPen);
        arrow->setBrush(QColor(78, 91, 110));
        arrow->setZValue(1.0);
        schematicScene->addItem(arrow);
    }

    return path;
}
