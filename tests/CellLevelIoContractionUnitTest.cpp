#include "controllers/CellLevelIoContraction.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const CellLevelIoCell *findCell(const QVector<CellLevelIoCell> &cells, int x, int y)
{
    for (const CellLevelIoCell &cell : cells) {
        if (cell.x == x && cell.y == y) {
            return &cell;
        }
    }
    return nullptr;
}

int phaseAt(int x, int y)
{
    const int gridX = static_cast<int>(std::floor((x + 10.0) / 100.0));
    const int gridY = static_cast<int>(std::floor((y + 10.0) / 100.0));
    return (gridX + gridY) & 0x3;
}

void requireLegalTwoDdWaveCells(const QVector<QVector<CellLevelIoCell>> &layers)
{
    for (const auto &layer : layers) {
        for (const CellLevelIoCell &cell : layer) {
            if (cell.type == CellType::InputCell ||
                cell.type == CellType::OutputCell ||
                cell.type == CellType::FixedCell_0 ||
                cell.type == CellType::FixedCell_1) {
                continue;
            }
            require(cell.phase == phaseAt(cell.x, cell.y),
                    "compaction broke the fixed 2DDWave phase template");
        }
    }
}

void contracts_to_a_five_by_five_edge()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    layers[0] = {
        {240, 240, 0, 0, CellType::InputCell, QStringLiteral("a")},
        {260, 240, 0, 0, CellType::NormalCell, {}},
        {280, 240, 0, 0, CellType::NormalCell, {}},
        {300, 240, 0, 0, CellType::NormalCell, {}},
        {320, 240, 0, 0, CellType::NormalCell, {}},
    };

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(240, 240), QPoint(340, 240)});
    require(stats.movedInputs == 1, "input did not move");
    require(stats.removedCells == 3, "wrong number of cells removed");
    require(stats.widthInGrids == 1 && stats.heightInGrids == 1,
            "occupied width/height are not measured in 5x5 clock grids");
    require(stats.occupiedArea() == 1,
            "occupied area is not width times height");
    const auto *input = findCell(layers[0], 300, 240);
    require(input != nullptr && input->type == CellType::InputCell && input->name == QStringLiteral("a"),
            "new IO is not on the latest legal edge");
}

void measures_area_in_clock_grids()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    layers[0] = {
        {200, 200, 0, 0, CellType::NormalCell, {}},
        {500, 700, 0, 0, CellType::NormalCell, {}},
    };

    const auto stats = contractCellLevelIoPorts(layers, {});
    require(stats.widthInGrids == 4 && stats.heightInGrids == 6,
            "grid span does not match the occupied 5x5 clock-grid bounds");
    require(stats.occupiedArea() == 24,
            "occupied area must equal grid width times grid height");
}

void compacts_a_clear_two_ddwave_wire_dogleg_after_io_contraction()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    for (int x = 200; x <= 500; x += 20) {
        layers[0].push_back({x, 200, 0, phaseAt(x, 200), CellType::NormalCell, {}});
    }
    for (int y = 220; y <= 420; y += 20) {
        layers[0].push_back({200, y, 0, phaseAt(200, y), CellType::NormalCell, {}});
        layers[0].push_back({500, y, 0, phaseAt(500, y), CellType::NormalCell, {}});
    }
    layers[0].push_back({740, 440, 0, 0, CellType::InputCell, QStringLiteral("in")});
    for (int x = 760; x <= 800; x += 20) {
        layers[0].push_back({x, 440, 0, phaseAt(x, 440), CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(740, 440)});
    require(stats.movedInputs == 1, "test IO stem was not contracted");
    require(stats.compactedDoglegs == 2, "clear U-shaped wire was not compacted twice");
    require(stats.removedRouteCells >= 20, "wire compaction removed too few route cells");
    require(findCell(layers[0], 200, 200) == nullptr &&
            findCell(layers[0], 500, 300) == nullptr,
            "obsolete dogleg rows were retained");
    require(stats.compactedGridColumns > 0,
            "whole empty/straight-through grid columns were not removed");
    require(stats.heightInGrids == 1 &&
            stats.occupiedArea() == static_cast<qulonglong>(stats.widthInGrids),
            "post-IO grid area was not recomputed after wire compaction");
    requireLegalTwoDdWaveCells(layers);
}

void removes_a_redundant_rectangular_wire_loop()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    for (int y = 180; y <= 320; y += 20) {
        layers[0].push_back({200, y, 0, phaseAt(200, y), CellType::NormalCell, {}});
    }
    for (int x = 220; x <= 500; x += 20) {
        layers[0].push_back({x, 200, 0, phaseAt(x, 200), CellType::NormalCell, {}});
        layers[0].push_back({x, 300, 0, phaseAt(x, 300), CellType::NormalCell, {}});
    }
    for (int y = 220; y <= 320; y += 20) {
        layers[0].push_back({500, y, 0, phaseAt(500, y), CellType::NormalCell, {}});
    }
    layers[0].push_back({740, 440, 0, 0, CellType::InputCell, QStringLiteral("in")});
    for (int x = 760; x <= 800; x += 20) {
        layers[0].push_back({x, 440, 0, phaseAt(x, 440), CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(740, 440)});
    require(stats.compactedDoglegs == 1,
            "redundant rectangle was not recognized as one compaction move");
    require(stats.removedRouteCells >= 19,
            "redundant upper arc was not removed completely");
    requireLegalTwoDdWaveCells(layers);
}

void slides_a_wire_branch_along_a_clear_trunk()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    for (int y = 180; y <= 420; y += 20) {
        layers[0].push_back({200, y, 0, phaseAt(200, y), CellType::NormalCell, {}});
    }
    for (int x = 220; x <= 500; x += 20) {
        layers[0].push_back({x, 200, 0, phaseAt(x, 200), CellType::NormalCell, {}});
    }
    for (int y = 220; y <= 420; y += 20) {
        layers[0].push_back({500, y, 0, phaseAt(500, y), CellType::NormalCell, {}});
    }
    layers[0].push_back({740, 440, 0, 0, CellType::InputCell, QStringLiteral("in")});
    for (int x = 760; x <= 800; x += 20) {
        layers[0].push_back({x, 440, 0, phaseAt(x, 440), CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(740, 440)});
    require(stats.compactedDoglegs == 2 && stats.removedRouteCells >= 10,
            "wire branch was not slid twice along its clear trunk");
    requireLegalTwoDdWaveCells(layers);
}

void compacts_whole_grid_rows_symmetrically()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    layers[0].push_back({200, 100, 0, 0, CellType::InputCell,
                         QStringLiteral("vertical_in")});
    for (int y = 120; y <= 800; y += 20) {
        layers[0].push_back({200, y, 0, phaseAt(200, y),
                             CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(200, 100)});
    require(stats.movedInputs == 1, "vertical test IO was not contracted");
    require(stats.compactedGridRows > 0,
            "vertical whitespace was not removed as whole grid rows");
    require(stats.compactedGridColumns == 0,
            "vertical-only layout unexpectedly removed grid columns");
    require(stats.heightInGrids < 8,
            "vertical occupied-grid height did not shrink");
    requireLegalTwoDdWaveCells(layers);
}

void compacts_whole_grid_columns_symmetrically()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    layers[0].push_back({100, 200, 0, 0, CellType::InputCell,
                         QStringLiteral("horizontal_in")});
    for (int x = 120; x <= 800; x += 20) {
        layers[0].push_back({x, 200, 0, phaseAt(x, 200),
                             CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(100, 200)});
    require(stats.movedInputs == 1, "horizontal test IO was not contracted");
    require(stats.compactedGridColumns > 0,
            "horizontal whitespace was not removed as whole grid columns");
    require(stats.compactedGridRows == 0,
            "horizontal-only layout unexpectedly removed grid rows");
    require(stats.widthInGrids < 8,
            "horizontal occupied-grid width did not shrink");
    requireLegalTwoDdWaveCells(layers);
}

void centers_a_one_site_offset_wire_fanout()
{
    QVector<QVector<CellLevelIoCell>> layers(1);
    for (int x = 500; x <= 800; x += 20) {
        layers[0].push_back({x, 740, 0, phaseAt(x, 740),
                             CellType::NormalCell, {}});
    }
    for (int y = 760; y <= 840; y += 20) {
        layers[0].push_back({560, y, 0, phaseAt(560, y),
                             CellType::NormalCell, {}});
    }
    for (int x = 580; x <= 680; x += 20) {
        layers[0].push_back({x, 840, 0, phaseAt(x, 840),
                             CellType::NormalCell, {}});
    }
    layers[0].push_back({900, 100, 0, 0, CellType::InputCell,
                         QStringLiteral("trigger")});
    for (int x = 920; x <= 980; x += 20) {
        layers[0].push_back({x, 100, 0, phaseAt(x, 100),
                             CellType::NormalCell, {}});
    }

    const auto stats = contractCellLevelIoPorts(layers, {});
    require(stats.centeredFanouts > 0,
            "one-site-offset wire fanout was not centered");

    int tJunctions = 0;
    for (const CellLevelIoCell &cell : layers[0]) {
        if (cell.type != CellType::NormalCell) {
            continue;
        }
        int horizontalNeighbors = 0;
        int verticalNeighbors = 0;
        horizontalNeighbors += findCell(layers[0], cell.x - 20, cell.y) != nullptr;
        horizontalNeighbors += findCell(layers[0], cell.x + 20, cell.y) != nullptr;
        verticalNeighbors += findCell(layers[0], cell.x, cell.y - 20) != nullptr;
        verticalNeighbors += findCell(layers[0], cell.x, cell.y + 20) != nullptr;
        if ((horizontalNeighbors == 2 && verticalNeighbors == 1) ||
            (verticalNeighbors == 2 && horizontalNeighbors == 1)) {
            ++tJunctions;
            const int along = horizontalNeighbors == 2 ? cell.x : cell.y;
            require(((along % 100) + 100) % 100 == 40,
                    "fanout junction is not on the middle site of its 5x5 tile");
        }
    }
    require(tJunctions == 1, "centered test route no longer has one T junction");
    requireLegalTwoDdWaveCells(layers);
}

void uses_the_layer_zero_crossover_edge()
{
    QVector<QVector<CellLevelIoCell>> layers(3);
    layers[0] = {
        {240, 240, 0, 0, CellType::InputCell, QStringLiteral("cross_in")},
        {260, 240, 0, 0, CellType::NormalCell, {}},
        {280, 240, 0, 0, CellType::VerticalCell, {}},
    };
    layers[1] = {{280, 240, 1, 0, CellType::VerticalCell, {}}};
    layers[2] = {
        {280, 240, 2, 0, CellType::VerticalCell, {}},
        {300, 240, 2, 0, CellType::CrossoverCell, {}},
        {320, 240, 2, 0, CellType::CrossoverCell, {}},
    };

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(240, 240)});
    require(stats.crossoverEdgePorts == 1, "crossover edge was not accepted");
    const auto *input = findCell(layers[0], 280, 240);
    require(input != nullptr && input->type == CellType::InputCell,
            "layer-0 crossover endpoint was not retyped as IO");
    require(layers[1].size() == 1 && layers[2].size() == 3,
            "upper crossover layers were changed");
}

void removes_a_crossover_consumed_by_the_contracted_io_stem()
{
    QVector<QVector<CellLevelIoCell>> layers(3);
    layers[0] = {
        {240, 240, 0, 0, CellType::InputCell, QStringLiteral("cross_in")},
        {260, 240, 0, 0, CellType::NormalCell, {}},
        {280, 240, 0, 0, CellType::VerticalCell, {}},
        {380, 240, 0, 0, CellType::VerticalCell, {}},
        {400, 240, 0, 0, CellType::NormalCell, {}},
        {420, 240, 0, 0, CellType::NormalCell, {}},
        // Independent lower-layer wire crossing beneath the upper segment.
        {320, 200, 0, 0, CellType::NormalCell, {}},
        {320, 220, 0, 0, CellType::NormalCell, {}},
        {320, 240, 0, 0, CellType::NormalCell, {}},
        {320, 260, 0, 0, CellType::NormalCell, {}},
        {320, 280, 0, 0, CellType::NormalCell, {}},
    };
    layers[1] = {
        {280, 240, 1, 0, CellType::VerticalCell, {}},
        {380, 240, 1, 0, CellType::VerticalCell, {}},
    };
    layers[2] = {
        {280, 240, 2, 0, CellType::VerticalCell, {}},
        {300, 240, 2, 0, CellType::CrossoverCell, {}},
        {320, 240, 2, 0, CellType::CrossoverCell, {}},
        {340, 240, 2, 0, CellType::CrossoverCell, {}},
        {360, 240, 2, 0, CellType::CrossoverCell, {}},
        {380, 240, 2, 0, CellType::VerticalCell, {}},
    };

    const auto stats = contractCellLevelIoPorts(
        layers, {QPoint(240, 240), QPoint(440, 240)});
    const auto *input = findCell(layers[0], 400, 240);
    require(input != nullptr && input->type == CellType::InputCell,
            "IO did not traverse the private crossover stem");
    require(layers[1].isEmpty() && layers[2].isEmpty(),
            "obsolete upper crossover cells were retained");
    require(stats.removedCrossoverCells == 8,
            "removed crossover-cell statistics are incorrect");
    require(findCell(layers[0], 320, 240) != nullptr,
            "the independent lower crossing wire was removed");
}

void lowers_the_other_wire_when_its_crossing_is_no_longer_needed()
{
    QVector<QVector<CellLevelIoCell>> layers(3);
    layers[0] = {
        // The vertical IO stem is removed through the crossing point.
        {320, 200, 0, 0, CellType::InputCell, QStringLiteral("lower_in")},
        {320, 220, 0, 0, CellType::NormalCell, {}},
        {320, 240, 0, 0, CellType::NormalCell, {}},
        {320, 260, 0, 0, CellType::NormalCell, {}},
        {320, 280, 0, 0, CellType::NormalCell, {}},
        // Endpoints of the independent upper horizontal wire.
        {280, 240, 0, 0, CellType::VerticalCell, {}},
        {380, 240, 0, 0, CellType::VerticalCell, {}},
    };
    layers[1] = {
        {280, 240, 1, 0, CellType::VerticalCell, {}},
        {380, 240, 1, 0, CellType::VerticalCell, {}},
    };
    layers[2] = {
        {280, 240, 2, 0, CellType::VerticalCell, {}},
        {300, 240, 2, 0, CellType::CrossoverCell, {}},
        {320, 240, 2, 0, CellType::CrossoverCell, {}},
        {340, 240, 2, 0, CellType::CrossoverCell, {}},
        {360, 240, 2, 0, CellType::CrossoverCell, {}},
        {380, 240, 2, 0, CellType::VerticalCell, {}},
    };

    const auto stats = contractCellLevelIoPorts(layers, {QPoint(320, 240)});
    const auto *input = findCell(layers[0], 320, 280);
    require(input != nullptr && input->type == CellType::InputCell,
            "lower IO stem was not contracted past the crossing");
    require(layers[1].isEmpty() && layers[2].isEmpty(),
            "redundant upper crossover layers were retained");
    for (int x = 280; x <= 380; x += 20) {
        const auto *wire = findCell(layers[0], x, 240);
        require(wire != nullptr && wire->type == CellType::NormalCell,
                "the preserved upper wire was not lowered to layer 0");
    }
    require(stats.removedCrossoverCells == 8,
            "flattened crossover statistics are incorrect");
}
} // namespace

int main()
{
    try {
        contracts_to_a_five_by_five_edge();
        measures_area_in_clock_grids();
        compacts_a_clear_two_ddwave_wire_dogleg_after_io_contraction();
        removes_a_redundant_rectangular_wire_loop();
        slides_a_wire_branch_along_a_clear_trunk();
        compacts_whole_grid_rows_symmetrically();
        compacts_whole_grid_columns_symmetrically();
        centers_a_one_site_offset_wire_fanout();
        uses_the_layer_zero_crossover_edge();
        removes_a_crossover_consumed_by_the_contracted_io_stem();
        lowers_the_other_wire_when_its_crossing_is_no_longer_needed();
        std::cout << "Cell-level IO contraction tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Cell-level IO contraction tests failed: " << error.what() << '\n';
        return 1;
    }
}
