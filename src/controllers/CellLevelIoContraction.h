#ifndef IFCN_CELLLEVELIOCONTRACTION_H
#define IFCN_CELLLEVELIOCONTRACTION_H

#include <QPoint>
#include <QString>
#include <QVector>

#include "config.h"

struct CellLevelIoCell {
    int x = 0;
    int y = 0;
    int layer = 0;
    int phase = 0;
    CellType type = CellType::NormalCell;
    QString name;
};

struct CellLevelIoContractionStats {
    qulonglong cellsBefore = 0;
    qulonglong cellsAfter = 0;
    qulonglong removedCells = 0;
    int movedInputs = 0;
    int movedOutputs = 0;
    int skippedPorts = 0;
    int crossoverEdgePorts = 0;
    int inputCount = 0;
    int outputCount = 0;
    int nonEmptyLayers = 0;
    int crossoverCellsBefore = 0;
    int crossoverCells = 0;
    int removedCrossoverCells = 0;
    int compactedDoglegs = 0;
    int compactedGridRows = 0;
    int compactedGridColumns = 0;
    int centeredFanouts = 0;
    int twoDdWavePhaseOffset = -1;
    qulonglong removedRouteCells = 0;
    int widthInGrids = 0;
    int heightInGrids = 0;

    int movedPorts() const { return movedInputs + movedOutputs; }
    qulonglong occupiedArea() const
    {
        return static_cast<qulonglong>(widthInGrids) *
               static_cast<qulonglong>(heightInGrids);
    }
    bool changed() const { return movedPorts() > 0 && removedCells > 0; }
};

// Contracts primary IOs along private, unbranched layer-0 wires.  A new IO is
// only placed at the midpoint of a 5x5 clock-region edge.  The layer-0 endpoint
// of a stacked crossover is a valid frontier.  A private crossover consumed by
// the IO stem is removed; if contraction removes the other wire beneath a
// crossover, the still-needed upper route is lowered safely onto layer 0.  For
// a consistent fixed 2DDWave layout, clear U-shaped detours and wire-only T
// branches are then tightened.  Whole 5x5 rows or columns containing only
// empty space or straight-through wires are removed symmetrically; device-
// adjacent branches, upper layers, and obstructed routes are left unchanged.
// IO-prefix discovery is repeated after each successful route compaction so
// newly exposed private prefixes are contracted to a stable result.
CellLevelIoContractionStats contractCellLevelIoPorts(
    QVector<QVector<CellLevelIoCell>> &cellsByLayer,
    const QVector<QPoint> &clockRegionCenters);

#endif // IFCN_CELLLEVELIOCONTRACTION_H
