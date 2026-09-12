#include "ui/mainwindow/MainWindow.h"

#include "controllers/CellLevelIoContraction.h"

#include <QFileInfo>
#include <QSignalBlocker>

#include <cmath>

namespace {
QVector<QPoint> clockCenters(const QVector<QCADScene::ClockRegionRecord> &regions)
{
    QVector<QPoint> centers;
    centers.reserve(regions.size());
    for (const QCADScene::ClockRegionRecord &region : regions) {
        centers.push_back(QPoint(region.x, region.y));
    }
    return centers;
}

int clockGridIndex(int coordinate)
{
    // Keep the quantisation identical to QCADScene::clockCenterForPosition().
    return static_cast<int>(std::floor(
        (static_cast<double>(coordinate) + 10.0) / CLOCK_SCHEME_SIZE_5));
}

int positiveModulo4(int value)
{
    const int remainder = value % 4;
    return remainder < 0 ? remainder + 4 : remainder;
}

QVector<QCADScene::ClockRegionRecord> compactedClockRegions(
    const QVector<QVector<CellLevelIoCell>> &cellsByLayer,
    int phaseOffset)
{
    bool haveCell = false;
    int minGridX = 0;
    int maxGridX = 0;
    int minGridY = 0;
    int maxGridY = 0;
    for (const auto &layer : cellsByLayer) {
        for (const CellLevelIoCell &cell : layer) {
            const int gridX = clockGridIndex(cell.x);
            const int gridY = clockGridIndex(cell.y);
            if (!haveCell) {
                minGridX = maxGridX = gridX;
                minGridY = maxGridY = gridY;
                haveCell = true;
            } else {
                minGridX = qMin(minGridX, gridX);
                maxGridX = qMax(maxGridX, gridX);
                minGridY = qMin(minGridY, gridY);
                maxGridY = qMax(maxGridY, gridY);
            }
        }
    }

    QVector<QCADScene::ClockRegionRecord> regions;
    if (!haveCell || phaseOffset < 0) {
        return regions;
    }
    regions.reserve((maxGridX - minGridX + 1) * (maxGridY - minGridY + 1));
    for (int gridX = minGridX; gridX <= maxGridX; ++gridX) {
        for (int gridY = minGridY; gridY <= maxGridY; ++gridY) {
            QCADScene::ClockRegionRecord region;
            region.x = 40 + gridX * CLOCK_SCHEME_SIZE_5;
            region.y = 40 + gridY * CLOCK_SCHEME_SIZE_5;
            region.phase = positiveModulo4(gridX + gridY + phaseOffset);
            regions.push_back(region);
        }
    }
    return regions;
}
} // namespace

void MainWindow::syncIoContractionControls(bool checked)
{
    if (contractCellLevelIoAction != nullptr) {
        const QSignalBlocker blocker(contractCellLevelIoAction);
        contractCellLevelIoAction->setChecked(checked);
    }
    if (ioContractionCheckBox != nullptr) {
        const QSignalBlocker blocker(ioContractionCheckBox);
        ioContractionCheckBox->setChecked(checked);
    }
}

void MainWindow::clearIoContractionPreviewState()
{
    ioContractionPreviewActive = false;
    ioContractionOriginalWasFast = false;
    ioContractionOriginalDirty = false;
    ioContractionOriginalFilePath.clear();
    ioContractionOriginalFastCells.clear();
    ioContractionOriginalClockRegions.clear();
    ioContractionOriginalSnapshot = DesignSnapshot();
    ioContractionRequiresSaveAs = false;
    ioContractionSourceFilePath.clear();
    syncIoContractionControls(false);
}

bool MainWindow::setCellLevelIoContractionEnabled(bool enabled)
{
    if (enabled) {
        const bool changed = contractCurrentCellLevelIo();
        syncIoContractionControls(changed);
        return changed;
    }

    restoreOriginalLayoutBeforeIoContraction();
    syncIoContractionControls(false);
    return true;
}

bool MainWindow::contractCurrentCellLevelIo()
{
    if (ioContractionPreviewActive) {
        return true;
    }
    if (scene == nullptr || currentSceneCellCount() == 0) {
        printToStatusBar(tr("IO contraction requires a loaded cell-level layout."));
        return false;
    }

    QVector<QVector<CellLevelIoCell>> cellsByLayer;
    const bool fastLayout = scene->hasFastRender();
    DesignSnapshot regularSnapshot;
    QVector<QPoint> regionCenters;

    if (fastLayout) {
        const auto &fastCells = scene->fastCellsByLayer();
        cellsByLayer.resize(fastCells.size());
        for (int layer = 0; layer < fastCells.size(); ++layer) {
            cellsByLayer[layer].reserve(fastCells[layer].size());
            for (const QCADScene::FastCellRecord &cell : fastCells[layer]) {
                cellsByLayer[layer].push_back({cell.x, cell.y, layer, cell.phase,
                                               cell.type, cell.name});
            }
        }
        regionCenters = clockCenters(scene->clockRegions());
    } else {
        regularSnapshot = captureDesignSnapshot();
        cellsByLayer.resize(regularSnapshot.cellsByLayer.size());
        for (int layer = 0; layer < regularSnapshot.cellsByLayer.size(); ++layer) {
            cellsByLayer[layer].reserve(regularSnapshot.cellsByLayer[layer].size());
            for (const SnapshotCell &cell : regularSnapshot.cellsByLayer[layer]) {
                cellsByLayer[layer].push_back({cell.x, cell.y, layer, cell.phase,
                                               cell.type, cell.name});
            }
        }
        regionCenters = clockCenters(regularSnapshot.clockRegions);
    }

    const CellLevelIoContractionStats stats =
        contractCellLevelIoPorts(cellsByLayer, regionCenters);
    if (!stats.changed()) {
        printToStatusBar(tr("IO contraction made no change: no movable IO-to-wire chain reached a legal 5x5 edge port."));
        return false;
    }

    const QVector<QCADScene::ClockRegionRecord> optimizedClockRegions =
        compactedClockRegions(cellsByLayer, stats.twoDdWavePhaseOffset);

    ioContractionOriginalWasFast = fastLayout;
    ioContractionOriginalDirty = isWindowModified();
    ioContractionOriginalFilePath = curFile;
    ioContractionOriginalClockRegions = scene->clockRegions();
    if (fastLayout) {
        ioContractionOriginalFastCells = scene->fastCellsByLayer();
        ioContractionOriginalSnapshot = DesignSnapshot();
    } else {
        ioContractionOriginalFastCells.clear();
        ioContractionOriginalSnapshot = regularSnapshot;
    }
    ioContractionPreviewActive = true;

    scene->clearSelection();
    if (fastLayout) {
        QVector<QVector<QCADScene::FastCellRecord>> fastCells(cellsByLayer.size());
        for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
            fastCells[layer].reserve(cellsByLayer[layer].size());
            for (const CellLevelIoCell &cell : cellsByLayer[layer]) {
                QCADScene::FastCellRecord record;
                record.x = cell.x;
                record.y = cell.y;
                record.layer = layer;
                record.phase = cell.phase;
                record.type = cell.type;
                record.name = cell.name;
                fastCells[layer].push_back(std::move(record));
            }
        }
        scene->replaceFastCells(fastCells);
        if (!optimizedClockRegions.isEmpty()) {
            scene->replaceFastClockRegions(optimizedClockRegions);
        }
    } else {
        regularSnapshot.cellsByLayer.resize(cellsByLayer.size());
        for (int layer = 0; layer < cellsByLayer.size(); ++layer) {
            auto &snapshotLayer = regularSnapshot.cellsByLayer[layer];
            snapshotLayer.clear();
            snapshotLayer.reserve(cellsByLayer[layer].size());
            for (const CellLevelIoCell &cell : cellsByLayer[layer]) {
                SnapshotCell snapshotCell;
                snapshotCell.x = cell.x;
                snapshotCell.y = cell.y;
                snapshotCell.layer = layer;
                snapshotCell.phase = cell.phase;
                snapshotCell.type = cell.type;
                snapshotCell.name = cell.name;
                snapshotLayer.push_back(std::move(snapshotCell));
            }
        }
        if (!optimizedClockRegions.isEmpty()) {
            regularSnapshot.clockRegions = optimizedClockRegions;
        }
        restoreDesignSnapshot(regularSnapshot, false);
    }

    QVector<QString> inputNames;
    for (const auto &layer : cellsByLayer) {
        for (const CellLevelIoCell &cell : layer) {
            if (cell.type == CellType::InputCell && !cell.name.isEmpty()) {
                inputNames.push_back(cell.name);
            }
        }
    }
    setInputNames(inputNames);

    ioContractionRequiresSaveAs = true;
    ioContractionSourceFilePath = curFile;
    resetUndoHistory();
    setDirty(true);
    updateLayoutInfoAfterIoContraction(stats);
    centerViewOnItems(true);
    updateStructure3DView();

    printToStatusBar(
        tr("IO contraction enabled: %1 ports moved, %2 cells removed, %3 obsolete crossover cells removed, %4 wire bends compacted, %5 fanout junctions centered, %6 grid rows and %7 grid columns removed%8. Uncheck to restore; save as a new .qca file.")
            .arg(stats.movedPorts())
            .arg(stats.removedCells)
            .arg(stats.removedCrossoverCells)
            .arg(stats.compactedDoglegs)
            .arg(stats.centeredFanouts)
            .arg(stats.compactedGridRows)
            .arg(stats.compactedGridColumns)
            .arg(stats.crossoverEdgePorts > 0
                     ? tr(", %1 port(s) placed on a layer-0 crossover edge cell")
                           .arg(stats.crossoverEdgePorts)
                     : QString()));
    return true;
}

void MainWindow::restoreOriginalLayoutBeforeIoContraction()
{
    if (!ioContractionPreviewActive || scene == nullptr) {
        return;
    }

    const bool originalWasFast = ioContractionOriginalWasFast;
    const bool originalDirty = ioContractionOriginalDirty;
    const QString originalFilePath = ioContractionOriginalFilePath;
    const auto originalFastCells = ioContractionOriginalFastCells;
    const auto originalClockRegions = ioContractionOriginalClockRegions;
    const DesignSnapshot originalSnapshot = ioContractionOriginalSnapshot;

    scene->clearSelection();
    if (originalWasFast) {
        scene->replaceFastCells(originalFastCells);
        scene->replaceFastClockRegions(originalClockRegions);
    } else {
        restoreDesignSnapshot(originalSnapshot, false);
    }

    QVector<QString> inputNames;
    if (originalWasFast) {
        for (const auto &layer : originalFastCells) {
            for (const QCADScene::FastCellRecord &cell : layer) {
                if (cell.type == CellType::InputCell && !cell.name.isEmpty()) {
                    inputNames.push_back(cell.name);
                }
            }
        }
    } else {
        for (const auto &layer : originalSnapshot.cellsByLayer) {
            for (const SnapshotCell &cell : layer) {
                if (cell.type == CellType::InputCell && !cell.name.isEmpty()) {
                    inputNames.push_back(cell.name);
                }
            }
        }
    }
    setInputNames(inputNames);

    ioContractionPreviewActive = false;
    setCurrentFile(originalFilePath);
    setDirty(originalDirty);
    resetUndoHistory();
    const bool mappedIfcn = QFileInfo(originalFilePath).suffix().compare(
                                QStringLiteral("ifcn"), Qt::CaseInsensitive) == 0 &&
                            gateLevelMapping != nullptr &&
                            !gateLevelMapping->metadata.isEmpty();
    if (mappedIfcn) {
        updateLayoutInfoFromMapping(*gateLevelMapping);
    } else {
        refreshLayoutInfoPanel();
    }
    centerViewOnItems(true);
    updateStructure3DView();
    printToStatusBar(tr("IO contraction disabled; the original cell-level layout was restored."));
}
