#include "ui/widgets/LayeredStructure3DView.h"

#include <QBrush>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPen>
#include <QPdfWriter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSvgGenerator>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>

namespace {
constexpr qreal kXYScale = 0.62;
constexpr qreal kIsoX = 0.86;
constexpr qreal kIsoY = 0.42;
constexpr qreal kClockRegionSize = 100.0;
constexpr qreal kCellSize = 18.0;
constexpr qreal kGeneratorZ = 0.0;
constexpr qreal kPatternZ = 78.0;
constexpr qreal kFirstCellLayerZ = 128.0;
constexpr qreal kCellLayerStepZ = 7.0;
constexpr int kVisibleCellLayers = 3;

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}
} // namespace

LayeredStructure3DView::LayeredStructure3DView(QWidget *parent)
    : QGraphicsView(parent),
      structureScene(new QGraphicsScene(this))
{
    setObjectName(QStringLiteral("layeredStructure3DView"));
    setScene(structureScene);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(247, 250, 253));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void LayeredStructure3DView::setStructure(int phaseCount,
                                          const QVector<ClockRegionRecord> &clockRegions,
                                          const QVector<QVector<CellRecord>> &cellsByLayer,
                                          const QVector<EncodedTileRecord> &encodedTiles)
{
    activePhaseCount = qBound(2, phaseCount, 8);
    clockRegionRecords = clockRegions;
    encodedTileRecords = encodedTiles;
    layerCells = cellsByLayer;
    while (layerCells.size() < kVisibleCellLayers) {
        layerCells.push_back({});
    }
    if (layerCells.size() > kVisibleCellLayers) {
        layerCells.resize(kVisibleCellLayers);
    }

    cachedBounds = sourceBounds();
    userAdjustedZoom = false;
    rebuildScene();
    fitToStructure();
}

QRectF LayeredStructure3DView::sourceBounds() const
{
    QRectF bounds;

    for (const ClockRegionRecord &clock : clockRegionRecords) {
        const QRectF rect(clock.x - kClockRegionSize / 2.0,
                          clock.y - kClockRegionSize / 2.0,
                          kClockRegionSize,
                          kClockRegionSize);
        bounds = bounds.isValid() ? bounds.united(rect) : rect;
    }

    for (const EncodedTileRecord &tile : encodedTileRecords) {
        const QRectF rect = encodedTileRect(tile);
        bounds = bounds.isValid() ? bounds.united(rect) : rect;
    }

    for (const QVector<CellRecord> &cells : layerCells) {
        for (const CellRecord &cell : cells) {
            const QRectF rect(cell.x - kCellSize / 2.0,
                              cell.y - kCellSize / 2.0,
                              kCellSize,
                              kCellSize);
            bounds = bounds.isValid() ? bounds.united(rect) : rect;
        }
    }

    if (!bounds.isValid() || bounds.isEmpty()) {
        bounds = QRectF(0.0, 0.0, 800.0, 520.0);
    }

    return bounds.adjusted(-120.0, -120.0, 120.0, 120.0);
}

QRectF LayeredStructure3DView::encodedTileRect(const EncodedTileRecord &tile) const
{
    const qreal left = 40.0 + tile.startGridX * CLOCK_SCHEME_SIZE_5 - CLOCK_SCHEME_SIZE_5 / 2.0;
    const qreal top = 40.0 + tile.startGridY * CLOCK_SCHEME_SIZE_5 - CLOCK_SCHEME_SIZE_5 / 2.0;
    return QRectF(left,
                  top,
                  tile.blockSize * CLOCK_SCHEME_SIZE_5,
                  tile.blockSize * CLOCK_SCHEME_SIZE_5);
}

bool LayeredStructure3DView::exportToSvg(const QString &fileName)
{
    if (structureScene == nullptr || fileName.trimmed().isEmpty()) {
        return false;
    }

    const QRectF viewBox = structureScene->itemsBoundingRect().adjusted(-36.0, -36.0, 36.0, 36.0);
    if (!viewBox.isValid() || viewBox.isEmpty()) {
        return false;
    }

    QSvgGenerator generator;
    generator.setFileName(fileName);
    generator.setViewBox(viewBox);
    generator.setSize(QSize(qMax(1, static_cast<int>(std::ceil(viewBox.width()))),
                            qMax(1, static_cast<int>(std::ceil(viewBox.height())))));
    generator.setTitle(QStringLiteral("IFCN 3D Structure"));
    generator.setDescription(QStringLiteral("Layered clock-code and cell structure exported from iFCN."));

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    structureScene->render(&painter, viewBox, viewBox, Qt::KeepAspectRatio);
    return painter.end();
}

bool LayeredStructure3DView::exportToPdf(const QString &fileName)
{
    if (structureScene == nullptr || fileName.trimmed().isEmpty()) {
        return false;
    }

    const QRectF sourceRect = structureScene->itemsBoundingRect().adjusted(-36.0, -36.0, 36.0, 36.0);
    if (!sourceRect.isValid() || sourceRect.isEmpty()) {
        return false;
    }

    const QSizeF pageSize(qMax<qreal>(1.0, std::ceil(sourceRect.width())),
                          qMax<qreal>(1.0, std::ceil(sourceRect.height())));
    QPdfWriter writer(fileName);
    writer.setResolution(72);
    writer.setTitle(QStringLiteral("IFCN 3D Structure"));
    writer.setCreator(QStringLiteral("iFCN"));
    writer.setPageSize(QPageSize(pageSize, QPageSize::Point, QStringLiteral("IFCN 3D Structure")));
    writer.setPageMargins(QMarginsF(0.0, 0.0, 0.0, 0.0), QPageLayout::Point);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    structureScene->render(&painter,
                           QRectF(QPointF(0.0, 0.0), pageSize),
                           sourceRect,
                           Qt::IgnoreAspectRatio);
    return painter.end();
}

QPointF LayeredStructure3DView::project(qreal x, qreal y, qreal z) const
{
    const qreal nx = (x - cachedBounds.left()) * kXYScale;
    const qreal ny = (y - cachedBounds.top()) * kXYScale;
    return QPointF((nx - ny) * kIsoX,
                   (nx + ny) * kIsoY - z);
}

QPolygonF LayeredStructure3DView::isoRect(const QRectF &rect, qreal z) const
{
    QPolygonF polygon;
    polygon << project(rect.left(), rect.top(), z)
            << project(rect.right(), rect.top(), z)
            << project(rect.right(), rect.bottom(), z)
            << project(rect.left(), rect.bottom(), z);
    return polygon;
}

void LayeredStructure3DView::addPlate(const QRectF &rect,
                                      qreal z,
                                      qreal thickness,
                                      const QColor &fill,
                                      const QColor &edge,
                                      qreal opacity)
{
    const QPolygonF top = isoRect(rect, z);
    const QPolygonF lower = isoRect(rect, z - thickness);

    QPen edgePen(edge, 1.2);
    edgePen.setJoinStyle(Qt::RoundJoin);

    QPolygonF rightFace;
    rightFace << top[1] << top[2] << lower[2] << lower[1];
    auto *right = structureScene->addPolygon(rightFace,
                                             QPen(withAlpha(edge, 130), 0.8),
                                             QBrush(withAlpha(fill.darker(118), 170)));
    right->setOpacity(opacity);

    QPolygonF frontFace;
    frontFace << top[2] << top[3] << lower[3] << lower[2];
    auto *front = structureScene->addPolygon(frontFace,
                                             QPen(withAlpha(edge, 130), 0.8),
                                             QBrush(withAlpha(fill.darker(108), 160)));
    front->setOpacity(opacity);

    auto *plate = structureScene->addPolygon(top, edgePen, QBrush(fill));
    plate->setOpacity(opacity);
}

void LayeredStructure3DView::addLabel(const QString &text,
                                      const QPointF &pos,
                                      const QColor &color,
                                      int pointSize,
                                      bool bold)
{
    auto *label = structureScene->addSimpleText(text);
    QFont font = label->font();
    font.setPointSize(pointSize);
    font.setBold(bold);
    label->setFont(font);
    label->setBrush(color);
    label->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    label->setPos(pos);
    label->setZValue(10000.0);
}

void LayeredStructure3DView::addLeaderLabel(const QString &text,
                                            const QPointF &anchor,
                                            const QPointF &labelPos,
                                            const QColor &color,
                                            int pointSize,
                                            bool bold)
{
    QPen leaderPen(withAlpha(color, 150), 1.1);
    leaderPen.setCapStyle(Qt::RoundCap);
    auto *leader = structureScene->addLine(QLineF(anchor, labelPos + QPointF(-8.0, 8.0)),
                                           leaderPen);
    leader->setZValue(9998.0);
    addLabel(text, labelPos, color, pointSize, bold);
}

QColor LayeredStructure3DView::phaseColor(int phase) const
{
    static const QVector<QColor> colors = {
        QColor(111, 205, 132),
        QColor(231, 128, 217),
        QColor(77, 190, 208),
        QColor(238, 209, 101),
        QColor(124, 149, 221),
        QColor(238, 143, 92),
        QColor(136, 201, 166),
        QColor(178, 140, 214)
    };
    if (phase >= 0 && phase < colors.size()) {
        return colors[phase];
    }
    return QColor(189, 198, 209);
}

QColor LayeredStructure3DView::clockPhaseColor(int phase) const
{
    static const QVector<QColor> colors = {
        QColor(CLOCK_ZONE_0),
        QColor(CLOCK_ZONE_1),
        QColor(CLOCK_ZONE_2),
        QColor(CLOCK_ZONE_3)
    };
    if (phase >= 0 && phase < colors.size()) {
        return colors[phase];
    }
    return QColor(172, 172, 172);
}

QColor LayeredStructure3DView::cellColor(const CellRecord &cell) const
{
    switch (cell.type) {
        case CellType::InputCell:
            return QColor(35, 126, 196);
        case CellType::OutputCell:
            return QColor(232, 181, 57);
        case CellType::FixedCell_0:
        case CellType::FixedCell_1:
            return QColor(232, 129, 67);
        case CellType::VerticalCell:
            return QColor(124, 98, 197);
        case CellType::CrossoverCell:
            return QColor(220, 90, 104);
        default:
            return phaseColor(cell.phase).lighter(105);
    }
}

QString LayeredStructure3DView::cellTypeLabel(CellType type) const
{
    switch (type) {
        case CellType::InputCell:
            return QStringLiteral("I");
        case CellType::OutputCell:
            return QStringLiteral("O");
        case CellType::FixedCell_0:
            return QStringLiteral("0");
        case CellType::FixedCell_1:
            return QStringLiteral("1");
        case CellType::VerticalCell:
            return QStringLiteral("V");
        case CellType::CrossoverCell:
            return QStringLiteral("X");
        default:
            return QString();
    }
}

void LayeredStructure3DView::addClockGenerator(const QRectF &bounds)
{
    const QRectF plateRect = bounds.adjusted(-50.0, -50.0, 50.0, 70.0);
    addPlate(plateRect,
             kGeneratorZ,
             28.0,
             QColor(226, 234, 244),
             QColor(98, 112, 132),
             1.0);

    const qreal laneHeight = plateRect.height() / qMax(1, activePhaseCount);
    for (int phase = 0; phase < activePhaseCount; ++phase) {
        const QRectF lane(plateRect.left() + 28.0,
                          plateRect.top() + phase * laneHeight + 8.0,
                          plateRect.width() - 56.0,
                          qMax<qreal>(10.0, laneHeight - 16.0));
        addPlate(lane,
                 kGeneratorZ + 2.0,
                 4.0,
                 withAlpha(clockPhaseColor(phase), 230),
                 clockPhaseColor(phase).darker(145),
                 0.88);
        addLabel(QStringLiteral("P%1").arg(phase),
                 project(plateRect.right() + 14.0, lane.center().y(), kGeneratorZ + 8.0),
                 QColor(48, 60, 78),
                 8,
                 true);
    }

    const QPointF frontLabelPos = project(plateRect.left() + 42.0,
                                          plateRect.bottom() + 28.0,
                                          kGeneratorZ + 16.0);
    addLabel(QStringLiteral("Programmable"),
             frontLabelPos,
             QColor(34, 45, 62),
             12,
             true);
    addLabel(QStringLiteral("%1-phase clock generator").arg(activePhaseCount),
             frontLabelPos + QPointF(0.0, 20.0),
             QColor(79, 92, 111),
             10,
             false);
}

void LayeredStructure3DView::addClockPattern(qreal z)
{
    const QRectF plateRect = cachedBounds.adjusted(-22.0, -22.0, 22.0, 22.0);
    addPlate(plateRect,
             z,
             18.0,
             QColor(245, 248, 252),
             QColor(142, 154, 171),
             0.78);
    const QPointF frontLabelPos = project(plateRect.left() + 42.0,
                                          plateRect.bottom() + 22.0,
                                          z + 14.0);
    addLabel(QStringLiteral("Clock-region"),
             frontLabelPos,
             QColor(52, 66, 88),
             12,
             true);
    addLabel(QStringLiteral("pattern"),
             frontLabelPos + QPointF(0.0, 20.0),
             QColor(79, 92, 111),
             10,
             false);

    for (const ClockRegionRecord &clock : clockRegionRecords) {
        if (clock.phase < 0) {
            continue;
        }
        const QRectF rect(clock.x - kClockRegionSize / 2.0,
                          clock.y - kClockRegionSize / 2.0,
                          kClockRegionSize,
                          kClockRegionSize);
        auto *tile = structureScene->addPolygon(isoRect(rect, z + 4.0),
                                                QPen(clockPhaseColor(clock.phase).darker(160), 0.9),
                                                QBrush(withAlpha(clockPhaseColor(clock.phase), 210)));
        tile->setZValue(z);
    }
}

void LayeredStructure3DView::addEncodedPattern(qreal z)
{
    if (encodedTileRecords.isEmpty()) {
        return;
    }

    for (const EncodedTileRecord &tile : encodedTileRecords) {
        const QRectF rect = encodedTileRect(tile).adjusted(5.0, 5.0, -5.0, -5.0);
        const qreal tileZ = z + 22.0;
        const QColor codecBlue(0, 103, 192);
        addPlate(rect,
                 tileZ,
                 8.0,
                 QColor(218, 237, 255, 82),
                 codecBlue,
                 0.86);

        QPen outlinePen(codecBlue, 2.2);
        outlinePen.setJoinStyle(Qt::RoundJoin);
        auto *outline = structureScene->addPolygon(isoRect(rect, tileZ + 4.0),
                                                   outlinePen,
                                                   Qt::NoBrush);
        outline->setZValue(tileZ + 8.0);

        QPen gridPen(QColor(0, 103, 192, 115), 0.85);
        gridPen.setStyle(Qt::DashLine);
        const int blockSize = qMax(1, tile.blockSize);
        for (int column = 1; column < blockSize; ++column) {
            const qreal x = rect.left() + column * CLOCK_SCHEME_SIZE_5;
            auto *line = structureScene->addLine(QLineF(project(x, rect.top(), tileZ + 5.0),
                                                        project(x, rect.bottom(), tileZ + 5.0)),
                                                 gridPen);
            line->setZValue(tileZ + 9.0);
        }
        for (int row = 1; row < blockSize; ++row) {
            const qreal y = rect.top() + row * CLOCK_SCHEME_SIZE_5;
            auto *line = structureScene->addLine(QLineF(project(rect.left(), y, tileZ + 5.0),
                                                        project(rect.right(), y, tileZ + 5.0)),
                                                 gridPen);
            line->setZValue(tileZ + 9.0);
        }

        addLabel(QStringLiteral("0x%1").arg(tile.hex),
                 project(rect.left() + 14.0, rect.top() + 14.0, tileZ + 13.0),
                 QColor(0, 63, 120),
                 12,
                 true);
    }
}

void LayeredStructure3DView::addCellLayer(int layerIndex, qreal z)
{
    const QRectF plateRect = cachedBounds.adjusted(-10.0, -10.0, 10.0, 10.0);
    addLeaderLabel(QStringLiteral("Cell layer %1").arg(layerIndex + 1),
                   project(plateRect.right(), plateRect.bottom(), z + 8.0),
                   project(plateRect.right() + 24.0,
                           plateRect.bottom() - layerIndex * 42.0,
                           z + 14.0),
                   QColor(58, 70, 90),
                   12,
                   true);

    QVector<CellRecord> cells = layerIndex < layerCells.size()
        ? layerCells[layerIndex]
        : QVector<CellRecord>();
    std::sort(cells.begin(), cells.end(), [](const CellRecord &left, const CellRecord &right) {
        if (left.y != right.y) {
            return left.y < right.y;
        }
        return left.x < right.x;
    });

    for (const CellRecord &cell : cells) {
        addCellBridge(cell, z);
    }
}

void LayeredStructure3DView::addCellBridge(const CellRecord &cell, qreal z)
{
    const qreal half = kCellSize / 2.0;
    const qreal cellZ = z + 8.0;

    QPen pen(cellColor(cell), 1.35);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);

    auto addBridgeLine = [&](const QPointF &a, const QPointF &b, qreal itemZ) {
        auto *line = structureScene->addLine(QLineF(a, b), pen);
        line->setZValue(itemZ);
    };

    static const QPointF dotOffsets[] = {
        QPointF(-4.5, -4.5),
        QPointF(4.5, -4.5),
        QPointF(4.5, 4.5),
        QPointF(-4.5, 4.5)
    };

    auto drawCellShell = [&](qreal cx, qreal cy, qreal shellZ, qreal itemZ) {
        const QPointF topLeft = project(cx - half, cy - half, shellZ);
        const QPointF topRight = project(cx + half, cy - half, shellZ);
        const QPointF bottomRight = project(cx + half, cy + half, shellZ);
        const QPointF bottomLeft = project(cx - half, cy + half, shellZ);
        const QPointF center = project(cx, cy, shellZ);

        addBridgeLine(topLeft, topRight, itemZ);
        addBridgeLine(topRight, bottomRight, itemZ);
        addBridgeLine(bottomRight, bottomLeft, itemZ);
        addBridgeLine(bottomLeft, topLeft, itemZ);

        if (cell.type != CellType::CrossoverCell && cell.type != CellType::VerticalCell) {
            addBridgeLine(topLeft, center, itemZ + 0.5);
            addBridgeLine(topRight, center, itemZ + 0.5);
            addBridgeLine(bottomRight, center, itemZ + 0.5);
            addBridgeLine(bottomLeft, center, itemZ + 0.5);
        }

        QPen dotPen(cellColor(cell), 0.8);
        const QBrush dotBrush(QColor(255, 255, 255, 235));
        for (const QPointF &offset : dotOffsets) {
            const QPointF dotCenter = project(cx + offset.x(), cy + offset.y(), shellZ + 0.1);
            auto *dot = structureScene->addEllipse(QRectF(dotCenter - QPointF(1.7, 1.7),
                                                          QSizeF(3.4, 3.4)),
                                                   dotPen,
                                                   dotBrush);
            dot->setZValue(itemZ + 4.0);
        }
    };

    drawCellShell(cell.x, cell.y, cellZ, cellZ + 10.0);

    if (cell.type == CellType::CrossoverCell) {
        drawCellShell(cell.x + 6.0, cell.y - 6.0, cellZ + 0.2, cellZ + 10.8);
    } else if (cell.type == CellType::VerticalCell) {
        const QPointF lower = project(cell.x, cell.y, cellZ - kCellLayerStepZ * 0.16);
        const QPointF upper = project(cell.x, cell.y, cellZ + kCellLayerStepZ * 0.16);
        QPen pillarPen(cellColor(cell), 1.8);
        pillarPen.setCapStyle(Qt::RoundCap);
        auto *pillar = structureScene->addLine(QLineF(lower, upper), pillarPen);
        pillar->setZValue(cellZ + 12.0);
    }
}

void LayeredStructure3DView::rebuildScene()
{
    structureScene->clear();
    cachedBounds = sourceBounds();

    addClockGenerator(cachedBounds);
    addClockPattern(kPatternZ);
    addEncodedPattern(kPatternZ);
    for (int layer = 0; layer < kVisibleCellLayers; ++layer) {
        addCellLayer(layer, kFirstCellLayerZ + layer * kCellLayerStepZ);
    }

    const QRectF itemsRect = structureScene->itemsBoundingRect().adjusted(-80.0, -80.0, 100.0, 100.0);
    structureScene->setSceneRect(itemsRect);
}

void LayeredStructure3DView::fitToStructure()
{
    if (structureScene == nullptr || structureScene->items().isEmpty()) {
        return;
    }
    resetTransform();
    currentZoom = 1.0;
    const QRectF rect = structureScene->itemsBoundingRect().adjusted(-45.0, -45.0, 45.0, 45.0);
    fitInView(rect, Qt::KeepAspectRatio);
}

void LayeredStructure3DView::zoomBy(qreal factor)
{
    if (factor <= 0.0) {
        return;
    }

    const qreal nextZoom = qBound(0.18, currentZoom * factor, 5.0);
    const qreal applied = nextZoom / currentZoom;
    if (qFuzzyCompare(applied, 1.0)) {
        return;
    }

    userAdjustedZoom = true;
    currentZoom = nextZoom;
    scale(applied, applied);
}

void LayeredStructure3DView::zoomIn()
{
    zoomBy(1.18);
}

void LayeredStructure3DView::zoomOut()
{
    zoomBy(1.0 / 1.18);
}

void LayeredStructure3DView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (!userAdjustedZoom) {
        fitToStructure();
    }
}

void LayeredStructure3DView::wheelEvent(QWheelEvent *event)
{
    const QPoint numDegrees = event->angleDelta() / 8;
    if (numDegrees.y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    zoomBy(numDegrees.y() > 0 ? 1.12 : 1.0 / 1.12);
    event->accept();
}
