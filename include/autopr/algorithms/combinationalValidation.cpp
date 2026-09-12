#include "combinationalValidation.h"

#include "mapping.h"
#include <autopr/graph/circuitGraph.h>
#include <algorithm>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace fcngraph {
CombinationalLayoutMetrics validateCombinationalLayout(
    Parse& parse, CircuitGraph& graph, int phaseCount, int maxSamePhase)
{
    CombinationalLayoutMetrics result;
    const auto fail = [&](const std::string& message) {
        result.error = message;
        return result;
    };
    try {
        if (graph.nodeIndex_pos.empty() || graph.routes.empty())
            return fail("empty placement or routing");
        NodeLinkMap links;
        std::set<position> nodePositions, occupied;
        std::set<int> drawable;
        for (const auto& layer : parse.getlayerNodeDivVec())
            drawable.insert(layer.begin(), layer.end());
        if (drawable.size() != graph.nodeIndex_pos.size())
            return fail("placement does not cover the drawable circuit nodes");
        for (const auto& node : graph.nodeIndex_pos) {
            if (!drawable.count(node.first)) return fail("unexpected placed node");
            if (!nodePositions.insert(node.second).second)
                return fail("multiple nodes share a coordinate");
            occupied.insert(node.second);
            links[{node.second, parse.getNodeType(node.first)}] = {{}, {}};
        }
        const auto edges = parse.getEffectiveEdges();
        const std::set<std::pair<int, int>> expected(edges.begin(), edges.end());
        if (expected.size() != edges.size() || graph.routes.size() != expected.size())
            return fail("routes do not cover each effective edge exactly once");
        std::map<unsigned int, std::set<position>> sinkPorts;
        std::vector<std::vector<position>> geometry;
        for (const auto& route : graph.routes) {
            const auto edge = route.first;
            const auto& path = route.second;
            if (!expected.count({edge.first, edge.second}))
                return fail("unexpected route edge");
            if (path.size() < 2) return fail("route has fewer than two coordinates");
            if (!graph.nodeIndex_pos.count(edge.first) || !graph.nodeIndex_pos.count(edge.second))
                return fail("route references a missing node");
            if (path.front() != graph.nodeIndex_pos.at(edge.first) ||
                path.back() != graph.nodeIndex_pos.at(edge.second))
                return fail("route endpoints disagree with node positions");
            if (!sinkPorts[edge.second].insert(path[path.size() - 2]).second)
                return fail("multiple fanins share one physical gate port");
            std::set<position> unique;
            for (std::size_t i = 0; i < path.size(); ++i) {
                if (!unique.insert(path[i]).second) return fail("route repeats a coordinate");
                if (i && std::abs(static_cast<long long>(path[i].first) - path[i - 1].first) +
                         std::abs(static_cast<long long>(path[i].second) - path[i - 1].second) != 1)
                    return fail("route is not four-connected");
                if (i && i + 1 < path.size() && nodePositions.count(path[i]))
                    return fail("route traverses an intermediate gate");
                occupied.insert(path[i]);
            }
            links[{path.front(), parse.getNodeType(edge.first)}].second.push_back(path[1]);
            links[{path.back(), parse.getNodeType(edge.second)}].first.push_back(path[path.size() - 2]);
            result.routeLength += path.size() - 1;
            geometry.push_back(path);
        }
        if (!graph.validateAssignedRoutePhases(phaseCount, maxSamePhase))
            return fail("global clock epochs or gate clock boundaries are invalid");
        for (auto& entry : links) {
            for (auto* ports : {&entry.second.first, &entry.second.second}) {
                std::sort(ports->begin(), ports->end());
                ports->erase(std::unique(ports->begin(), ports->end()), ports->end());
            }
        }
        Mapping mapping;
        mapping.node_mapping(links, MappingMode::Combinational);
        mapping.mapping_line(geometry, MappingMode::Combinational);
        std::string error;
        if (!mapping.validate_crossovers(&error)) return fail("device crossover: " + error);
        result.physicalCells = mapping.physicalCellSites(geometry, MappingMode::Combinational).size();
        if (!result.physicalCells) return fail("mapped device contains no cells");
        unsigned int minX = std::numeric_limits<unsigned int>::max();
        unsigned int minY = minX, maxX = 0, maxY = 0;
        for (const auto& point : occupied) {
            minX = std::min(minX, point.first); minY = std::min(minY, point.second);
            maxX = std::max(maxX, point.first); maxY = std::max(maxY, point.second);
        }
        result.width = static_cast<std::uint64_t>(maxX) - minX + 1;
        result.height = static_cast<std::uint64_t>(maxY) - minY + 1;
        result.area = result.width * result.height;
        result.occupiedTiles = occupied.size();
        result.valid = true;
        return result;
    } catch (const std::exception& error) {
        return fail(error.what());
    }
}
}
