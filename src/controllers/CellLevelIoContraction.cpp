#include "controllers/CellLevelIoContraction.h"

#include <QHash>
#include <QDebug>
#include <QSet>

#include <algorithm>
#include <limits>

namespace {
constexpr int kPitch = GRID_SIZE;
constexpr int kClockTileSize = CLOCK_SCHEME_SIZE_5;
constexpr int kClockEdgeOffset = 2 * GRID_SIZE;

quint64 positionKey(int x, int y)
{
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32) |
           static_cast<quint32>(y);
}

int positiveModulo(int value, int modulus)
{
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

bool isIo(CellType type)
{
    return type == CellType::InputCell || type == CellType::OutputCell;
}

bool isFixed(CellType type)
{
    return type == CellType::FixedCell_0 || type == CellType::FixedCell_1;
}

bool isDefaultFiveByFiveEdgePort(int x, int y)
{
    // The global 5x5 template is centred at coordinate 40 modulo 100.
    const int localX = positiveModulo(x, kClockTileSize);
    const int localY = positiveModulo(y, kClockTileSize);
    return ((localX == 0 || localX == 4 * GRID_SIZE) && localY == 2 * GRID_SIZE) ||
           ((localY == 0 || localY == 4 * GRID_SIZE) && localX == 2 * GRID_SIZE);
}

int gridIndexForCoordinate(int coordinate)
{
    // Keep this aligned with QCADScene::clockCenterForPosition(): a 5x5
    // clock grid spans five 20-unit cell sites, with a half-cell margin.
    const int shifted = coordinate + kPitch / 2;
    int quotient = shifted / kClockTileSize;
    if (shifted < 0 && shifted % kClockTileSize != 0) {
        --quotient;
    }
    return quotient;
}

void finalizeStatistics(const QVector<QVector<CellLevelIoCell>> &cellsByLayer,
                        CellLevelIoContractionStats &stats)
{
    bool haveBounds = false;
    int minGridX = std::numeric_limits<int>::max();
    int minGridY = std::numeric_limits<int>::max();
    int maxGridX = std::numeric_limits<int>::min();
    int maxGridY = std::numeric_limits<int>::min();

    stats.cellsAfter = 0;
    stats.inputCount = 0;
    stats.outputCount = 0;
    stats.nonEmptyLayers = 0;
    stats.crossoverCells = 0;
    for (const auto &layer : cellsByLayer) {
        if (!layer.isEmpty()) {
            ++stats.nonEmptyLayers;
        }
        stats.cellsAfter += static_cast<qulonglong>(layer.size());
        for (const CellLevelIoCell &cell : layer) {
            haveBounds = true;
            const int gridX = gridIndexForCoordinate(cell.x);
            const int gridY = gridIndexForCoordinate(cell.y);
            minGridX = qMin(minGridX, gridX);
            minGridY = qMin(minGridY, gridY);
            maxGridX = qMax(maxGridX, gridX);
            maxGridY = qMax(maxGridY, gridY);
            if (cell.type == CellType::InputCell) {
                ++stats.inputCount;
            } else if (cell.type == CellType::OutputCell) {
                ++stats.outputCount;
            }
            if (cell.type == CellType::CrossoverCell ||
                (cell.layer > 0 && cell.type == CellType::VerticalCell)) {
                ++stats.crossoverCells;
            }
        }
    }

    if (haveBounds) {
        stats.widthInGrids = maxGridX - minGridX + 1;
        stats.heightInGrids = maxGridY - minGridY + 1;
    }
}

void flattenRedundantCrossovers(QVector<QVector<CellLevelIoCell>> &cellsByLayer)
{
    if (cellsByLayer.size() < 2 || cellsByLayer[0].isEmpty()) {
        return;
    }

    struct CellRef {
        int layer = 0;
        int index = 0;
    };
    const auto refKey = [](const CellRef &ref) {
        return (static_cast<quint64>(static_cast<quint32>(ref.layer)) << 32) |
               static_cast<quint32>(ref.index);
    };
    static const QPoint offsets[] = {
        QPoint(-kPitch, 0), QPoint(kPitch, 0),
        QPoint(0, -kPitch), QPoint(0, kPitch),
    };

    QVector<QHash<quint64, int>> indicesByLayer(cellsByLayer.size());
    for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
        indicesByLayer[layer].reserve(cellsByLayer[layer].size());
        for (int index = 0; index < cellsByLayer[layer].size(); ++index) {
            const CellLevelIoCell &cell = cellsByLayer[layer][index];
            indicesByLayer[layer].insert(positionKey(cell.x, cell.y), index);
        }
    }

    const auto verticalNeighbor = [&](const CellRef &ref, int nextLayer, CellRef &neighbor) {
        if (nextLayer < 0 || nextLayer >= cellsByLayer.size()) {
            return false;
        }
        const CellLevelIoCell &cell = cellsByLayer[ref.layer][ref.index];
        if (cell.type != CellType::VerticalCell) {
            return false;
        }
        const auto it = indicesByLayer[nextLayer].constFind(positionKey(cell.x, cell.y));
        if (it == indicesByLayer[nextLayer].constEnd() ||
            cellsByLayer[nextLayer][it.value()].type != CellType::VerticalCell) {
            return false;
        }
        neighbor = {nextLayer, it.value()};
        return true;
    };

    const auto upperNeighbors = [&](const CellRef &ref) {
        QVector<CellRef> neighbors;
        const CellLevelIoCell &cell = cellsByLayer[ref.layer][ref.index];
        CellRef below;
        CellRef above;
        const bool hasBelow = verticalNeighbor(ref, ref.layer - 1, below);
        const bool hasAbove = verticalNeighbor(ref, ref.layer + 1, above);
        if (cell.type != CellType::VerticalCell || !(hasBelow && hasAbove)) {
            for (const QPoint &offset : offsets) {
                const auto it = indicesByLayer[ref.layer].constFind(
                    positionKey(cell.x + offset.x(), cell.y + offset.y()));
                if (it != indicesByLayer[ref.layer].constEnd()) {
                    neighbors.push_back({ref.layer, it.value()});
                }
            }
        }
        if (hasBelow && below.layer > 0) {
            neighbors.push_back(below);
        }
        if (hasAbove) {
            neighbors.push_back(above);
        }
        return neighbors;
    };

    QSet<quint64> visited;
    QVector<QSet<int>> cellsToRemove(cellsByLayer.size());
    for (int startLayer = 1; startLayer < cellsByLayer.size(); ++startLayer) {
        for (int startIndex = 0; startIndex < cellsByLayer[startLayer].size(); ++startIndex) {
            const CellRef start{startLayer, startIndex};
            if (visited.contains(refKey(start))) {
                continue;
            }

            QVector<CellRef> component;
            QVector<CellRef> queue{start};
            visited.insert(refKey(start));
            for (int cursor = 0; cursor < queue.size(); ++cursor) {
                const CellRef current = queue[cursor];
                component.push_back(current);
                for (const CellRef &neighbor : upperNeighbors(current)) {
                    const quint64 key = refKey(neighbor);
                    if (!visited.contains(key)) {
                        visited.insert(key);
                        queue.push_back(neighbor);
                    }
                }
            }

            bool hasCrossoverCell = false;
            QHash<quint64, CellLevelIoCell> projectedCells;
            QSet<quint64> verticalPositions;
            for (const CellRef &ref : component) {
                const CellLevelIoCell &cell = cellsByLayer[ref.layer][ref.index];
                const quint64 key = positionKey(cell.x, cell.y);
                projectedCells.insert(key, cell);
                if (cell.type == CellType::CrossoverCell) {
                    hasCrossoverCell = true;
                }
                if (cell.type == CellType::VerticalCell) {
                    verticalPositions.insert(key);
                }
            }
            if (!hasCrossoverCell) {
                continue;
            }

            bool canFlatten = true;
            int attachments = 0;
            for (auto it = projectedCells.constBegin(); it != projectedCells.constEnd(); ++it) {
                const auto lower = indicesByLayer[0].constFind(it.key());
                if (lower == indicesByLayer[0].constEnd()) {
                    continue;
                }
                const CellType lowerType = cellsByLayer[0][lower.value()].type;
                const bool attachmentCell = lowerType == CellType::VerticalCell ||
                                            lowerType == CellType::InputCell ||
                                            lowerType == CellType::OutputCell;
                if (!attachmentCell || !verticalPositions.contains(it.key())) {
                    canFlatten = false;
                    break;
                }
                ++attachments;
            }
            if (!canFlatten || attachments < 2) {
                continue;
            }

            for (auto it = projectedCells.constBegin(); it != projectedCells.constEnd(); ++it) {
                auto lower = indicesByLayer[0].find(it.key());
                if (lower != indicesByLayer[0].end()) {
                    CellLevelIoCell &lowerCell = cellsByLayer[0][lower.value()];
                    if (lowerCell.type == CellType::VerticalCell) {
                        lowerCell.type = CellType::NormalCell;
                    }
                    continue;
                }
                CellLevelIoCell lowered = it.value();
                lowered.layer = 0;
                lowered.type = CellType::NormalCell;
                lowered.name.clear();
                const int newIndex = cellsByLayer[0].size();
                cellsByLayer[0].push_back(lowered);
                indicesByLayer[0].insert(it.key(), newIndex);
            }
            for (const CellRef &ref : component) {
                cellsToRemove[ref.layer].insert(ref.index);
            }
        }
    }

    for (int layer = 1; layer < cellsByLayer.size(); ++layer) {
        if (cellsToRemove[layer].isEmpty()) {
            continue;
        }
        QVector<CellLevelIoCell> retained;
        retained.reserve(cellsByLayer[layer].size() - cellsToRemove[layer].size());
        for (int index = 0; index < cellsByLayer[layer].size(); ++index) {
            if (!cellsToRemove[layer].contains(index)) {
                retained.push_back(cellsByLayer[layer][index]);
            }
        }
        cellsByLayer[layer] = std::move(retained);
    }
}

struct DoglegCandidate {
    bool horizontal = true;
    int fixedCoordinate = 0;
    int beginCoordinate = 0;
    int endCoordinate = 0;
    int direction = 0;
    int shiftSteps = 0;
    bool anchorBegin = false;
    bool anchorEnd = false;
    bool reuseTargetSegment = false;
    bool movesOuterBoundaryInward = false;
};

QPoint orientedPoint(bool horizontal, int along, int fixed)
{
    return horizontal ? QPoint(along, fixed) : QPoint(fixed, along);
}

int inferTwoDdWavePhaseOffset(const QVector<QVector<CellLevelIoCell>> &cellsByLayer)
{
    int inferredOffset = -1;
    int checkedCells = 0;
    for (const auto &layer : cellsByLayer) {
        for (const CellLevelIoCell &cell : layer) {
            if (isIo(cell.type) || isFixed(cell.type)) {
                continue;
            }
            const int offset = positiveModulo(
                cell.phase - gridIndexForCoordinate(cell.x) - gridIndexForCoordinate(cell.y),
                4);
            if (inferredOffset < 0) {
                inferredOffset = offset;
            } else if (inferredOffset != offset) {
                return -1;
            }
            ++checkedCells;
        }
    }
    return checkedCells > 0 ? inferredOffset : -1;
}

QVector<DoglegCandidate> findDoglegCandidates(
    const QVector<QVector<CellLevelIoCell>> &cellsByLayer)
{
    QVector<DoglegCandidate> candidates;
    if (cellsByLayer.isEmpty() || cellsByLayer[0].isEmpty()) {
        return candidates;
    }

    const auto &mainLayer = cellsByLayer[0];
    QHash<quint64, int> mainIndex;
    QSet<quint64> allPositions;
    QSet<quint64> upperPositions;
    mainIndex.reserve(mainLayer.size());
    for (int index = 0; index < mainLayer.size(); ++index) {
        mainIndex.insert(positionKey(mainLayer[index].x, mainLayer[index].y), index);
    }
    for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
        for (const CellLevelIoCell &cell : cellsByLayer[layer]) {
            const quint64 key = positionKey(cell.x, cell.y);
            allPositions.insert(key);
            if (layer > 0) {
                upperPositions.insert(key);
            }
        }
    }

    const auto cellAt = [&](const QPoint &point) -> const CellLevelIoCell * {
        const auto it = mainIndex.constFind(positionKey(point.x(), point.y()));
        return it == mainIndex.constEnd() ? nullptr : &mainLayer[it.value()];
    };
    const auto isNormalAt = [&](const QPoint &point) {
        const CellLevelIoCell *cell = cellAt(point);
        return cell != nullptr && cell->type == CellType::NormalCell;
    };
    const auto clearOnAllLayers = [&](const QPoint &point) {
        return !allPositions.contains(positionKey(point.x(), point.y()));
    };
    const auto protectedDeviceNear = [&](const QPoint &point) {
        for (int dx = -2 * kPitch; dx <= 2 * kPitch; dx += kPitch) {
            for (int dy = -2 * kPitch; dy <= 2 * kPitch; dy += kPitch) {
                if (qAbs(dx) + qAbs(dy) > 2 * kPitch) {
                    continue;
                }
                const CellLevelIoCell *nearby = cellAt(point + QPoint(dx, dy));
                if (nearby != nullptr && isFixed(nearby->type)) {
                    return true;
                }
            }
        }
        return false;
    };

    int minimumGrid[2] = {std::numeric_limits<int>::max(),
                          std::numeric_limits<int>::max()};
    int maximumGrid[2] = {std::numeric_limits<int>::min(),
                          std::numeric_limits<int>::min()};
    for (const auto &layer : cellsByLayer) {
        for (const CellLevelIoCell &cell : layer) {
            const int grids[2] = {gridIndexForCoordinate(cell.x),
                                  gridIndexForCoordinate(cell.y)};
            for (int axis = 0; axis < 2; ++axis) {
                minimumGrid[axis] = qMin(minimumGrid[axis], grids[axis]);
                maximumGrid[axis] = qMax(maximumGrid[axis], grids[axis]);
            }
        }
    }

    const int gridSteps = kClockTileSize / kPitch;
    for (const bool horizontal : {true, false}) {
        const int fixedAxis = horizontal ? 1 : 0;
        for (const CellLevelIoCell &startCell : mainLayer) {
            if (startCell.type != CellType::NormalCell) {
                continue;
            }
            const int begin = horizontal ? startCell.x : startCell.y;
            const int fixed = horizontal ? startCell.y : startCell.x;
            if (!isNormalAt(orientedPoint(horizontal, begin + kPitch, fixed))) {
                continue;
            }

            const QPoint beginNegative = orientedPoint(horizontal, begin, fixed - kPitch);
            const QPoint beginPositive = orientedPoint(horizontal, begin, fixed + kPitch);
            const bool beginTurnsNegative = isNormalAt(beginNegative);
            const bool beginTurnsPositive = isNormalAt(beginPositive);
            if ((!beginTurnsNegative && cellAt(beginNegative) != nullptr) ||
                (!beginTurnsPositive && cellAt(beginPositive) != nullptr) ||
                (!beginTurnsNegative && !beginTurnsPositive)) {
                continue;
            }

            int end = begin + kPitch;
            while (isNormalAt(orientedPoint(horizontal, end, fixed))) {
                const QPoint endNegative = orientedPoint(horizontal, end, fixed - kPitch);
                const QPoint endPositive = orientedPoint(horizontal, end, fixed + kPitch);
                if (cellAt(endNegative) != nullptr || cellAt(endPositive) != nullptr) {
                    break;
                }
                end += kPitch;
            }
            if (end - begin < kClockTileSize ||
                !isNormalAt(orientedPoint(horizontal, end, fixed))) {
                continue;
            }

            const QPoint endNegative = orientedPoint(horizontal, end, fixed - kPitch);
            const QPoint endPositive = orientedPoint(horizontal, end, fixed + kPitch);
            const bool endTurnsNegative = isNormalAt(endNegative);
            const bool endTurnsPositive = isNormalAt(endPositive);
            if ((!endTurnsNegative && cellAt(endNegative) != nullptr) ||
                (!endTurnsPositive && cellAt(endPositive) != nullptr)) {
                continue;
            }
            const bool canMoveNegative = beginTurnsNegative && endTurnsNegative;
            const bool canMovePositive = beginTurnsPositive && endTurnsPositive;
            const int direction = canMoveNegative != canMovePositive
                ? (canMoveNegative ? -1 : 1)
                : 0;
            if (direction == 0) {
                continue;
            }

            const CellLevelIoCell *beforeBegin = cellAt(
                orientedPoint(horizontal, begin - kPitch, fixed));
            const CellLevelIoCell *afterEnd = cellAt(
                orientedPoint(horizontal, end + kPitch, fixed));
            const bool anchorBegin = (beginTurnsNegative && beginTurnsPositive) ||
                                     (beforeBegin != nullptr &&
                                      beforeBegin->type == CellType::NormalCell);
            const bool anchorEnd = (endTurnsNegative && endTurnsPositive) ||
                                   (afterEnd != nullptr &&
                                    afterEnd->type == CellType::NormalCell);
            if ((beforeBegin != nullptr && !anchorBegin) ||
                (afterEnd != nullptr && !anchorEnd) ||
                (anchorBegin && anchorEnd) ||
                protectedDeviceNear(orientedPoint(horizontal, begin, fixed)) ||
                protectedDeviceNear(orientedPoint(horizontal, end, fixed))) {
                continue;
            }

            bool oldSegmentLegal = true;
            for (int along = begin; along <= end; along += kPitch) {
                const QPoint oldPoint = orientedPoint(horizontal, along, fixed);
                if (upperPositions.contains(positionKey(oldPoint.x(), oldPoint.y()))) {
                    oldSegmentLegal = false;
                    break;
                }
            }
            bool foundShift = false;
            for (int shiftSteps = gridSteps;
                 oldSegmentLegal && !foundShift && shiftSteps >= 1; --shiftSteps) {
                bool legal = true;
                for (int step = 1; legal && step <= shiftSteps; ++step) {
                    const int legFixed = fixed + direction * step * kPitch;
                    for (const int along : {begin, end}) {
                        const QPoint legPoint = orientedPoint(horizontal, along, legFixed);
                        if (!isNormalAt(legPoint) ||
                            (step < shiftSteps &&
                             upperPositions.contains(positionKey(legPoint.x(), legPoint.y())))) {
                            legal = false;
                            break;
                        }
                        if (step < shiftSteps &&
                            (cellAt(orientedPoint(horizontal, along - kPitch, legFixed)) != nullptr ||
                             cellAt(orientedPoint(horizontal, along + kPitch, legFixed)) != nullptr)) {
                            legal = false;
                            break;
                        }
                    }
                }

                const int targetFixed = fixed + direction * shiftSteps * kPitch;
                if (legal) {
                    for (const int along : {begin, end}) {
                        if (!isNormalAt(orientedPoint(
                                horizontal, along, targetFixed + direction * kPitch))) {
                            legal = false;
                        }
                    }
                }

                bool targetIsEmpty = true;
                bool targetIsExistingSegment = true;
                for (int along = begin + kPitch; legal && along < end; along += kPitch) {
                    const QPoint target = orientedPoint(horizontal, along, targetFixed);
                    const bool empty = clearOnAllLayers(target);
                    const bool existingNormal = isNormalAt(target) &&
                        !upperPositions.contains(positionKey(target.x(), target.y()));
                    targetIsEmpty = targetIsEmpty && empty;
                    targetIsExistingSegment = targetIsExistingSegment && existingNormal;
                    if (!empty && !existingNormal) {
                        legal = false;
                    }
                }
                if (legal && !targetIsEmpty && !targetIsExistingSegment) {
                    legal = false;
                }
                if (legal && targetIsEmpty) {
                    if (cellAt(orientedPoint(horizontal, begin - kPitch, targetFixed)) != nullptr ||
                        cellAt(orientedPoint(horizontal, end + kPitch, targetFixed)) != nullptr) {
                        legal = false;
                    }
                    for (int along = begin + kPitch; legal && along < end; along += kPitch) {
                        const int negativeFixed = targetFixed - kPitch;
                        const int positiveFixed = targetFixed + kPitch;
                        const bool negativeClear = negativeFixed == fixed ||
                            cellAt(orientedPoint(horizontal, along, negativeFixed)) == nullptr;
                        const bool positiveClear = positiveFixed == fixed ||
                            cellAt(orientedPoint(horizontal, along, positiveFixed)) == nullptr;
                        if (!negativeClear || !positiveClear) {
                            legal = false;
                        }
                    }
                }
                if (!legal) {
                    continue;
                }

                const int fixedGrid = gridIndexForCoordinate(fixed);
                candidates.push_back({horizontal, fixed, begin, end, direction,
                                      shiftSteps, anchorBegin, anchorEnd,
                                      targetIsExistingSegment,
                                      !anchorBegin && !anchorEnd &&
                                      ((fixedGrid == minimumGrid[fixedAxis] && direction > 0) ||
                                       (fixedGrid == maximumGrid[fixedAxis] && direction < 0))});
                foundShift = true;
            }
            if (!foundShift) {
                continue;
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const DoglegCandidate &lhs,
                                                       const DoglegCandidate &rhs) {
        if (lhs.reuseTargetSegment != rhs.reuseTargetSegment) {
            return lhs.reuseTargetSegment;
        }
        if (lhs.movesOuterBoundaryInward != rhs.movesOuterBoundaryInward) {
            return lhs.movesOuterBoundaryInward;
        }
        if (lhs.shiftSteps != rhs.shiftSteps) {
            return lhs.shiftSteps > rhs.shiftSteps;
        }
        const int lhsSpan = lhs.endCoordinate - lhs.beginCoordinate;
        const int rhsSpan = rhs.endCoordinate - rhs.beginCoordinate;
        if (lhsSpan != rhsSpan) {
            return lhsSpan > rhsSpan;
        }
        if (lhs.horizontal != rhs.horizontal) {
            return lhs.horizontal;
        }
        if (lhs.fixedCoordinate != rhs.fixedCoordinate) {
            return lhs.fixedCoordinate < rhs.fixedCoordinate;
        }
        return lhs.beginCoordinate < rhs.beginCoordinate;
    });
    return candidates;
}

void applyDoglegCandidate(QVector<QVector<CellLevelIoCell>> &cellsByLayer,
                          const DoglegCandidate &candidate,
                          int phaseOffset)
{
    auto &mainLayer = cellsByLayer[0];
    QSet<quint64> removedPositions;
    for (int along = candidate.beginCoordinate;
         along <= candidate.endCoordinate; along += kPitch) {
        if ((along == candidate.beginCoordinate && candidate.anchorBegin) ||
            (along == candidate.endCoordinate && candidate.anchorEnd)) {
            continue;
        }
        const QPoint point = orientedPoint(candidate.horizontal, along,
                                           candidate.fixedCoordinate);
        removedPositions.insert(positionKey(point.x(), point.y()));
    }
    for (int step = 1; step < candidate.shiftSteps; ++step) {
        const int fixed = candidate.fixedCoordinate +
                          candidate.direction * step * kPitch;
        if (!candidate.anchorBegin) {
            const QPoint point = orientedPoint(candidate.horizontal,
                                               candidate.beginCoordinate, fixed);
            removedPositions.insert(positionKey(point.x(), point.y()));
        }
        if (!candidate.anchorEnd) {
            const QPoint point = orientedPoint(candidate.horizontal,
                                               candidate.endCoordinate, fixed);
            removedPositions.insert(positionKey(point.x(), point.y()));
        }
    }

    QVector<CellLevelIoCell> retained;
    retained.reserve(qMax(0, mainLayer.size() - removedPositions.size()));
    for (CellLevelIoCell cell : mainLayer) {
        if (!removedPositions.contains(positionKey(cell.x, cell.y))) {
            retained.push_back(std::move(cell));
        }
    }
    mainLayer = std::move(retained);

    const int targetFixed = candidate.fixedCoordinate +
                            candidate.direction * candidate.shiftSteps * kPitch;
    for (int along = candidate.beginCoordinate + kPitch;
         !candidate.reuseTargetSegment && along < candidate.endCoordinate;
         along += kPitch) {
        const QPoint point = orientedPoint(candidate.horizontal, along, targetFixed);
        CellLevelIoCell cell;
        cell.x = point.x();
        cell.y = point.y();
        cell.layer = 0;
        cell.phase = positiveModulo(
            gridIndexForCoordinate(cell.x) + gridIndexForCoordinate(cell.y) + phaseOffset,
            4);
        cell.type = CellType::NormalCell;
        mainLayer.push_back(std::move(cell));
    }
}

int compactStraightDoglegs(QVector<QVector<CellLevelIoCell>> &cellsByLayer)
{
    const int phaseOffset = inferTwoDdWavePhaseOffset(cellsByLayer);
    const bool debugCompaction = qEnvironmentVariableIntValue(
        "IFCN_DEBUG_IO_COMPACTION") != 0;
    if (debugCompaction) {
        qInfo() << "[IO compaction] inferred 2DDWave phase offset" << phaseOffset;
    }
    if (phaseOffset < 0) {
        return 0;
    }

    constexpr int kMaximumMoves = 64;
    int moves = 0;
    while (moves < kMaximumMoves) {
        const QVector<DoglegCandidate> candidates = findDoglegCandidates(cellsByLayer);
        if (debugCompaction) {
            qInfo() << "[IO compaction] move" << moves
                    << "legal dogleg candidates" << candidates.size();
        }
        if (candidates.isEmpty()) {
            break;
        }
        if (debugCompaction) {
            const DoglegCandidate &selected = candidates.front();
            qInfo() << "[IO compaction] select"
                    << (selected.horizontal ? "horizontal segment / vertical shrink"
                                            : "vertical segment / horizontal shrink")
                    << "shift cells" << selected.shiftSteps
                    << "reuse target" << selected.reuseTargetSegment;
        }
        applyDoglegCandidate(cellsByLayer, candidates.front(), phaseOffset);
        ++moves;
    }
    return moves;
}

struct GridStripCandidate {
    bool horizontalBand = true;
    int startCoordinate = 0;
    int crossingWires = 0;
    int areaSaving = 0;
};

QVector<GridStripCandidate> findGridStripCandidates(
    const QVector<QVector<CellLevelIoCell>> &cellsByLayer)
{
    QVector<GridStripCandidate> candidates;
    if (cellsByLayer.isEmpty() || cellsByLayer[0].isEmpty()) {
        return candidates;
    }

    const auto &mainLayer = cellsByLayer[0];
    QHash<quint64, int> mainIndex;
    mainIndex.reserve(mainLayer.size());
    for (int index = 0; index < mainLayer.size(); ++index) {
        mainIndex.insert(positionKey(mainLayer[index].x, mainLayer[index].y), index);
    }
    const auto cellAt = [&](const QPoint &point) -> const CellLevelIoCell * {
        const auto it = mainIndex.constFind(positionKey(point.x(), point.y()));
        return it == mainIndex.constEnd() ? nullptr : &mainLayer[it.value()];
    };
    const auto isNormalAt = [&](const QPoint &point) {
        const CellLevelIoCell *cell = cellAt(point);
        return cell != nullptr && cell->type == CellType::NormalCell;
    };

    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    for (const auto &layer : cellsByLayer) {
        for (const CellLevelIoCell &cell : layer) {
            minX = qMin(minX, cell.x);
            minY = qMin(minY, cell.y);
            maxX = qMax(maxX, cell.x);
            maxY = qMax(maxY, cell.y);
        }
    }
    const int widthInGrids = gridIndexForCoordinate(maxX) -
                             gridIndexForCoordinate(minX) + 1;
    const int heightInGrids = gridIndexForCoordinate(maxY) -
                              gridIndexForCoordinate(minY) + 1;
    const int stripSteps = kClockTileSize / kPitch;

    for (const bool horizontalBand : {true, false}) {
        const int minimum = horizontalBand ? minY : minX;
        const int maximum = horizontalBand ? maxY : maxX;
        for (int start = minimum + kPitch;
             start + kClockTileSize <= maximum; start += kPitch) {
            const int lastRemoved = start + (stripSteps - 1) * kPitch;
            QSet<int> crossingCoordinates;
            bool haveBefore = false;
            bool haveAfter = false;
            bool legal = true;

            for (int layer = 0; legal && layer < cellsByLayer.size(); ++layer) {
                for (const CellLevelIoCell &cell : cellsByLayer[layer]) {
                    const int fixed = horizontalBand ? cell.y : cell.x;
                    const int along = horizontalBand ? cell.x : cell.y;
                    if (fixed < start) {
                        haveBefore = true;
                    } else if (fixed > lastRemoved) {
                        haveAfter = true;
                    } else {
                        if (layer != 0 || cell.type != CellType::NormalCell) {
                            legal = false;
                            break;
                        }
                        crossingCoordinates.insert(along);
                    }
                }
            }
            if (!legal || !haveBefore || !haveAfter) {
                continue;
            }

            for (const int along : crossingCoordinates) {
                if (!isNormalAt(orientedPoint(horizontalBand, along, start - kPitch)) ||
                    !isNormalAt(orientedPoint(
                        horizontalBand, along, start + kClockTileSize))) {
                    legal = false;
                    break;
                }
                for (int step = 0; step < stripSteps; ++step) {
                    const int fixed = start + step * kPitch;
                    if (!isNormalAt(orientedPoint(horizontalBand, along, fixed)) ||
                        cellAt(orientedPoint(
                            horizontalBand, along - kPitch, fixed)) != nullptr ||
                        cellAt(orientedPoint(
                            horizontalBand, along + kPitch, fixed)) != nullptr) {
                        legal = false;
                        break;
                    }
                }
                if (!legal) {
                    break;
                }
            }
            if (!legal) {
                continue;
            }

            candidates.push_back({horizontalBand, start,
                                  crossingCoordinates.size(),
                                  horizontalBand ? widthInGrids : heightInGrids});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const GridStripCandidate &lhs,
                                                       const GridStripCandidate &rhs) {
        if (lhs.areaSaving != rhs.areaSaving) {
            return lhs.areaSaving > rhs.areaSaving;
        }
        if (lhs.crossingWires != rhs.crossingWires) {
            return lhs.crossingWires < rhs.crossingWires;
        }
        if (lhs.horizontalBand != rhs.horizontalBand) {
            return lhs.horizontalBand;
        }
        return lhs.startCoordinate < rhs.startCoordinate;
    });
    return candidates;
}

void applyGridStripCandidate(QVector<QVector<CellLevelIoCell>> &cellsByLayer,
                             const GridStripCandidate &candidate,
                             int phaseOffset)
{
    const int lastRemoved = candidate.startCoordinate +
                            kClockTileSize - kPitch;
    const int firstShifted = candidate.startCoordinate + kClockTileSize;
    for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
        QVector<CellLevelIoCell> compacted;
        compacted.reserve(cellsByLayer[layer].size());
        for (CellLevelIoCell cell : cellsByLayer[layer]) {
            int &fixed = candidate.horizontalBand ? cell.y : cell.x;
            if (fixed >= candidate.startCoordinate && fixed <= lastRemoved) {
                continue;
            }
            if (fixed >= firstShifted) {
                fixed -= kClockTileSize;
                cell.phase = positiveModulo(
                    gridIndexForCoordinate(cell.x) +
                    gridIndexForCoordinate(cell.y) + phaseOffset,
                    4);
            }
            compacted.push_back(std::move(cell));
        }
        cellsByLayer[layer] = std::move(compacted);
    }
}

QPair<int, int> compactGridStrips(
    QVector<QVector<CellLevelIoCell>> &cellsByLayer,
    int phaseOffset)
{
    constexpr int kMaximumCuts = 64;
    const bool debugCompaction = qEnvironmentVariableIntValue(
        "IFCN_DEBUG_IO_COMPACTION") != 0;
    int removedRows = 0;
    int removedColumns = 0;
    for (int cut = 0; cut < kMaximumCuts; ++cut) {
        const QVector<GridStripCandidate> candidates =
            findGridStripCandidates(cellsByLayer);
        if (candidates.isEmpty()) {
            break;
        }
        const GridStripCandidate &selected = candidates.front();
        if (debugCompaction) {
            qInfo() << "[IO compaction] remove"
                    << (selected.horizontalBand ? "grid row" : "grid column")
                    << "at" << selected.startCoordinate
                    << "crossing wires" << selected.crossingWires;
        }
        applyGridStripCandidate(cellsByLayer, selected, phaseOffset);
        if (selected.horizontalBand) {
            ++removedRows;
        } else {
            ++removedColumns;
        }
    }
    return {removedRows, removedColumns};
}

bool centerOneWireFanout(QVector<QVector<CellLevelIoCell>> &cellsByLayer,
                         int phaseOffset)
{
    if (cellsByLayer.isEmpty() || cellsByLayer.front().isEmpty()) {
        return false;
    }

    auto &mainLayer = cellsByLayer[0];
    QHash<quint64, int> mainIndex;
    QSet<quint64> mainOccupancy;
    QSet<quint64> upperOccupancy;
    mainIndex.reserve(mainLayer.size());
    for (int index = 0; index < mainLayer.size(); ++index) {
        const quint64 key = positionKey(mainLayer[index].x, mainLayer[index].y);
        mainIndex.insert(key, index);
        mainOccupancy.insert(key);
    }
    for (int layer = 1; layer < cellsByLayer.size(); ++layer) {
        for (const CellLevelIoCell &cell : cellsByLayer[layer]) {
            upperOccupancy.insert(positionKey(cell.x, cell.y));
        }
    }

    const auto cellAt = [&](const QPoint &point) -> const CellLevelIoCell * {
        const auto it = mainIndex.constFind(positionKey(point.x(), point.y()));
        return it == mainIndex.constEnd() ? nullptr : &mainLayer[it.value()];
    };
    const auto normalAt = [&](const QPoint &point) {
        const CellLevelIoCell *cell = cellAt(point);
        return cell != nullptr && cell->type == CellType::NormalCell;
    };
    const QPoint directions[] = {
        QPoint(-kPitch, 0), QPoint(kPitch, 0),
        QPoint(0, -kPitch), QPoint(0, kPitch),
    };

    for (const CellLevelIoCell &junctionCell : mainLayer) {
        if (junctionCell.type != CellType::NormalCell) {
            continue;
        }
        const QPoint junction(junctionCell.x, junctionCell.y);
        const bool left = normalAt(junction + directions[0]);
        const bool right = normalAt(junction + directions[1]);
        const bool up = normalAt(junction + directions[2]);
        const bool down = normalAt(junction + directions[3]);
        const bool horizontalTrunk = left && right && (up != down);
        const bool verticalTrunk = up && down && (left != right);
        if (horizontalTrunk == verticalTrunk) {
            continue;
        }

        const int oldAlong = horizontalTrunk ? junction.x() : junction.y();
        const int fixed = horizontalTrunk ? junction.y() : junction.x();
        const int centeredAlong =
            gridIndexForCoordinate(oldAlong) * kClockTileSize + 2 * kPitch;
        // Moving a branch by one site is the unambiguous correction for an
        // off-by-one fanout.  Larger moves are left to placement/routing.
        if (qAbs(centeredAlong - oldAlong) != kPitch) {
            continue;
        }
        const QPoint centeredJunction =
            orientedPoint(horizontalTrunk, centeredAlong, fixed);
        if (!normalAt(centeredJunction)) {
            continue;
        }

        const int branchDirection = horizontalTrunk
            ? (down ? 1 : -1)
            : (right ? 1 : -1);
        const QPoint branchStep = horizontalTrunk
            ? QPoint(0, branchDirection * kPitch)
            : QPoint(branchDirection * kPitch, 0);
        const QPoint sideStep = horizontalTrunk
            ? QPoint(kPitch, 0)
            : QPoint(0, kPitch);

        QSet<quint64> removed;
        QPoint endpoint;
        QPoint cursor = junction + branchStep;
        bool legalStem = false;
        for (int step = 0; step < 64; ++step) {
            if (!normalAt(cursor) ||
                upperOccupancy.contains(positionKey(cursor.x(), cursor.y()))) {
                break;
            }
            const bool sideNegative = cellAt(cursor - sideStep) != nullptr;
            const bool sidePositive = cellAt(cursor + sideStep) != nullptr;
            const bool forward = cellAt(cursor + branchStep) != nullptr;
            const int sideCount = static_cast<int>(sideNegative) +
                                  static_cast<int>(sidePositive);
            if (sideCount > 0) {
                if (sideCount == 1 && !forward) {
                    endpoint = cursor;
                    legalStem = !removed.isEmpty();
                }
                break;
            }
            if (!normalAt(cursor + branchStep)) {
                break;
            }
            removed.insert(positionKey(cursor.x(), cursor.y()));
            cursor += branchStep;
        }
        if (!legalStem) {
            continue;
        }

        QSet<quint64> added;
        QPoint newCursor = centeredJunction + branchStep;
        while ((horizontalTrunk ? newCursor.y() : newCursor.x()) !=
               (horizontalTrunk ? endpoint.y() : endpoint.x()) +
                   branchDirection * kPitch) {
            const quint64 key = positionKey(newCursor.x(), newCursor.y());
            if ((mainOccupancy.contains(key) && !removed.contains(key)) ||
                upperOccupancy.contains(key)) {
                added.clear();
                break;
            }
            added.insert(key);
            newCursor += branchStep;
        }
        if (added.isEmpty()) {
            continue;
        }

        QSet<quint64> proposed = mainOccupancy;
        for (const quint64 key : removed) {
            proposed.remove(key);
        }
        proposed.unite(added);
        const auto degree = [&proposed](const QPoint &point) {
            int result = 0;
            for (const QPoint &direction : {QPoint(-kPitch, 0), QPoint(kPitch, 0),
                                             QPoint(0, -kPitch), QPoint(0, kPitch)}) {
                if (proposed.contains(positionKey(point.x() + direction.x(),
                                                  point.y() + direction.y()))) {
                    ++result;
                }
            }
            return result;
        };
        if (degree(centeredJunction) != 3 || degree(junction) != 2 ||
            degree(endpoint) != 2) {
            continue;
        }
        bool cleanReroute = true;
        for (const quint64 key : added) {
            const QPoint point(static_cast<qint32>(key >> 32),
                               static_cast<qint32>(key & 0xffffffffu));
            if (degree(point) != 2) {
                cleanReroute = false;
                break;
            }
        }
        if (!cleanReroute) {
            continue;
        }

        QVector<CellLevelIoCell> rerouted;
        rerouted.reserve(mainLayer.size() - removed.size() + added.size());
        for (CellLevelIoCell cell : mainLayer) {
            if (!removed.contains(positionKey(cell.x, cell.y))) {
                rerouted.push_back(std::move(cell));
            }
        }
        for (const quint64 key : added) {
            CellLevelIoCell cell;
            cell.x = static_cast<qint32>(key >> 32);
            cell.y = static_cast<qint32>(key & 0xffffffffu);
            cell.layer = 0;
            cell.phase = positiveModulo(
                gridIndexForCoordinate(cell.x) + gridIndexForCoordinate(cell.y) +
                    phaseOffset,
                4);
            cell.type = CellType::NormalCell;
            rerouted.push_back(std::move(cell));
        }
        mainLayer = std::move(rerouted);
        return true;
    }
    return false;
}

int centerWireFanouts(QVector<QVector<CellLevelIoCell>> &cellsByLayer,
                      int phaseOffset)
{
    constexpr int kMaximumMoves = 64;
    int moves = 0;
    while (moves < kMaximumMoves &&
           centerOneWireFanout(cellsByLayer, phaseOffset)) {
        ++moves;
    }
    return moves;
}
} // namespace

static CellLevelIoContractionStats contractCellLevelIoPortsSinglePass(
    QVector<QVector<CellLevelIoCell>> &cellsByLayer,
    const QVector<QPoint> &clockRegionCenters)
{
    CellLevelIoContractionStats stats;
    for (const auto &layer : cellsByLayer) {
        stats.cellsBefore += static_cast<qulonglong>(layer.size());
        for (const CellLevelIoCell &cell : layer) {
            if (cell.type == CellType::CrossoverCell ||
                (cell.layer > 0 && cell.type == CellType::VerticalCell)) {
                ++stats.crossoverCellsBefore;
            }
        }
    }
    if (cellsByLayer.isEmpty() || cellsByLayer.front().isEmpty()) {
        finalizeStatistics(cellsByLayer, stats);
        return stats;
    }

    QSet<quint64> clockCenters;
    clockCenters.reserve(clockRegionCenters.size());
    for (const QPoint &center : clockRegionCenters) {
        clockCenters.insert(positionKey(center.x(), center.y()));
    }
    const auto isLegalPort = [&clockCenters](int x, int y) {
        if (clockCenters.isEmpty()) {
            return isDefaultFiveByFiveEdgePort(x, y);
        }
        return clockCenters.contains(positionKey(x - kClockEdgeOffset, y)) ||
               clockCenters.contains(positionKey(x + kClockEdgeOffset, y)) ||
               clockCenters.contains(positionKey(x, y - kClockEdgeOffset)) ||
               clockCenters.contains(positionKey(x, y + kClockEdgeOffset));
    };

    static const QPoint offsets[] = {
        QPoint(-kPitch, 0), QPoint(kPitch, 0),
        QPoint(0, -kPitch), QPoint(0, kPitch),
    };

    struct CellRef {
        int layer = 0;
        int index = 0;
    };
    const auto refKey = [](const CellRef &ref) {
        return (static_cast<quint64>(static_cast<quint32>(ref.layer)) << 32) |
               static_cast<quint32>(ref.index);
    };

    QVector<QHash<quint64, int>> indicesByLayer(cellsByLayer.size());
    for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
        indicesByLayer[layer].reserve(cellsByLayer[layer].size());
        for (int index = 0; index < cellsByLayer[layer].size(); ++index) {
            const CellLevelIoCell &cell = cellsByLayer[layer][index];
            indicesByLayer[layer].insert(positionKey(cell.x, cell.y), index);
        }
    }

    const auto verticalNeighbor = [&](const CellRef &ref, int nextLayer, CellRef &neighbor) {
        if (nextLayer < 0 || nextLayer >= cellsByLayer.size()) {
            return false;
        }
        const CellLevelIoCell &cell = cellsByLayer[ref.layer][ref.index];
        if (cell.type != CellType::VerticalCell) {
            return false;
        }
        const auto it = indicesByLayer[nextLayer].constFind(positionKey(cell.x, cell.y));
        if (it == indicesByLayer[nextLayer].constEnd() ||
            cellsByLayer[nextLayer][it.value()].type != CellType::VerticalCell) {
            return false;
        }
        neighbor = {nextLayer, it.value()};
        return true;
    };

    const auto neighborsOf = [&](const CellRef &ref) {
        QVector<CellRef> neighbors;
        neighbors.reserve(6);
        const CellLevelIoCell &cell = cellsByLayer[ref.layer][ref.index];
        CellRef below;
        CellRef above;
        const bool hasBelow = verticalNeighbor(ref, ref.layer - 1, below);
        const bool hasAbove = verticalNeighbor(ref, ref.layer + 1, above);

        // A vertical cell in the middle of a stack only connects between
        // layers.  The two end cells also connect to their in-plane wires.
        if (cell.type != CellType::VerticalCell || !(hasBelow && hasAbove)) {
            for (const QPoint &offset : offsets) {
                const auto it = indicesByLayer[ref.layer].constFind(
                    positionKey(cell.x + offset.x(), cell.y + offset.y()));
                if (it != indicesByLayer[ref.layer].constEnd()) {
                    neighbors.push_back({ref.layer, it.value()});
                }
            }
        }
        if (hasBelow) {
            neighbors.push_back(below);
        }
        if (hasAbove) {
            neighbors.push_back(above);
        }
        return neighbors;
    };

    auto &mainLayer = cellsByLayer[0];

    QVector<CellRef> terminals;
    terminals.reserve(mainLayer.size());
    for (int index = 0; index < mainLayer.size(); ++index) {
        if (mainLayer[index].type == CellType::InputCell) {
            terminals.push_back({0, index});
        }
    }
    for (int index = 0; index < mainLayer.size(); ++index) {
        if (mainLayer[index].type == CellType::OutputCell) {
            terminals.push_back({0, index});
        }
    }

    QVector<QSet<int>> cellsToRemove(cellsByLayer.size());
    QSet<quint64> claimedCells;
    struct Replacement {
        CellType type = CellType::NormalCell;
        QString name;
    };
    QHash<quint64, Replacement> replacements;

    for (const CellRef &terminalRef : terminals) {
        const CellLevelIoCell &terminal = mainLayer[terminalRef.index];
        const quint64 terminalKey = refKey(terminalRef);
        if (claimedCells.contains(terminalKey)) {
            ++stats.skippedPorts;
            continue;
        }

        QVector<CellRef> path{terminalRef};
        QSet<quint64> visited{terminalKey};
        CellRef currentRef = terminalRef;
        int targetPathIndex = -1;
        bool targetIsCrossoverEdge = false;

        while (true) {
            QVector<CellRef> forward;
            for (const CellRef &neighbor : neighborsOf(currentRef)) {
                const quint64 key = refKey(neighbor);
                if (!visited.contains(key)) {
                    forward.push_back(neighbor);
                }
            }

            // A private wire is a unique degree-2 chain.  The first branch,
            // another terminal, a fixed cell, or an already claimed prefix is
            // a hard frontier.
            if (forward.size() != 1) {
                break;
            }
            const CellRef nextRef = forward.front();
            const CellLevelIoCell &next = cellsByLayer[nextRef.layer][nextRef.index];
            const quint64 nextKey = refKey(nextRef);
            if (claimedCells.contains(nextKey) || isIo(next.type) || isFixed(next.type)) {
                break;
            }

            path.push_back(nextRef);
            visited.insert(nextKey);
            currentRef = nextRef;
            if (nextRef.layer == 0 && isLegalPort(next.x, next.y)) {
                targetPathIndex = path.size() - 1;
                CellRef upper;
                targetIsCrossoverEdge = next.type == CellType::VerticalCell &&
                    verticalNeighbor(nextRef, 1, upper);
            }
        }

        if (targetPathIndex <= 0) {
            ++stats.skippedPorts;
            continue;
        }

        const CellRef targetRef = path[targetPathIndex];
        const quint64 targetKey = refKey(targetRef);
        bool conflicts = replacements.contains(targetKey);
        for (int pathIndex = 0; pathIndex < targetPathIndex && !conflicts; ++pathIndex) {
            conflicts = claimedCells.contains(refKey(path[pathIndex]));
        }
        if (conflicts) {
            ++stats.skippedPorts;
            continue;
        }

        for (int pathIndex = 0; pathIndex < targetPathIndex; ++pathIndex) {
            const CellRef &ref = path[pathIndex];
            const quint64 key = refKey(ref);
            cellsToRemove[ref.layer].insert(ref.index);
            claimedCells.insert(key);
        }
        replacements.insert(targetKey, Replacement{terminal.type, terminal.name});
        claimedCells.insert(targetKey);
        if (terminal.type == CellType::InputCell) {
            ++stats.movedInputs;
        } else {
            ++stats.movedOutputs;
        }
        if (targetIsCrossoverEdge) {
            ++stats.crossoverEdgePorts;
        }
    }

    if (!replacements.isEmpty()) {
        for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
            QVector<CellLevelIoCell> compacted;
            compacted.reserve(cellsByLayer[layer].size() - cellsToRemove[layer].size());
            for (int index = 0; index < cellsByLayer[layer].size(); ++index) {
                if (cellsToRemove[layer].contains(index)) {
                    continue;
                }
                CellLevelIoCell cell = cellsByLayer[layer][index];
                const auto replacement = replacements.constFind(refKey({layer, index}));
                if (replacement != replacements.constEnd()) {
                    cell.type = replacement->type;
                    cell.name = replacement->name;
                }
                compacted.push_back(std::move(cell));
            }
            cellsByLayer[layer] = std::move(compacted);
        }
    }

    // Removing an IO stem can leave a crossover with no lower wire beneath
    // it.  In that case the still-needed upper route is lowered to layer 0 and
    // its vertical/crossover cells are removed.
    flattenRedundantCrossovers(cellsByLayer);

    qulonglong cellsBeforeRouteCompaction = 0;
    for (const auto &layer : cellsByLayer) {
        cellsBeforeRouteCompaction += static_cast<qulonglong>(layer.size());
    }
    if (!replacements.isEmpty()) {
        stats.compactedDoglegs = compactStraightDoglegs(cellsByLayer);
        const int phaseOffset = inferTwoDdWavePhaseOffset(cellsByLayer);
        stats.twoDdWavePhaseOffset = phaseOffset;
        if (phaseOffset >= 0) {
            const QPair<int, int> compactedStrips =
                compactGridStrips(cellsByLayer, phaseOffset);
            stats.compactedGridRows = compactedStrips.first;
            stats.compactedGridColumns = compactedStrips.second;
            stats.compactedDoglegs += compactStraightDoglegs(cellsByLayer);
        }
    }
    qulonglong cellsAfterRouteCompaction = 0;
    for (const auto &layer : cellsByLayer) {
        cellsAfterRouteCompaction += static_cast<qulonglong>(layer.size());
    }
    stats.removedRouteCells = cellsBeforeRouteCompaction - cellsAfterRouteCompaction;

    stats.removedCells = stats.cellsBefore;
    finalizeStatistics(cellsByLayer, stats);
    stats.removedCells -= stats.cellsAfter;
    stats.removedCrossoverCells = stats.crossoverCellsBefore - stats.crossoverCells;
    return stats;
}

CellLevelIoContractionStats contractCellLevelIoPorts(
    QVector<QVector<CellLevelIoCell>> &cellsByLayer,
    const QVector<QPoint> &clockRegionCenters)
{
    constexpr int kMaximumIoContractionPasses = 64;
    const bool debugCompaction = qEnvironmentVariableIntValue(
        "IFCN_DEBUG_IO_COMPACTION") != 0;
    CellLevelIoContractionStats total;
    bool initialized = false;

    for (int pass = 0; pass < kMaximumIoContractionPasses; ++pass) {
        const CellLevelIoContractionStats current =
            contractCellLevelIoPortsSinglePass(cellsByLayer, clockRegionCenters);
        if (!initialized) {
            total = current;
            initialized = true;
        } else {
            total.cellsAfter = current.cellsAfter;
            total.removedCells = total.cellsBefore - total.cellsAfter;
            total.movedInputs = qMin(current.inputCount,
                                     total.movedInputs + current.movedInputs);
            total.movedOutputs = qMin(current.outputCount,
                                      total.movedOutputs + current.movedOutputs);
            total.crossoverEdgePorts += current.crossoverEdgePorts;
            total.inputCount = current.inputCount;
            total.outputCount = current.outputCount;
            total.nonEmptyLayers = current.nonEmptyLayers;
            total.crossoverCells = current.crossoverCells;
            total.removedCrossoverCells =
                total.crossoverCellsBefore - total.crossoverCells;
            total.compactedDoglegs += current.compactedDoglegs;
            total.compactedGridRows += current.compactedGridRows;
            total.compactedGridColumns += current.compactedGridColumns;
            total.removedRouteCells += current.removedRouteCells;
            total.widthInGrids = current.widthInGrids;
            total.heightInGrids = current.heightInGrids;
            if (current.twoDdWavePhaseOffset >= 0) {
                total.twoDdWavePhaseOffset = current.twoDdWavePhaseOffset;
            }
        }

        if (debugCompaction) {
            qInfo() << "[IO compaction] IO-prefix pass" << pass + 1
                    << "moved" << current.movedPorts()
                    << "remaining cells" << current.cellsAfter;
        }
        if (!current.changed()) {
            break;
        }
    }

    total.skippedPorts = qMax(0, total.inputCount + total.outputCount -
                                 total.movedPorts());
    if (total.twoDdWavePhaseOffset >= 0) {
        const qulonglong cellsBeforeCentering = total.cellsAfter;
        total.centeredFanouts = centerWireFanouts(
            cellsByLayer, total.twoDdWavePhaseOffset);
        if (total.centeredFanouts > 0) {
            finalizeStatistics(cellsByLayer, total);
            total.removedCells = total.cellsBefore - total.cellsAfter;
            total.removedCrossoverCells =
                total.crossoverCellsBefore - total.crossoverCells;
            if (total.cellsAfter > cellsBeforeCentering) {
                const qulonglong addedCells = total.cellsAfter - cellsBeforeCentering;
                total.removedRouteCells = total.removedRouteCells > addedCells
                    ? total.removedRouteCells - addedCells
                    : 0;
            }
        }
    }
    return total;
}
