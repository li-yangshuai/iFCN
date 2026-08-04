#include "autopr/algorithms/astar.h"
#include "autopr/algorithms/mapping.h"
#include "autopr/graph/circuitGraph.h"
#include "autopr/graph/parse.h"
#include "autopr/grid/grid.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>

namespace {

std::pair<int, int> occupiedDimensions(const fcngraph::GridChessboard &board)
{
    unsigned int minX = std::numeric_limits<unsigned int>::max();
    unsigned int minY = std::numeric_limits<unsigned int>::max();
    unsigned int maxX = 0;
    unsigned int maxY = 0;
    bool found = false;
    for (const auto &cell : board.gridMap) {
        if (cell.second.get_current_weight() == 0) {
            continue;
        }
        found = true;
        minX = std::min(minX, cell.first.first);
        minY = std::min(minY, cell.first.second);
        maxX = std::max(maxX, cell.first.first);
        maxY = std::max(maxY, cell.first.second);
    }
    if (!found) {
        return {0, 0};
    }
    return {static_cast<int>(maxX - minX + 1),
            static_cast<int>(maxY - minY + 1)};
}

bool validateSourceAwareCrossings(
    const std::map<std::pair<unsigned int, unsigned int>,
                   std::vector<fcngraph::position>> &routes,
    std::string &error)
{
    enum class Orientation { Horizontal, Vertical, Bend };
    std::map<fcngraph::position,
             std::map<unsigned int, std::set<Orientation>>> uses;
    for (const auto &route : routes) {
        const auto &path = route.second;
        if (path.size() < 2) {
            error = "route has fewer than two points";
            return false;
        }
        std::set<fcngraph::position> unique(path.begin(), path.end());
        if (unique.size() != path.size()) {
            error = "route repeats a coordinate";
            return false;
        }
        for (std::size_t index = 1; index < path.size(); ++index) {
            const int distance =
                std::abs(static_cast<int>(path[index].first) -
                         static_cast<int>(path[index - 1].first)) +
                std::abs(static_cast<int>(path[index].second) -
                         static_cast<int>(path[index - 1].second));
            if (distance != 1) {
                error = "route is not 4-connected";
                return false;
            }
        }
        for (std::size_t index = 1; index + 1 < path.size(); ++index) {
            Orientation orientation = Orientation::Bend;
            if (path[index - 1].second == path[index].second &&
                path[index].second == path[index + 1].second) {
                orientation = Orientation::Horizontal;
            } else if (path[index - 1].first == path[index].first &&
                       path[index].first == path[index + 1].first) {
                orientation = Orientation::Vertical;
            }
            uses[path[index]][route.first.first].insert(orientation);
        }
    }

    std::set<std::pair<unsigned int, unsigned int>> crossedPairs;
    for (const auto &entry : uses) {
        if (entry.second.size() <= 1) {
            continue;
        }
        if (entry.second.size() != 2) {
            error = "more than two source trees share one coordinate";
            return false;
        }
        const auto first = entry.second.begin();
        const auto second = std::next(first);
        const bool firstH = first->second == std::set<Orientation>{Orientation::Horizontal};
        const bool firstV = first->second == std::set<Orientation>{Orientation::Vertical};
        const bool secondH = second->second == std::set<Orientation>{Orientation::Horizontal};
        const bool secondV = second->second == std::set<Orientation>{Orientation::Vertical};
        if (!((firstH && secondV) || (firstV && secondH))) {
            error = "different sources overlap without a single straight H/V crossing";
            return false;
        }
        const auto pair = std::minmax(first->first, second->first);
        if (!crossedPairs.insert(pair).second) {
            error = "the same source-tree pair crosses more than once";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string source = argc > 1
        ? std::string(argv[1])
        : std::string(IFCN_TEST_SOURCE_DIR) + "/tests/benchmarks_f/TOY/xor2.v";
    const double gridSize = argc > 2 ? std::stod(argv[2]) : 40.0;
    const double searchCost = argc > 3 ? std::stod(argv[3]) : 40.0;
    const int routeOrderRetries = argc > 4 ? std::stoi(argv[4]) : 24;

    fcngraph::Parse parse;
    parse.parseVerilog(source);
    parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    parse.addLayerRedundancyNode();
    parse.caculateSameLayerNodeRoutePair();

    fcngraph::GridChessboard board;
    fcngraph::Astar router(board, false, searchCost);
    router.setAllowInterSourceWireOverlap(false);
    fcngraph::CircuitGraph graph(parse, source, board, router);
    graph.setFitnessCallback([](const std::string &message) {
        if (message.rfind("June random-clock graph P&R:", 0) == 0) {
            std::cerr << message << '\n';
        }
    });

    if (!graph.placeAndRouteJuneRandomClock(4, gridSize, routeOrderRetries)) {
        std::cerr << "June random-clock Graph P&R candidate failed: grid=/"
                  << gridSize << ", A* cost=" << searchCost << ".\n";
        return 10;
    }
    if (graph.routes.size() != parse.getEffectiveEdges().size()) {
        std::cerr << "Route count does not match the effective edge count.\n";
        return 11;
    }
    if (!graph.validateAssignedRoutePhases(4)) {
        std::cerr << "Assigned phases violate the 4-phase route contract.\n";
        return 12;
    }
    std::string sourceCrossingError;
    if (!validateSourceAwareCrossings(graph.routes, sourceCrossingError)) {
        std::cerr << "Source-aware crossing validation failed: "
                  << sourceCrossingError << '\n';
        return 13;
    }
    std::set<int> drawableNodes;
    for (const auto &layer : parse.getlayerNodeDivVec()) {
        drawableNodes.insert(layer.begin(), layer.end());
    }
    if (graph.nodeIndex_pos.size() != drawableNodes.size()) {
        std::cerr << "Not all drawable nodes received a placement.\n";
        return 14;
    }

    for (const auto &route : graph.routes) {
        if (route.second.empty()) {
            std::cerr << "An accepted route is empty.\n";
            return 15;
        }
        for (const auto &position : route.second) {
            const auto cell = board.gridMap.find(position);
            if (cell == board.gridMap.end() ||
                cell->second.getPhase() < 1 || cell->second.getPhase() > 4) {
                std::cerr << "A routed cell is missing a legal 1..4 phase.\n";
                return 16;
            }
        }
    }

    std::vector<std::vector<fcngraph::position>> routeGeometry;
    for (const auto &route : graph.routes) {
        routeGeometry.push_back(route.second);
    }
    fcngraph::Mapping mapping;
    mapping.mapping_line(routeGeometry);
    std::string crossoverError;
    if (!mapping.validate_crossovers(&crossoverError)) {
        std::cerr << "Mapped crossover validation failed: " << crossoverError << '\n';
        return 17;
    }

    const auto dimensions = occupiedDimensions(board);
    if (dimensions.first <= 0 || dimensions.second <= 0) {
        std::cerr << "Accepted layout has no occupied bounds.\n";
        return 18;
    }
    std::cout << "June 2025 random-clock Graphviz P&R regression passed: "
              << graph.nodeIndex_pos.size() << " nodes, "
              << graph.routes.size() << " routes, "
              << dimensions.first << 'x' << dimensions.second << '='
              << dimensions.first * dimensions.second << " occupied-grid area.\n";
    return 0;
}
