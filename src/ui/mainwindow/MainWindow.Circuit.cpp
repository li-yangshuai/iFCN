#include "ui/mainwindow/MainWindow.h"
#include "ui/widgets/CircuitSchematicView.h"

#include <QDir>
#include <QDialog>
#include <QDockWidget>
#include <QGraphicsRectItem>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace {
constexpr int kMappedCellPitch = 20;
constexpr int kMappedCellOrigin = 200;
constexpr int kNodeBlockCells = 5;
constexpr int kHighlightZ = 20000;

int sceneCenterForMappedCell(int mappedCellCoord)
{
    return mappedCellCoord * kMappedCellPitch + kMappedCellOrigin;
}

int mappedCellCoordFromScene(int sceneCoord)
{
    return qRound((sceneCoord - kMappedCellOrigin) / static_cast<double>(kMappedCellPitch));
}

QString edgeKey(int source, int sink)
{
    return QStringLiteral("%1:%2").arg(source).arg(sink);
}

QString safeFileStem(QString text)
{
    text = QFileInfo(text).completeBaseName().trimmed();
    if (text.isEmpty()) {
        text = QStringLiteral("circuit_structure");
    }
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    text = text.trimmed();
    return text.isEmpty() ? QStringLiteral("circuit_structure") : text;
}
} // namespace

void MainWindow::updateCircuitSchematicFromMapping(const GateLevelMapping &mapping)
{
    updateCircuitSchematicFromRawData(mapping.circuitName,
                                      mapping.nodes,
                                      mapping.routes,
                                      mapping.coordPhaseMap,
                                      mapping.mappedRouteCells,
                                      mapping.metadata);
}

void MainWindow::updateCircuitSchematicFromRawData(const QString &circuitName,
                                                   QMap<int, GateLevelMapping::NodeInfo> nodes,
                                                   QMap<QPair<int,int>, QVector<QPoint>> routes,
                                                   QHash<QPoint, int> coordPhaseMap,
                                                   QMap<QPair<int,int>, QVector<QPoint>> mappedRouteCells,
                                                   QMap<QString, QString> metadata)
{
    if (circuitSchematicView == nullptr) {
        return;
    }

    if (gateLevelMapping != nullptr) {
        gateLevelMapping->circuitName = circuitName;
        gateLevelMapping->nodes = nodes;
        gateLevelMapping->routes = routes;
        gateLevelMapping->coordPhaseMap = coordPhaseMap;
        gateLevelMapping->mappedRouteCells = mappedRouteCells;
        gateLevelMapping->metadata = metadata;
    }

    QVector<CircuitSchematicView::NodeRecord> schematicNodes;
    schematicNodes.reserve(nodes.size());
    for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
        const GateLevelMapping::NodeInfo &node = it.value();
        CircuitSchematicView::NodeRecord record;
        record.index = node.index;
        record.name = node.name;
        record.type = node.type;
        record.gridPos = node.pos;
        schematicNodes.push_back(record);
    }

    QVector<CircuitSchematicView::EdgeRecord> schematicEdges;
    QSet<QString> seenEdges;
    for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
        const int source = it.key().first;
        const int sink = it.key().second;
        if (!nodes.contains(source) || !nodes.contains(sink)) {
            continue;
        }
        const QString key = edgeKey(source, sink);
        if (seenEdges.contains(key)) {
            continue;
        }
        seenEdges.insert(key);

        CircuitSchematicView::EdgeRecord edge;
        edge.source = source;
        edge.sink = sink;
        edge.routePath = it.value();
        schematicEdges.push_back(edge);
    }

    QVector<CircuitSchematicView::ClockRecord> schematicClockGrid;
    schematicClockGrid.reserve(coordPhaseMap.size());
    for (auto it = coordPhaseMap.cbegin(); it != coordPhaseMap.cend(); ++it) {
        CircuitSchematicView::ClockRecord clock;
        clock.gridPos = it.key();
        clock.phase = it.value();
        schematicClockGrid.push_back(clock);
    }

    circuitSchematicView->setCircuit(circuitName,
                                     schematicNodes,
                                     schematicEdges,
                                     schematicClockGrid);
    if (circuitSchematicDock != nullptr) {
        const QString title = circuitName.trimmed().isEmpty()
            ? tr("Circuit Structure")
            : tr("Circuit Structure - %1").arg(circuitName);
        circuitSchematicDock->setWindowTitle(title);
        if (circuitSchematicFloatWindow != nullptr) {
            circuitSchematicFloatWindow->setWindowTitle(title);
        }
    }
    clearCircuitNodeHighlight();
    if (gateLevelMapping != nullptr) {
        updateLayoutInfoFromMapping(*gateLevelMapping);
    }
}

void MainWindow::exportCircuitSchematicSvg()
{
    if (circuitSchematicView == nullptr) {
        return;
    }

    const QString circuitName = gateLevelMapping != nullptr
        ? gateLevelMapping->circuitName
        : QString();
    const QString defaultFileName = safeFileStem(circuitName) + QStringLiteral("_structure.pdf");
    const QFileInfo currentInfo(curFile);
    const QString defaultDir = currentInfo.absoluteDir().exists()
        ? currentInfo.absoluteDir().absolutePath()
        : QDir::currentPath();
    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(this,
                                                    tr("Save Circuit Structure"),
                                                    QDir(defaultDir).absoluteFilePath(defaultFileName),
                                                    tr("PDF files (*.pdf);;SVG files (*.svg)"),
                                                    &selectedFilter);
    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo fileInfo(filePath);
    QString suffix = fileInfo.suffix().toLower();
    if (suffix.isEmpty()) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        filePath += QLatin1Char('.') + suffix;
    } else if (suffix != QStringLiteral("svg") && suffix != QStringLiteral("pdf")) {
        suffix = selectedFilter.contains(QStringLiteral("SVG"), Qt::CaseInsensitive)
            ? QStringLiteral("svg")
            : QStringLiteral("pdf");
        filePath += QLatin1Char('.') + suffix;
    }

    const bool saved = suffix == QStringLiteral("pdf")
        ? circuitSchematicView->exportToPdf(filePath)
        : circuitSchematicView->exportToSvg(filePath);
    if (!saved) {
        QMessageBox::warning(this,
                             tr("Save Circuit Structure"),
                             tr("Failed to save circuit structure graphic."));
        return;
    }

    printToStatusBar(tr("Circuit structure saved: %1")
                         .arg(QDir::toNativeSeparators(filePath)));
}

void MainWindow::slotCircuitNodeActivated(int nodeIndex)
{
    highlightCircuitNode(nodeIndex);
}

void MainWindow::slotCircuitEdgeActivated(int sourceNodeIndex, int sinkNodeIndex)
{
    highlightCircuitEdge(sourceNodeIndex, sinkNodeIndex);
}

void MainWindow::slotClearCircuitSelection()
{
    if (circuitSchematicView != nullptr) {
        circuitSchematicView->clearSelectionState();
    }
    clearCircuitNodeHighlight();
    printToStatusBar(tr("Circuit selection cleared"));
}

void MainWindow::clearCircuitNodeHighlight()
{
    if (scene == nullptr) {
        circuitNodeHighlightItems.clear();
        return;
    }

    for (QGraphicsItem *item : circuitNodeHighlightItems) {
        if (item == nullptr) {
            continue;
        }
        if (item->scene() == scene) {
            scene->removeItem(item);
        }
        delete item;
    }
    circuitNodeHighlightItems.clear();
    scene->clearSelection();
}

QRectF MainWindow::circuitNodeSceneBlock(const GateLevelMapping::NodeInfo &node) const
{
    const int baseX = node.pos.x() * kNodeBlockCells;
    const int baseY = node.pos.y() * kNodeBlockCells;
    const int firstX = sceneCenterForMappedCell(baseX);
    const int firstY = sceneCenterForMappedCell(baseY);
    const int lastX = sceneCenterForMappedCell(baseX + kNodeBlockCells - 1);
    const int lastY = sceneCenterForMappedCell(baseY + kNodeBlockCells - 1);

    return QRectF(QPointF(firstX - 12, firstY - 12),
                  QPointF(lastX + 12, lastY + 12)).normalized();
}

void MainWindow::highlightCircuitNode(int nodeIndex)
{
    clearCircuitNodeHighlight();

    if (scene == nullptr || gateLevelMapping == nullptr ||
        !gateLevelMapping->nodes.contains(nodeIndex)) {
        return;
    }

    const GateLevelMapping::NodeInfo node = gateLevelMapping->nodes.value(nodeIndex);
    const QRectF blockRect = circuitNodeSceneBlock(node);

    QPen outerPen(QColor(224, 36, 76), 3.0);
    outerPen.setJoinStyle(Qt::RoundJoin);
    auto *outer = scene->addRect(blockRect, outerPen, QBrush(QColor(224, 36, 76, 28)));
    outer->setZValue(kHighlightZ + 10);
    outer->setAcceptedMouseButtons(Qt::NoButton);
    circuitNodeHighlightItems.push_back(outer);

    scene->update(blockRect.adjusted(-20.0, -20.0, 20.0, 20.0));
    if (view != nullptr) {
        view->centerOn(blockRect.center());
    }

    printToStatusBar(tr("Selected node #%1 %2 (%3), highlighted 5x5 cell block")
                         .arg(node.index)
                         .arg(node.name)
                         .arg(node.type));
}

void MainWindow::highlightCircuitEdge(int sourceNodeIndex, int sinkNodeIndex)
{
    clearCircuitNodeHighlight();

    if (scene == nullptr || gateLevelMapping == nullptr) {
        return;
    }

    const QPair<int, int> routeKey(sourceNodeIndex, sinkNodeIndex);
    QVector<QPoint> mappedCells = gateLevelMapping->mappedRouteCells.value(routeKey);
    if (mappedCells.isEmpty()) {
        const QVector<QPoint> routePath = gateLevelMapping->routes.value(routeKey);
        mappedCells.reserve(routePath.size());
        for (const QPoint &routePoint : routePath) {
            mappedCells.push_back(QPoint(routePoint.x() * kNodeBlockCells + 2,
                                         routePoint.y() * kNodeBlockCells + 2));
        }
    }

    QRectF highlightedBounds;
    int highlightedCells = 0;
    QSet<quint64> seenSceneCells;
    QSet<QGraphicsItem*> selectedCellItems;

    for (const QPoint &mappedCell : mappedCells) {
        const int x = sceneCenterForMappedCell(mappedCell.x());
        const int y = sceneCenterForMappedCell(mappedCell.y());
        const quint64 key = packSceneCoord(x, y);
        if (seenSceneCells.contains(key)) {
            continue;
        }
        seenSceneCells.insert(key);

        const QRectF cellRect(x - 12, y - 12, 24, 24);
        highlightedBounds = highlightedBounds.isValid()
            ? highlightedBounds.united(cellRect)
            : cellRect;

        const QList<QGraphicsItem*> itemsAtCell = scene->items(cellRect,
                                                               Qt::IntersectsItemShape,
                                                               Qt::DescendingOrder,
                                                               QTransform());
        for (QGraphicsItem *item : itemsAtCell) {
            if (item == nullptr || item->type() != QCADCellItem::Type ||
                selectedCellItems.contains(item)) {
                continue;
            }
            item->setSelected(true);
            selectedCellItems.insert(item);
            highlightedBounds = highlightedBounds.united(item->sceneBoundingRect());
        }

        QPen cellPen(QColor(9, 112, 209), 2.4);
        QBrush cellBrush(QColor(9, 112, 209, 52));
        auto *marker = scene->addRect(cellRect, cellPen, cellBrush);
        marker->setZValue(kHighlightZ + 20);
        marker->setAcceptedMouseButtons(Qt::NoButton);
        circuitNodeHighlightItems.push_back(marker);
        ++highlightedCells;
    }

    if (highlightedBounds.isValid()) {
        scene->update(highlightedBounds.adjusted(-30.0, -30.0, 30.0, 30.0));
        if (view != nullptr) {
            view->centerOn(highlightedBounds.center());
        }
    }

    printToStatusBar(tr("Selected route #%1 -> #%2, highlighted %3 mapped cells")
                         .arg(sourceNodeIndex)
                         .arg(sinkNodeIndex)
                         .arg(highlightedCells));
}
