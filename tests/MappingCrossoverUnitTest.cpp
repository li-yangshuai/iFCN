#include <autopr/algorithms/mapping.h>
#include <autopr/io/ifcnMappingMetadata.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {

using fcngraph::Mapping;
using fcngraph::MappingMode;
using fcngraph::MappingPositionHash;
using fcngraph::NodeLinkMap;
using fcngraph::PhysicalCellSite;
using fcngraph::RouteCellMap;
using fcngraph::IfcnMappingModeResolver;
using fcngraph::position;

bool isBoundedStraightCrossover(const std::vector<position>& cells)
{
    if (cells.size() != 5) {
        return false;
    }

    const bool horizontal = std::all_of(cells.begin(), cells.end(), [&](const position& cell) {
        return cell.second == cells.front().second;
    });
    const bool vertical = std::all_of(cells.begin(), cells.end(), [&](const position& cell) {
        return cell.first == cells.front().first;
    });
    if (horizontal == vertical) {
        return false;
    }

    const unsigned int tileX = cells.front().first / 5;
    const unsigned int tileY = cells.front().second / 5;
    for (std::size_t index = 0; index < cells.size(); ++index) {
        if (cells[index].first / 5 != tileX || cells[index].second / 5 != tileY) {
            return false;
        }
        if (index == 0) {
            continue;
        }
        const auto dx = cells[index].first > cells[index - 1].first
                            ? cells[index].first - cells[index - 1].first
                            : cells[index - 1].first - cells[index].first;
        const auto dy = cells[index].second > cells[index - 1].second
                            ? cells[index].second - cells[index - 1].second
                            : cells[index - 1].second - cells[index].second;
        if (dx + dy != 1) {
            return false;
        }
    }
    return true;
}

bool coversDirectedIntermediateTiles(const std::vector<position>& route,
                                     const RouteCellMap& routeCells,
                                     const RouteCellMap& crossCells)
{
    if (route.size() <= 2) {
        return true;
    }
    const auto key = std::make_pair(route.front(), route.back());
    std::unordered_set<position, MappingPositionHash> coveredTiles;
    const auto include = [&](const RouteCellMap& mappings) {
        const auto it = mappings.find(key);
        if (it == mappings.end()) {
            return;
        }
        for (const auto& segment : it->second) {
            for (const position& cell : segment) {
                coveredTiles.insert({cell.first / 5, cell.second / 5});
            }
        }
    };
    include(routeCells);
    include(crossCells);
    for (std::size_t index = 1; index + 1 < route.size(); ++index) {
        if (coveredTiles.find(route[index]) == coveredTiles.end()) {
            return false;
        }
    }
    return true;
}

bool connectsNodeCenters(const Mapping& mapping,
                         const RouteCellMap& routeCells,
                         const std::vector<position>& route)
{
    if (route.size() < 2) {
        return false;
    }

    const position sourceCenter{route.front().first * 5 + 2,
                                route.front().second * 5 + 2};
    const position sinkCenter{route.back().first * 5 + 2,
                              route.back().second * 5 + 2};
    std::unordered_set<position, MappingPositionHash> cells;

    // Only admit the two endpoint templates and this route's cells.  That
    // prevents an unrelated nearby net from making a broken route look whole.
    for (const auto& bucket : mapping.nodecell_list) {
        for (const position& cell : bucket.second) {
            const position tile{cell.first / 5, cell.second / 5};
            if (tile == route.front() || tile == route.back()) {
                cells.insert(cell);
            }
        }
    }

    const auto routeKey = std::make_pair(route.front(), route.back());
    const auto includeRouteSegments = [&](const RouteCellMap& mappings) {
        const auto mappingIt = mappings.find(routeKey);
        if (mappingIt == mappings.end()) {
            return;
        }
        for (const auto& segment : mappingIt->second) {
            cells.insert(segment.begin(), segment.end());
        }
    };
    includeRouteSegments(routeCells);
    includeRouteSegments(mapping.crossline_list);

    if (cells.find(sourceCenter) == cells.end() ||
        cells.find(sinkCenter) == cells.end()) {
        return false;
    }

    std::unordered_set<position, MappingPositionHash> visited{sourceCenter};
    std::vector<position> pending{sourceCenter};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const position current = pending[index];
        if (current == sinkCenter) {
            return true;
        }
        const position neighbors[]{
            {current.first + 1, current.second},
            {current.first, current.second + 1},
            {current.first == 0 ? 0 : current.first - 1, current.second},
            {current.first, current.second == 0 ? 0 : current.second - 1},
        };
        for (const position& neighbor : neighbors) {
            if (cells.find(neighbor) != cells.end() &&
                visited.insert(neighbor).second) {
                pending.push_back(neighbor);
            }
        }
    }
    return false;
}

unsigned int cellManhattanDistance(const position& left,
                                   const position& right)
{
    const unsigned int dx = left.first > right.first
                                ? left.first - right.first
                                : right.first - left.first;
    const unsigned int dy = left.second > right.second
                                ? left.second - right.second
                                : right.second - left.second;
    return dx + dy;
}

bool isFourNeighborPath(const std::vector<position>& path)
{
    for (std::size_t index = 1; index < path.size(); ++index) {
        if (cellManhattanDistance(path[index - 1], path[index]) != 1) {
            return false;
        }
    }
    return true;
}

bool isLayerAwareNeighborPath(const std::vector<PhysicalCellSite>& path)
{
    for (std::size_t index = 1; index < path.size(); ++index) {
        const auto& before = path[index - 1];
        const auto& after = path[index];
        if (before.layer == after.layer) {
            if (before.layer == 1 ||
                cellManhattanDistance(before.xy, after.xy) != 1) {
                return false;
            }
            continue;
        }
        if (before.xy != after.xy ||
            std::abs(before.layer - after.layer) != 1) {
            return false;
        }
    }
    return true;
}

std::vector<position> visitedCoarseTiles(
    const std::vector<position>& physicalPath)
{
    std::vector<position> tiles;
    for (const position& cell : physicalPath) {
        const position tile{cell.first / 5, cell.second / 5};
        if (tiles.empty() || tiles.back() != tile) {
            tiles.push_back(tile);
        }
    }
    return tiles;
}

std::size_t commonPhysicalPrefixLength(const std::vector<position>& left,
                                       const std::vector<position>& right)
{
    const std::size_t limit = std::min(left.size(), right.size());
    std::size_t length = 0;
    while (length < limit && left[length] == right[length]) {
        ++length;
    }
    return length;
}

} // namespace

int main()
{
    // Seed-1 legal 2DDWave routing for TOY/par_gen after rejecting parallel
    // inter-net overlap. Same-source fanout trunks remain shared; the five
    // remaining inter-net intersections are orthogonal and local.
    std::vector<std::vector<position>> routes{
        {{0, 0}, {0, 1}, {1, 1}, {1, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6},
         {1, 7}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {7, 8}},
        {{0, 0}, {0, 1}, {0, 2}},
        {{2, 0}, {2, 1}, {2, 2}},
        {{2, 0}, {2, 1}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {4, 4}},
        {{4, 0}, {4, 1}, {5, 1}, {5, 2}, {5, 3}, {5, 4}, {6, 4}},
        {{4, 0}, {4, 1}, {4, 2}},
        {{2, 2}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}, {6, 4}},
        {{6, 4}, {6, 5}},
        {{4, 2}, {4, 3}, {4, 4}},
        {{4, 4}, {4, 5}, {5, 5}, {6, 5}},
        {{6, 5}, {6, 6}, {7, 6}, {7, 7}},
        {{6, 5}, {6, 6}, {6, 7}},
        {{7, 7}, {7, 8}},
        {{7, 8}, {7, 9}},
        {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7},
         {3, 7}, {4, 7}, {5, 7}, {6, 7}},
        {{6, 7}, {6, 8}, {6, 9}, {7, 9}},
    };

    Mapping mapping;
    mapping.mapping_line(routes);

    std::string validationError;
    if (!mapping.validate_crossovers(&validationError)) {
        std::cerr << "legal par_gen crossovers were rejected: "
                  << validationError << '\n';
        return 1;
    }

    std::size_t crossoverCount = 0;
    for (const auto& routeEntry : mapping.crossline_list) {
        for (const auto& segment : routeEntry.second) {
            ++crossoverCount;
            if (!isBoundedStraightCrossover(segment)) {
                std::cerr << "invalid crossover segment for route ("
                          << routeEntry.first.first.first << ','
                          << routeEntry.first.first.second << ")->("
                          << routeEntry.first.second.first << ','
                          << routeEntry.first.second.second << "), cells="
                          << segment.size() << '\n';
                return 2;
            }
        }
    }

    if (crossoverCount != 5) {
        std::cerr << "expected five local par_gen crossovers, got "
                  << crossoverCount << '\n';
        return 3;
    }

    // Reduced xor5R regression: two fanout branches share (1,4)->(1,5),
    // while the second branch crosses an unrelated vertical route at (3,6).
    // Fanout centerline stitching used to lift the whole multi-tile corridor
    // instead of only the local 5x5 crossover segment.
    std::vector<std::vector<position>> stitchedFanoutRoutes{
        {{1, 4}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 5}, {7, 6}},
        {{1, 4}, {1, 5}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}},
        {{3, 4}, {3, 5}, {3, 6}, {3, 7}, {3, 8}, {3, 9}, {4, 9}, {5, 9}},
    };
    Mapping stitchedFanout;
    stitchedFanout.mapping_line(stitchedFanoutRoutes);
    if (stitchedFanout.crossline_list.empty() ||
        !stitchedFanout.validate_crossovers(&validationError)) {
        std::cerr << "stitched fanout produced a non-local crossover: "
                  << validationError << '\n';
        return 4;
    }

    // Reduced b1_r2 regression for route-order selection: a shorter fanout
    // candidate is not admissible when its crossover mapping is illegal.
    std::vector<std::vector<position>> orderedCandidateRoutes{
        {{3, 2}, {3, 3}, {4, 3}, {5, 3}, {5, 4}},
        {{3, 2}, {3, 3}, {3, 4}, {3, 5}},
        {{2, 2}, {2, 3}, {3, 3}, {3, 4}, {4, 4}},
    };
    Mapping orderedCandidate;
    orderedCandidate.mapping_line(orderedCandidateRoutes);
    if (orderedCandidate.crossline_list.empty() ||
        !orderedCandidate.validate_crossovers(&validationError)) {
        std::cerr << "route-order candidate selected an illegal crossover: "
                  << validationError << '\n';
        return 5;
    }

    Mapping legacyLocalTurn;
    legacyLocalTurn.crossline_list[{{1, 0}, {3, 4}}].push_back(
        {{7, 10}, {8, 10}, {9, 10}, {9, 11}, {9, 12}, {9, 13}, {9, 14}});
    if (!legacyLocalTurn.validate_crossovers(&validationError)) {
        std::cerr << "local legacy crossover turn was rejected: "
                  << validationError << '\n';
        return 6;
    }

    Mapping invalidLiftedCorridor;
    invalidLiftedCorridor.crossline_list[{{1, 0}, {3, 4}}].push_back(
        {{7, 5}, {7, 6}, {7, 7}, {7, 8}, {7, 9}, {7, 10},
         {8, 10}, {9, 10}, {9, 11}, {9, 12}, {9, 13}, {9, 14}});
    if (invalidLiftedCorridor.validate_crossovers(&validationError)) {
        std::cerr << "cross-tile lifted corridor was not rejected\n";
        return 7;
    }

    // Sequential paths may legitimately reverse an axis to pack deliberate
    // clock/epoch delay into a compact box.  The cell mapper must retain every
    // ordered intermediate coarse tile instead of replacing the detour with a
    // direct source-to-sink corridor.
    std::vector<std::vector<position>> sequentialDetours{
        {{1, 1}, {1, 2}, {2, 2}, {3, 2}, {3, 1}, {4, 1}},
        {{1, 1}, {1, 2}, {2, 2}, {2, 3}, {2, 4}},
    };
    Mapping sequentialMapping;
    NodeLinkMap sequentialDetourNodes;
    sequentialDetourNodes[{{1, 1}, "input"}] = {{}, {{1, 2}}};
    sequentialDetourNodes[{{4, 1}, "output"}] = {{{3, 1}}, {}};
    sequentialDetourNodes[{{2, 4}, "output"}] = {{{2, 3}}, {}};
    sequentialMapping.node_mapping(sequentialDetourNodes,
                                   MappingMode::Sequential);
    const std::vector<unsigned int> sequentialDistances{1, 0};
    const RouteCellMap sequentialCells = sequentialMapping.mapping_line(
        sequentialDetours, MappingMode::Sequential, sequentialDistances);
    if (!sequentialMapping.validate_crossovers(&validationError)) {
        std::cerr << "legal sequential detour mapping was rejected: "
                  << validationError << '\n';
        return 8;
    }
    for (const auto& route : sequentialDetours) {
        if (!coversDirectedIntermediateTiles(route,
                                             sequentialCells,
                                             sequentialMapping.crossline_list)) {
            std::cerr << "sequential mapping dropped an ordered detour tile\n";
            return 9;
        }
    }

    const auto orderedDetourPaths =
        sequentialMapping.orderedPhysicalRoutes(sequentialDetours);
    if (orderedDetourPaths.size() != sequentialDetours.size()) {
        std::cerr << "ordered physical detour path count does not match coarse routes\n";
        return 26;
    }
    if (!isFourNeighborPath(orderedDetourPaths.front()) ||
        visitedCoarseTiles(orderedDetourPaths.front()) !=
            sequentialDetours.front()) {
        std::cerr << "ordered physical path dropped or reordered a non-monotonic detour tile\n";
        return 27;
    }

    const std::vector<position> expectedFanoutPrefix{
        {7, 9}, {7, 10}, {7, 11}, {7, 12}, {8, 12},
        {9, 12}, {10, 12}, {11, 12}, {12, 12},
    };
    const std::size_t sharedFanoutCells = commonPhysicalPrefixLength(
        orderedDetourPaths[0], orderedDetourPaths[1]);
    if (sharedFanoutCells != expectedFanoutPrefix.size() ||
        !std::equal(expectedFanoutPrefix.begin(),
                    expectedFanoutPrefix.end(),
                    orderedDetourPaths[0].begin()) ||
        !std::equal(expectedFanoutPrefix.begin(),
                    expectedFanoutPrefix.end(),
                    orderedDetourPaths[1].begin())) {
        std::cerr << "ordered fanout routes do not preserve an identical physical-cell prefix\n";
        return 28;
    }

    // johnson2 regression: the cut state terminal d0 is represented as an
    // IFCN output, but physically it is both the sink of the state-update net
    // and the source of the feedback net.  The legacy output template mapped
    // only its north input arm, leaving the feedback route below d0 detached.
    NodeLinkMap johnson2Nodes;
    johnson2Nodes[{{6, 7}, "wire"}] = {{}, {{6, 8}}};
    johnson2Nodes[{{6, 8}, "output"}] = {{{6, 7}}, {{6, 9}}};
    johnson2Nodes[{{4, 8}, "and"}] = {{{4, 9}}, {}};
    std::vector<std::vector<position>> johnson2Routes{
        {{6, 7}, {6, 8}},
        {{6, 8}, {6, 9}, {5, 9}, {4, 9}, {4, 8}},
    };
    const std::vector<unsigned int> johnson2Distances{0, 1};

    Mapping johnson2Mapping;
    johnson2Mapping.node_mapping(johnson2Nodes, MappingMode::Sequential);
    const position d0SouthInner{32, 43};
    const position d0SouthBoundary{32, 44};
    const auto& johnson2NormalCells = johnson2Mapping.nodecell_list["normal"];
    if (std::find(johnson2NormalCells.begin(), johnson2NormalCells.end(),
                  d0SouthInner) == johnson2NormalCells.end() ||
        std::find(johnson2NormalCells.begin(), johnson2NormalCells.end(),
                  d0SouthBoundary) == johnson2NormalCells.end()) {
        std::cerr << "sequential state output d0 is missing its south feedback port\n";
        return 23;
    }

    const RouteCellMap johnson2Cells = johnson2Mapping.mapping_line(
        johnson2Routes, MappingMode::Sequential, johnson2Distances);
    if (!johnson2Mapping.validate_crossovers(&validationError)) {
        std::cerr << "johnson2 feedback mapping failed crossover DRC: "
                  << validationError << '\n';
        return 24;
    }
    if (!connectsNodeCenters(johnson2Mapping,
                             johnson2Cells,
                             johnson2Routes.back())) {
        std::cerr << "johnson2 d0 feedback route is not physically connected end to end\n";
        return 25;
    }

    const auto orderedJohnson2Paths =
        johnson2Mapping.orderedPhysicalRoutes(johnson2Routes);
    if (orderedJohnson2Paths.size() != johnson2Routes.size()) {
        std::cerr << "johnson2 ordered physical path count does not match coarse routes\n";
        return 29;
    }
    const std::vector<position>& orderedFeedbackPath =
        orderedJohnson2Paths.back();
    const position expectedFeedbackSourcePort{32, 44};
    const position expectedFeedbackSinkPort{22, 44};
    if (orderedFeedbackPath.empty() ||
        orderedFeedbackPath.front() != expectedFeedbackSourcePort ||
        orderedFeedbackPath.back() != expectedFeedbackSinkPort) {
        std::cerr << "johnson2 ordered feedback path uses the wrong endpoint port\n";
        return 30;
    }
    if (orderedFeedbackPath.size() != 17) {
        std::cerr << "johnson2 ordered feedback path expected 17 cells, got "
                  << orderedFeedbackPath.size() << '\n';
        return 31;
    }
    if (!isFourNeighborPath(orderedFeedbackPath)) {
        std::cerr << "johnson2 ordered feedback path contains a non-4-neighbor step\n";
        return 32;
    }

    // Layer-aware crossover regression.  The lifted cells are deliberately
    // split across two mapping segments, while a different route has a cell
    // adjacent to the entry.  Pillars must follow the current route's maximal
    // owned crossover run; global neighboring crossover cells must not make
    // an entry/exit pillar disappear.
    Mapping layeredCrossing;
    const std::vector<std::vector<position>> layeredCoarseRoute{
        {{0, 0}, {1, 0}, {2, 0}},
    };
    const auto layeredKey =
        std::make_pair(position{0, 0}, position{2, 0});
    layeredCrossing.deviatemapping_list[layeredKey] = {
        {{5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2}},
    };
    layeredCrossing.crossline_list[layeredKey] = {
        {{5, 2}, {6, 2}, {7, 2}},
        {{8, 2}, {9, 2}},
    };
    layeredCrossing.crossline_list[{{1, 2}, {1, 0}}] = {
        {{5, 0}, {5, 1}},
    };
    const auto layeredPaths =
        layeredCrossing.orderedLayerAwarePhysicalRoutes(layeredCoarseRoute);
    const std::vector<PhysicalCellSite> expectedLayeredPath{
        {4, 2, 0},
        {5, 2, 0}, {5, 2, 1}, {5, 2, 2},
        {6, 2, 2}, {7, 2, 2}, {8, 2, 2},
        {9, 2, 2}, {9, 2, 1}, {9, 2, 0},
        {10, 2, 0},
    };
    if (layeredPaths.size() != 1 ||
        layeredPaths.front() != expectedLayeredPath ||
        !isLayerAwareNeighborPath(layeredPaths.front())) {
        std::cerr << "layer-aware crossover path omitted or misplaced a vertical pillar\n";
        return 33;
    }
    const auto layeredSites =
        layeredCrossing.physicalCellSites(layeredCoarseRoute);
    if (layeredSites.size() != expectedLayeredPath.size() ||
        !std::all_of(expectedLayeredPath.begin(), expectedLayeredPath.end(),
                     [&](const PhysicalCellSite& site) {
                         return layeredSites.count(site) == 1;
                     })) {
        std::cerr << "layer-aware physical site set disagrees with the ordered path\n";
        return 34;
    }

    // Two routes with different sources may cross on different layers, but
    // they must never claim the same exact QCA site.  This deliberately makes
    // their ordinary L0 paths overlap inside one coarse tile and verifies the
    // layer-aware materializer fails closed.
    Mapping exactSiteOverlap;
    const std::vector<std::vector<position>> overlappingCoarseRoutes{
        {{0, 0}, {1, 0}, {2, 0}},
        {{1, 1}, {1, 0}, {2, 0}},
    };
    exactSiteOverlap.deviatemapping_list[
        {{0, 0}, {2, 0}}] = {{{5, 2}, {6, 2}, {7, 2}, {8, 2}, {9, 2}}};
    exactSiteOverlap.deviatemapping_list[
        {{1, 1}, {2, 0}}] = {{{7, 4}, {7, 3}, {7, 2}, {8, 2}, {9, 2}}};
    try {
        static_cast<void>(
            exactSiteOverlap.physicalCellSites(overlappingCoarseRoutes));
        std::cerr << "layer-aware mapping accepted an exact L0 site shared by different sources\n";
        return 35;
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("different source routes") ==
            std::string::npos) {
            std::cerr << "exact-site overlap failed for an unexpected reason: "
                      << error.what() << '\n';
            return 36;
        }
    }

    std::vector<std::vector<position>> repeatedSequentialTile{
        {{1, 1}, {1, 2}, {2, 2}, {1, 2}, {1, 3}},
    };
    try {
        sequentialMapping.mapping_line(repeatedSequentialTile,
                                       MappingMode::Sequential);
        std::cerr << "sequential mapping accepted a repeated coarse tile\n";
        return 10;
    } catch (const std::runtime_error&) {
    }
    if (!sequentialMapping.deviatemapping_list.empty() ||
        !sequentialMapping.crossline_list.empty()) {
        std::cerr << "failed sequential remap retained stale route state\n";
        return 17;
    }

    try {
        Mapping mismatchedMetadata;
        const std::vector<unsigned int> oneDistance{1};
        mismatchedMetadata.mapping_line(sequentialDetours,
                                        MappingMode::Sequential,
                                        oneDistance);
        std::cerr << "sequential mapping accepted misaligned route metadata\n";
        return 21;
    } catch (const std::runtime_error&) {
    }

    try {
        Mapping combinationalFeedback;
        combinationalFeedback.mapping_line(sequentialDetours,
                                           MappingMode::Combinational,
                                           sequentialDistances);
        std::cerr << "combinational mapping accepted recurrence metadata\n";
        return 22;
    } catch (const std::runtime_error&) {
    }

    std::vector<std::vector<position>> rejoiningSequentialFanout{
        {{1, 1}, {1, 2}, {2, 2}, {2, 3}, {3, 3}, {4, 3}},
        {{1, 1}, {1, 2}, {1, 3}, {2, 3}, {2, 4}, {3, 4}},
    };
    try {
        Mapping invalidSequentialMapping;
        invalidSequentialMapping.mapping_line(rejoiningSequentialFanout,
                                               MappingMode::Sequential);
        std::cerr << "sequential mapping accepted fanout split/rejoin\n";
        return 11;
    } catch (const std::runtime_error&) {
    }

    std::vector<std::vector<position>> duplicateSequentialConnection{
        {{1, 1}, {1, 2}, {2, 2}, {3, 2}},
        {{1, 1}, {2, 1}, {2, 2}, {3, 2}},
    };
    try {
        Mapping invalidSequentialMapping;
        invalidSequentialMapping.mapping_line(duplicateSequentialConnection,
                                               MappingMode::Sequential);
        std::cerr << "sequential mapping accepted duplicate endpoints\n";
        return 18;
    } catch (const std::runtime_error&) {
    }

    std::vector<std::vector<position>> sharedForeignEdge{
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
        {{1, 0}, {1, 1}, {2, 1}, {2, 2}},
    };
    try {
        Mapping invalidSequentialMapping;
        invalidSequentialMapping.mapping_line(sharedForeignEdge,
                                               MappingMode::Sequential);
        std::cerr << "sequential mapping accepted a shared foreign edge\n";
        return 19;
    } catch (const std::runtime_error&) {
    }

    std::vector<std::vector<position>> strictPrefixFanout{
        {{1, 1}, {1, 2}, {2, 2}},
        {{1, 1}, {1, 2}, {2, 2}, {3, 2}},
    };
    try {
        Mapping invalidSequentialMapping;
        invalidSequentialMapping.mapping_line(strictPrefixFanout,
                                               MappingMode::Sequential);
        std::cerr << "sequential mapping accepted a strict-prefix fanout\n";
        return 20;
    } catch (const std::runtime_error&) {
    }

    IfcnMappingModeResolver explicitSequential;
    explicitSequential.observeModeValue(" Sequential ");
    if (explicitSequential.resolve().mode != MappingMode::Sequential ||
        !explicitSequential.resolve().explicitMode) {
        std::cerr << "explicit sequential IFCN mode was not resolved\n";
        return 12;
    }

    IfcnMappingModeResolver legacyFeedback;
    legacyFeedback.observeIterationDistance(1);
    if (legacyFeedback.resolve().mode != MappingMode::Sequential ||
        !legacyFeedback.resolve().inferredFromIterationDistance) {
        std::cerr << "legacy feedback IFCN mode was not inferred\n";
        return 13;
    }

    IfcnMappingModeResolver legacyCut;
    legacyCut.observeFlowValue("sequential register-cut P&R v0");
    if (legacyCut.resolve().mode != MappingMode::Sequential ||
        !legacyCut.resolve().inferredFromLegacyFlow) {
        std::cerr << "legacy register-cut IFCN mode was not inferred\n";
        return 14;
    }

    try {
        IfcnMappingModeResolver conflictingSemantics;
        conflictingSemantics.observeModeValue("combinational");
        conflictingSemantics.observeIterationDistance(1);
        static_cast<void>(conflictingSemantics.resolve());
        std::cerr << "combinational mode accepted positive iteration distance\n";
        return 15;
    } catch (const std::runtime_error&) {
    }

    try {
        IfcnMappingModeResolver invalidMode;
        invalidMode.observeModeValue("sequential-v2");
        std::cerr << "unknown IFCN mapping mode was accepted\n";
        return 16;
    } catch (const std::runtime_error&) {
    }
    return 0;
}
