#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/mapping.h>
#include <autopr/graph/circuitGraph.h>
#include <autopr/graph/parse.h>
#include <autopr/grid/grid.h>
#include <autopr/sequential/globalPhaseSolver.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

using fcngraph::Astar;
using fcngraph::CircuitGraph;
using fcngraph::GridChessboard;
using fcngraph::Mapping;
using fcngraph::NodeLinkMap;
using fcngraph::Parse;
using fcngraph::PhysicalCellSite;
using fcngraph::position;
using namespace fcngraph::sequential;

using Edge = std::pair<int, int>;
using RouteKey = std::pair<unsigned int, unsigned int>;
using RouteMap = std::map<RouteKey, std::vector<position>>;
using SteadyClock = std::chrono::steady_clock;

struct StateBoundary
{
    std::string dataEvent;
    std::string qEvent;
};

struct PhysicalNetlist
{
    // The value is the logical iteration distance of the physical route.
    std::map<Edge, int> distance;
    std::set<int> removedQNodes;
    std::vector<Edge> feedbackEdges;
};

struct MappedPhysicalLayout
{
    std::vector<Edge> edges;
    std::vector<std::vector<position>> coarseRoutes;
    std::vector<std::vector<PhysicalCellSite>> orderedRoutes;
    fcngraph::RouteCellMap routeCells;
    fcngraph::RouteCellMap crossoverCells;
    std::map<std::string, std::vector<position>> nodeCells;
    std::set<position> uniqueCells;
    std::set<PhysicalCellSite> cellSites;
    std::size_t crossoverSegments = 0;
};

struct GeometryMetrics
{
    std::uint64_t bboxArea = std::numeric_limits<std::uint64_t>::max();
    std::size_t routeSteps = std::numeric_limits<std::size_t>::max();
    unsigned int maxDimension = std::numeric_limits<unsigned int>::max();
    unsigned int perimeter = std::numeric_limits<unsigned int>::max();
    unsigned int width = 0;
    unsigned int height = 0;
};

struct SeamCompactionStats
{
    std::size_t statesExplored = 0;
    std::size_t legalMoves = 0;
    unsigned int rowsRemoved = 0;
    unsigned int columnsRemoved = 0;
    bool optimalityProven = false;
};

struct GeometryCandidate
{
    GeometryMetrics metrics;
    std::map<int, position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
    std::unordered_map<position, fcngraph::GridCell,
                       fcngraph::PositionHash> gridCells;
    SeamCompactionStats seamCompaction;
};

struct GeometrySelectionStats
{
    std::size_t placementCandidates = 0;
    std::size_t qFilteredPlacementCandidates = 0;
    std::size_t legacyPlacementFallbackCandidates = 0;
    std::size_t searchCostCandidates = 0;
    std::size_t routingWindowTemplates = 0;
    std::size_t boundedRoutingAttempts = 0;
    std::size_t unboundedRoutingAttempts = 0;
    std::size_t boundedRoutedCandidates = 0;
    std::size_t unboundedRoutedCandidates = 0;
    std::size_t routedCandidates = 0;
    std::size_t drcValidCandidates = 0;
    std::size_t rawDistinctCandidates = 0;
    std::size_t distinctCandidates = 0;
    std::size_t selectedRank = 0;
    std::size_t compactionMaxStates = 0;
    std::size_t compactionSeedCandidates = 0;
    std::size_t compactionStatesExplored = 0;
    std::size_t compactionLegalMoves = 0;
    std::size_t compactionReducedCandidates = 0;
    std::size_t compactionProvenCandidates = 0;
    bool allRawCandidatesCompacted = false;
    bool allCompactionOptimalityProven = false;
    SeamCompactionStats selectedCompaction;
};

struct CommandLine
{
    std::string input;
    std::string output;
    std::string latexOutput;
    std::vector<StateBoundary> states;
    std::vector<int> iiCandidates{4, 8, 12, 16, 20, 24};
    int maxSamePhaseTiles = 4;
    std::uint64_t maxDfsNodes = 5000000;
    unsigned int spacing = 2;
    double routeSearchCost = 80.0;
    std::size_t compactionMaxStates = 256;
    std::size_t compactionSeedLimit = 16;
    std::size_t geometryRank = 0;
    std::string clockProblemOutput;
    std::string phaseSolutionInput;
    bool deferPhase = false;
};

std::string usage()
{
    return
        "usage: ifcn_paper_cyclic_pnr <cut-dag.v> <output.ifcn> "
        "--state <D-event>:<Q-event> [--state ...] "
        "[--ii 4,8,12,16,20,24] [--max-same-phase 4] "
        "[--max-dfs-nodes 5000000] [--spacing 2] "
        "[--route-search-cost 80] [--tex output.tex] "
        "[--compaction-max-states 256] [--compaction-seeds 16] "
        "[--geometry-rank 0] "
        "[--clock-problem-out problem.json] "
        "[--phase-solution solution.tsv] [--defer-phase]\n";
}

std::vector<int> parseIntegerList(const std::string &text)
{
    std::vector<int> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ','))
    {
        if (token.empty())
        {
            throw std::runtime_error("empty value in integer list: " + text);
        }
        const int value = std::stoi(token);
        if (value <= 0)
        {
            throw std::runtime_error("II values must be positive");
        }
        result.push_back(value);
    }
    if (result.empty())
    {
        throw std::runtime_error("at least one II candidate is required");
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

StateBoundary parseState(const std::string &text)
{
    const auto separator = text.find(':');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= text.size() ||
        text.find(':', separator + 1) != std::string::npos)
    {
        throw std::runtime_error(
            "state boundary must have the form D-event:Q-event: " + text);
    }
    return {text.substr(0, separator), text.substr(separator + 1)};
}

CommandLine parseCommandLine(int argc, char **argv)
{
    if (argc < 5)
    {
        throw std::runtime_error(usage());
    }
    CommandLine command;
    command.input = argv[1];
    command.output = argv[2];
    for (int index = 3; index < argc; ++index)
    {
        const std::string option(argv[index]);
        const auto value = [&](const char *name) -> std::string {
            if (index + 1 >= argc)
            {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++index];
        };
        if (option == "--state")
        {
            command.states.push_back(parseState(value("--state")));
        }
        else if (option == "--ii")
        {
            command.iiCandidates = parseIntegerList(value("--ii"));
        }
        else if (option == "--max-same-phase")
        {
            command.maxSamePhaseTiles = std::stoi(value("--max-same-phase"));
            if (command.maxSamePhaseTiles <= 0 ||
                command.maxSamePhaseTiles > 4)
            {
                throw std::runtime_error(
                    "--max-same-phase must be between 1 and 4 tiles");
            }
        }
        else if (option == "--max-dfs-nodes")
        {
            const std::string raw = value("--max-dfs-nodes");
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(raw, &consumed);
            if (consumed != raw.size() || parsed == 0)
            {
                throw std::runtime_error(
                    "--max-dfs-nodes must be a positive integer");
            }
            command.maxDfsNodes = static_cast<std::uint64_t>(parsed);
        }
        else if (option == "--spacing")
        {
            const int spacing = std::stoi(value("--spacing"));
            if (spacing < 2)
            {
                throw std::runtime_error("--spacing must be at least 2");
            }
            command.spacing = static_cast<unsigned int>(spacing);
        }
        else if (option == "--route-search-cost")
        {
            command.routeSearchCost = std::stod(value("--route-search-cost"));
            if (command.routeSearchCost <= 0.0)
            {
                throw std::runtime_error("--route-search-cost must be positive");
            }
        }
        else if (option == "--compaction-max-states")
        {
            const std::string raw = value("--compaction-max-states");
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(raw, &consumed);
            if (consumed != raw.size() || parsed == 0)
            {
                throw std::runtime_error(
                    "--compaction-max-states must be a positive integer");
            }
            command.compactionMaxStates = static_cast<std::size_t>(parsed);
        }
        else if (option == "--compaction-seeds")
        {
            const std::string raw = value("--compaction-seeds");
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(raw, &consumed);
            if (consumed != raw.size())
            {
                throw std::runtime_error(
                    "--compaction-seeds must be a non-negative integer");
            }
            command.compactionSeedLimit = static_cast<std::size_t>(parsed);
        }
        else if (option == "--geometry-rank")
        {
            const std::string raw = value("--geometry-rank");
            std::size_t consumed = 0;
            const unsigned long long parsed = std::stoull(raw, &consumed);
            if (consumed != raw.size())
            {
                throw std::runtime_error(
                    "--geometry-rank must be a non-negative integer");
            }
            command.geometryRank = static_cast<std::size_t>(parsed);
        }
        else if (option == "--tex")
        {
            command.latexOutput = value("--tex");
        }
        else if (option == "--clock-problem-out")
        {
            command.clockProblemOutput = value("--clock-problem-out");
        }
        else if (option == "--phase-solution")
        {
            command.phaseSolutionInput = value("--phase-solution");
        }
        else if (option == "--defer-phase")
        {
            command.deferPhase = true;
        }
        else
        {
            throw std::runtime_error("unknown option: " + option + "\n" + usage());
        }
    }
    if (command.states.empty())
    {
        throw std::runtime_error("at least one --state D:Q boundary is required");
    }
    return command;
}

std::vector<std::vector<int>> parseLayers(const Parse &parse)
{
    std::vector<std::vector<int>> layers;
    for (const auto &layer : parse.getlayerNodeDivVec())
    {
        layers.emplace_back(layer.begin(), layer.end());
    }
    return layers;
}

std::vector<std::vector<int>> physicalPlacementLayers(
    const Parse &parse,
    const std::set<int> &excludedNodes)
{
    std::vector<std::vector<int>> layers = parseLayers(parse);
    for (auto &layer : layers)
    {
        layer.erase(
            std::remove_if(layer.begin(), layer.end(),
                           [&](int node) {
                               return excludedNodes.count(node) != 0;
                           }),
            layer.end());
    }
    return layers;
}

std::string layerOrderSignature(
    const std::vector<std::vector<int>> &layers)
{
    std::ostringstream signature;
    for (const auto &layer : layers)
    {
        signature << '[';
        for (const int node : layer)
        {
            signature << node << ',';
        }
        signature << ']';
    }
    return signature.str();
}

std::vector<std::vector<std::vector<int>>> feedbackAwareLayerOrders(
    const std::vector<std::vector<int>> &seed,
    const PhysicalNetlist &netlist)
{
    std::map<int, std::vector<std::pair<int, double>>> adjacency;
    for (const auto &edge : netlist.distance)
    {
        // Feedback endpoints deserve extra pull: they otherwise sit at the
        // opposite ends of a schedule-only layered placement.
        const double weight = edge.second == 1 ? 3.0 : 1.0;
        adjacency[edge.first.first].push_back({edge.first.second, weight});
        adjacency[edge.first.second].push_back({edge.first.first, weight});
    }

    const auto refine = [&](std::vector<std::vector<int>> layers,
                            bool highToLowFirst) {
        for (int pass = 0; pass < 6; ++pass)
        {
            const bool highToLow = (pass % 2 == 0)
                                       ? highToLowFirst
                                       : !highToLowFirst;
            for (std::size_t step = 0; step < layers.size(); ++step)
            {
                const std::size_t layerIndex = highToLow
                    ? layers.size() - step - 1
                    : step;
                auto &layer = layers[layerIndex];
                if (layer.size() < 2)
                {
                    continue;
                }

                std::map<int, double> normalizedPosition;
                for (const auto &positionedLayer : layers)
                {
                    const double divisor = static_cast<double>(
                        std::max<std::size_t>(1, positionedLayer.size()));
                    for (std::size_t index = 0;
                         index < positionedLayer.size(); ++index)
                    {
                        normalizedPosition[positionedLayer[index]] =
                            (static_cast<double>(index) + 0.5) / divisor;
                    }
                }

                const auto barycenter = [&](int node) {
                    double weightedPosition = 0.0;
                    double totalWeight = 0.0;
                    const auto neighbors = adjacency.find(node);
                    if (neighbors != adjacency.end())
                    {
                        for (const auto &neighbor : neighbors->second)
                        {
                            const auto position =
                                normalizedPosition.find(neighbor.first);
                            if (position == normalizedPosition.end())
                            {
                                continue;
                            }
                            weightedPosition += position->second * neighbor.second;
                            totalWeight += neighbor.second;
                        }
                    }
                    return totalWeight == 0.0
                        ? normalizedPosition.at(node)
                        : weightedPosition / totalWeight;
                };
                std::stable_sort(
                    layer.begin(), layer.end(),
                    [&](int left, int right) {
                        const double leftScore = barycenter(left);
                        const double rightScore = barycenter(right);
                        if (leftScore != rightScore)
                        {
                            return leftScore < rightScore;
                        }
                        return left < right;
                    });
            }
        }
        return layers;
    };

    std::vector<std::vector<std::vector<int>>> result;
    std::set<std::string> seen;
    const auto add = [&](std::vector<std::vector<int>> candidate) {
        const std::string signature = layerOrderSignature(candidate);
        if (seen.insert(signature).second)
        {
            result.push_back(std::move(candidate));
        }
    };
    add(seed);
    add(refine(seed, false));
    add(refine(seed, true));

    std::vector<std::vector<int>> mirrored = seed;
    for (auto &layer : mirrored)
    {
        std::reverse(layer.begin(), layer.end());
    }
    add(refine(std::move(mirrored), false));

    // Barycenter sweeps can be trapped by an already locally sorted layer.
    // A bounded adjacent-swap neighborhood exposes alternate port/crossover
    // topologies without factorial permutation growth.
    constexpr std::size_t maxOrderCandidates = 9;
    for (std::size_t layerIndex = 0;
         layerIndex < seed.size() && result.size() < maxOrderCandidates;
         ++layerIndex)
    {
        for (std::size_t index = 0;
             index + 1 < seed[layerIndex].size() &&
             result.size() < maxOrderCandidates;
             ++index)
        {
            auto candidate = seed;
            std::swap(candidate[layerIndex][index],
                      candidate[layerIndex][index + 1]);
            add(std::move(candidate));
        }
    }
    return result;
}

PhysicalNetlist makePhysicalNetlist(
    Parse &parse,
    const std::vector<StateBoundary> &states)
{
    std::map<int, int> qToD;
    PhysicalNetlist result;
    for (const auto &state : states)
    {
        const int data = parse.getVertexIndex(state.dataEvent);
        const int q = parse.getVertexIndex(state.qEvent);
        if (data < 0)
        {
            throw std::runtime_error(
                "state D event is absent from cut DAG: " + state.dataEvent);
        }
        if (q < 0)
        {
            throw std::runtime_error(
                "state Q event is absent from cut DAG: " + state.qEvent);
        }
        if (data == q)
        {
            throw std::runtime_error("state D and Q events must be distinct");
        }
        if (!qToD.emplace(q, data).second)
        {
            throw std::runtime_error(
                "duplicate Q event in state boundaries: " + state.qEvent);
        }
        result.removedQNodes.insert(q);
    }

    std::map<int, std::size_t> replacedFanouts;
    for (const auto &raw : parse.getEffectiveEdges())
    {
        const Edge edge{raw.first, raw.second};
        const auto sourceState = qToD.find(edge.first);
        if (sourceState != qToD.end())
        {
            const Edge feedback{sourceState->second, edge.second};
            const auto [position, inserted] = result.distance.emplace(feedback, 1);
            if (!inserted)
            {
                throw std::runtime_error(
                    "physical feedback edge collides with another net: " +
                    parse.getNodeName(feedback.first) + " -> " +
                    parse.getNodeName(feedback.second));
            }
            result.feedbackEdges.push_back(feedback);
            ++replacedFanouts[edge.first];
            continue;
        }
        if (result.removedQNodes.count(edge.second) != 0)
        {
            throw std::runtime_error(
                "cut DAG unexpectedly contains an incoming edge to Q pseudo input");
        }
        if (!result.distance.emplace(edge, 0).second)
        {
            throw std::runtime_error("duplicate effective edge in cut DAG");
        }
    }

    for (const auto &state : qToD)
    {
        if (replacedFanouts[state.first] == 0)
        {
            throw std::runtime_error(
                "Q pseudo input has no fanout to replace: " +
                parse.getNodeName(state.first));
        }
    }
    if (result.feedbackEdges.empty())
    {
        throw std::runtime_error("no physical feedback edge was constructed");
    }
    return result;
}

bool hasDirectedCycle(const PhysicalNetlist &netlist,
                      const std::map<int, position> &nodes)
{
    std::map<int, std::vector<int>> adjacency;
    for (const auto &node : nodes)
    {
        adjacency[node.first];
    }
    for (const auto &net : netlist.distance)
    {
        adjacency[net.first.first].push_back(net.first.second);
    }
    std::map<int, int> color;
    const auto visit = [&](const auto &self, int node) -> bool {
        color[node] = 1;
        for (const int sink : adjacency[node])
        {
            if (color[sink] == 1)
            {
                return true;
            }
            if (color[sink] == 0 && self(self, sink))
            {
                return true;
            }
        }
        color[node] = 2;
        return false;
    };
    for (const auto &node : adjacency)
    {
        if (color[node.first] == 0 && visit(visit, node.first))
        {
            return true;
        }
    }
    return false;
}

std::uint64_t manhattan(const position &left, const position &right)
{
    const auto distance = [](unsigned int a, unsigned int b) {
        return a >= b ? static_cast<std::uint64_t>(a - b)
                      : static_cast<std::uint64_t>(b - a);
    };
    return distance(left.first, right.first) +
           distance(left.second, right.second);
}

GeometryMetrics geometryMetrics(
    const std::map<int, position> &nodePositions,
    const std::map<std::pair<unsigned int, unsigned int>,
                   std::vector<position>> &routes)
{
    GeometryMetrics metrics;
    std::set<position> occupied;
    for (const auto &node : nodePositions)
    {
        occupied.insert(node.second);
    }
    metrics.routeSteps = 0;
    for (const auto &route : routes)
    {
        occupied.insert(route.second.begin(), route.second.end());
        metrics.routeSteps += route.second.empty() ? 0 : route.second.size() - 1;
    }
    if (occupied.empty())
    {
        metrics.bboxArea = 0;
        metrics.maxDimension = 0;
        metrics.perimeter = 0;
        return metrics;
    }
    unsigned int minX = occupied.begin()->first;
    unsigned int maxX = minX;
    unsigned int minY = occupied.begin()->second;
    unsigned int maxY = minY;
    for (const auto &site : occupied)
    {
        minX = std::min(minX, site.first);
        maxX = std::max(maxX, site.first);
        minY = std::min(minY, site.second);
        maxY = std::max(maxY, site.second);
    }
    metrics.width = maxX - minX + 1;
    metrics.height = maxY - minY + 1;
    metrics.bboxArea = static_cast<std::uint64_t>(metrics.width) * metrics.height;
    metrics.maxDimension = std::max(metrics.width, metrics.height);
    metrics.perimeter = metrics.width + metrics.height;
    return metrics;
}

GeometryMetrics geometryMetrics(const CircuitGraph &graph)
{
    return geometryMetrics(graph.nodeIndex_pos, graph.routes);
}

bool betterGeometry(const GeometryMetrics &candidate,
                    const GeometryMetrics &incumbent)
{
    // Area is the primary physical objective. Route length is secondary;
    // aspect-ratio and perimeter break ties without weakening feasibility.
    return std::tie(candidate.bboxArea, candidate.routeSteps,
                    candidate.maxDimension, candidate.perimeter) <
           std::tie(incumbent.bboxArea, incumbent.routeSteps,
                    incumbent.maxDimension, incumbent.perimeter);
}

std::size_t validateAndCountMappedCells(Parse &parse,
                                        const CircuitGraph &graph,
                                        const PhysicalNetlist &netlist,
                                        std::size_t *crossoverSegments = nullptr);
std::size_t validateAndCountMappedCells(
    Parse &parse,
    const std::map<int, position> &nodePositions,
    const RouteMap &routes,
    const PhysicalNetlist &netlist,
    std::size_t *crossoverSegments = nullptr);
bool validateCyclicGeometry(
    Parse &parse,
    const PhysicalNetlist &netlist,
    const std::map<int, position> &nodePositions,
    const RouteMap &routes,
    std::string *error);

struct RoutingWindow
{
    position minimum{0, 0};
    position maximum{0, 0};
};

std::vector<RoutingWindow> compactRoutingWindows(
    const std::map<int, position> &nodePositions)
{
    if (nodePositions.empty())
    {
        return {};
    }

    unsigned int minX = nodePositions.begin()->second.first;
    unsigned int maxX = minX;
    unsigned int minY = nodePositions.begin()->second.second;
    unsigned int maxY = minY;
    for (const auto &node : nodePositions)
    {
        minX = std::min(minX, node.second.first);
        maxX = std::max(maxX, node.second.first);
        minY = std::min(minY, node.second.second);
        maxY = std::max(maxY, node.second.second);
    }

    std::vector<RoutingWindow> windows;
    std::set<std::tuple<unsigned int, unsigned int,
                        unsigned int, unsigned int>> seen;
    const auto addWindow = [&](unsigned int left,
                               unsigned int right,
                               unsigned int top,
                               unsigned int bottom) {
        const unsigned int boundedMinX = left > minX ? 0 : minX - left;
        const unsigned int boundedMinY = top > minY ? 0 : minY - top;
        const unsigned int coordinateLimit =
            std::numeric_limits<unsigned int>::max();
        const unsigned int boundedMaxX =
            right > coordinateLimit - maxX ? coordinateLimit : maxX + right;
        const unsigned int boundedMaxY =
            bottom > coordinateLimit - maxY ? coordinateLimit : maxY + bottom;
        const auto signature = std::make_tuple(
            boundedMinX, boundedMinY, boundedMaxX, boundedMaxY);
        if (seen.insert(signature).second)
        {
            windows.push_back(RoutingWindow{
                {boundedMinX, boundedMinY},
                {boundedMaxX, boundedMaxY}});
        }
    };

    // Explore all ways to spend a two-track halo first.  Asymmetric windows
    // matter for feedback corridors: reserving one track only on the side
    // where a loop closes often saves an entire row or column.
    for (unsigned int left = 0; left <= 2; ++left)
    {
        for (unsigned int right = 0; right <= 2; ++right)
        {
            for (unsigned int top = 0; top <= 2; ++top)
            {
                for (unsigned int bottom = 0; bottom <= 2; ++bottom)
                {
                    if (left + right + top + bottom <= 2)
                    {
                        addWindow(left, right, top, bottom);
                    }
                }
            }
        }
    }
    addWindow(1, 1, 1, 1);
    addWindow(2, 2, 2, 2);

    std::stable_sort(
        windows.begin(), windows.end(),
        [](const RoutingWindow &left, const RoutingWindow &right) {
            const auto dimensions = [](const RoutingWindow &window) {
                const std::uint64_t width =
                    static_cast<std::uint64_t>(window.maximum.first) -
                    window.minimum.first + 1;
                const std::uint64_t height =
                    static_cast<std::uint64_t>(window.maximum.second) -
                    window.minimum.second + 1;
                return std::make_tuple(width * height, width + height,
                                       window.minimum, window.maximum);
            };
            return dimensions(left) < dimensions(right);
        });
    return windows;
}

bool routePhysicalNetlist(
    CircuitGraph &graph,
    Parse &parse,
    GridChessboard &board,
    Astar &router,
    const PhysicalNetlist &netlist,
    bool exploreCompactWindows,
    GeometryMetrics *selectedMetrics,
    GeometrySelectionStats *selectionStats,
    std::vector<GeometryCandidate> *geometryCandidates)
{
    std::vector<Edge> baseEdges;
    for (const auto &entry : netlist.distance)
    {
        baseEdges.push_back(entry.first);
    }
    std::map<int, std::size_t> sourceFanout;
    std::map<int, std::size_t> sinkFanin;
    for (const auto &edge : baseEdges)
    {
        ++sourceFanout[edge.first];
        ++sinkFanin[edge.second];
    }
    std::set<position> nodePositions;
    for (const auto &node : graph.nodeIndex_pos)
    {
        nodePositions.insert(node.second);
    }
    const std::vector<RoutingWindow> routingWindows =
        exploreCompactWindows
            ? compactRoutingWindows(graph.nodeIndex_pos)
            : std::vector<RoutingWindow>{};
    if (selectionStats != nullptr)
    {
        selectionStats->routingWindowTemplates = std::max(
            selectionStats->routingWindowTemplates, routingWindows.size());
    }

    const auto prepare = [&]() {
        graph.routes.clear();
        router.reset();
        board.reset();
        for (const auto &node : graph.nodeIndex_pos)
        {
            if (!board.is_placeNode(node.second))
            {
                return false;
            }
            board.placeNode(node.second);
        }
        return true;
    };
    const auto crossesNode = [&](const std::vector<position> &path) {
        if (path.size() <= 2)
        {
            return false;
        }
        for (auto it = std::next(path.begin()); std::next(it) != path.end(); ++it)
        {
            if (nodePositions.count(*it) != 0)
            {
                return true;
            }
        }
        return false;
    };
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> bestRoutes;
    GeometryMetrics bestMetrics;
    const auto attempt = [&](const std::vector<Edge> &edges,
                             const RoutingWindow *window) {
        if (window != nullptr)
        {
            router.setSearchBounds(window->minimum, window->maximum);
            if (selectionStats != nullptr)
            {
                ++selectionStats->boundedRoutingAttempts;
            }
        }
        else
        {
            router.clearSearchBounds();
            if (selectionStats != nullptr)
            {
                ++selectionStats->unboundedRoutingAttempts;
            }
        }
        if (!prepare())
        {
            return false;
        }
        for (const auto &edge : edges)
        {
            const auto source = graph.nodeIndex_pos.find(edge.first);
            const auto sink = graph.nodeIndex_pos.find(edge.second);
            if (source == graph.nodeIndex_pos.end() ||
                sink == graph.nodeIndex_pos.end())
            {
                return false;
            }
            auto path = router.findPath(
                source->second, sink->second, sourceFanout[edge.first] > 1);
            if (path.size() < 2 || crossesNode(path))
            {
                return false;
            }
            graph.routes.emplace(edge, std::move(path));
        }
        if (graph.routes.size() != netlist.distance.size())
        {
            return false;
        }
        if (selectionStats != nullptr)
        {
            ++selectionStats->routedCandidates;
            if (window != nullptr)
            {
                ++selectionStats->boundedRoutedCandidates;
            }
            else
            {
                ++selectionStats->unboundedRoutedCandidates;
            }
        }
        std::string validationError;
        if (!validateCyclicGeometry(
                parse, netlist, graph.nodeIndex_pos, graph.routes,
                &validationError))
        {
            return false;
        }
        if (selectionStats != nullptr)
        {
            ++selectionStats->drcValidCandidates;
        }
        const GeometryMetrics candidateMetrics = geometryMetrics(graph);
        if (geometryCandidates != nullptr)
        {
            const auto duplicate = std::find_if(
                geometryCandidates->begin(), geometryCandidates->end(),
                [&](const GeometryCandidate &candidate) {
                    return candidate.nodePositions == graph.nodeIndex_pos &&
                           candidate.routes == graph.routes;
                });
            if (duplicate == geometryCandidates->end())
            {
                std::unordered_map<position, fcngraph::GridCell,
                                   fcngraph::PositionHash> occupiedCells;
                occupiedCells.reserve(board.gridMap.size());
                for (const auto &cell : board.gridMap)
                {
                    if (cell.second.get_current_weight() != 0)
                    {
                        occupiedCells.emplace(cell);
                    }
                }
                geometryCandidates->push_back(GeometryCandidate{
                    candidateMetrics, graph.nodeIndex_pos, graph.routes,
                    std::move(occupiedCells)});
            }
        }
        if (bestRoutes.empty() || betterGeometry(candidateMetrics, bestMetrics))
        {
            bestRoutes = graph.routes;
            bestMetrics = candidateMetrics;
        }
        return true;
    };

    const auto orderedEdges = [&](int policy) {
        auto edges = baseEdges;
        std::stable_sort(edges.begin(), edges.end(), [&](const Edge &lhs,
                                                         const Edge &rhs) {
            const bool lhsFeedback = netlist.distance.at(lhs) == 1;
            const bool rhsFeedback = netlist.distance.at(rhs) == 1;
            if (policy == 0 && lhsFeedback != rhsFeedback)
            {
                return lhsFeedback;
            }
            if (policy == 1 && sinkFanin[lhs.second] != sinkFanin[rhs.second])
            {
                return sinkFanin[lhs.second] > sinkFanin[rhs.second];
            }
            if (policy == 2 && sourceFanout[lhs.first] != sourceFanout[rhs.first])
            {
                return sourceFanout[lhs.first] > sourceFanout[rhs.first];
            }
            const auto lhsLength = manhattan(
                graph.nodeIndex_pos.at(lhs.first), graph.nodeIndex_pos.at(lhs.second));
            const auto rhsLength = manhattan(
                graph.nodeIndex_pos.at(rhs.first), graph.nodeIndex_pos.at(rhs.second));
            if (lhsLength != rhsLength)
            {
                return policy == 3 ? lhsLength < rhsLength
                                   : lhsLength > rhsLength;
            }
            return lhs < rhs;
        });
        return edges;
    };

    // First search inside tight, independently expanded routing windows.  A
    // window is only a candidate generator; the complete unbounded search is
    // retained below, so a feedback loop that genuinely needs an outer
    // corridor never loses routability.
    for (const RoutingWindow &window : routingWindows)
    {
        for (int policy = 0; policy < 4; ++policy)
        {
            (void)attempt(orderedEdges(policy), &window);
        }
    }

    std::mt19937 boundedGenerator(0x424f554eu); // "BOUN"
    auto boundedEdges = baseEdges;
    for (int retry = 0; retry < 32 && !routingWindows.empty(); ++retry)
    {
        std::shuffle(boundedEdges.begin(), boundedEdges.end(), boundedGenerator);
        const RoutingWindow &window =
            routingWindows[static_cast<std::size_t>(retry) %
                           routingWindows.size()];
        (void)attempt(boundedEdges, &window);
    }

    // Preserve the original four policies and all 48 seeded shuffles as the
    // completeness fallback instead of relying on one permanently hard box.
    for (int policy = 0; policy < 4; ++policy)
    {
        (void)attempt(orderedEdges(policy), nullptr);
    }
    std::mt19937 generator(0x4359434cu); // "CYCL"
    auto edges = baseEdges;
    for (int retry = 0; retry < 48; ++retry)
    {
        std::shuffle(edges.begin(), edges.end(), generator);
        (void)attempt(edges, nullptr);
    }
    router.clearSearchBounds();
    if (bestRoutes.empty())
    {
        return false;
    }
    graph.routes = std::move(bestRoutes);
    if (selectedMetrics != nullptr)
    {
        *selectedMetrics = bestMetrics;
    }
    return true;
}

std::string routeId(const Edge &edge)
{
    return "net." + std::to_string(edge.first) + "." +
           std::to_string(edge.second);
}

std::string tileResourceId(const position &tile)
{
    return "tile." + std::to_string(tile.first) + "." +
           std::to_string(tile.second);
}

GlobalClockProblem buildTileClockProblem(
    Parse &parse,
    const CircuitGraph &graph,
    const PhysicalNetlist &netlist,
    const std::vector<int> &iiCandidates,
    int maxSamePhaseTiles,
    std::uint64_t maxDfsNodes)
{
    GlobalClockProblem problem;
    problem.phaseCount = 4;
    problem.maxConsecutiveSamePhaseCells = maxSamePhaseTiles;
    problem.iiCandidates = iiCandidates;
    problem.maxDfsNodes = maxDfsNodes;

    for (const auto &node : graph.nodeIndex_pos)
    {
        problem.events.push_back(parse.getNodeName(node.first));
    }

    std::set<position> routedTiles;
    for (const auto &route : graph.routes)
    {
        routedTiles.insert(route.second.begin(), route.second.end());
    }
    for (const position &tile : routedTiles)
    {
        problem.clockResources.push_back(ClockResourceSpec{
            tileResourceId(tile),
            ClockResourceSharing::PhaseSharedIndependentEpochs});
    }

    // A tile is the clocking resource.  Same-source fanout occurrences on an
    // identical tile carry the same token/epoch.  A crossing from another
    // source shares only the tile's modulo phase.
    std::map<std::pair<int, position>, std::string> trunkEpochVariables;
    std::size_t nextTrunkVariable = 0;
    std::size_t routeIndex = 0;
    for (const auto &route : graph.routes)
    {
        const Edge edge{static_cast<int>(route.first.first),
                        static_cast<int>(route.first.second)};
        const int distance = netlist.distance.at(edge);
        const std::string id = routeId(edge);
        std::vector<std::string> occurrenceIds;
        occurrenceIds.reserve(route.second.size());
        for (std::size_t index = 0; index < route.second.size(); ++index)
        {
            const position tile = route.second[index];
            const std::string occurrence =
                "tile.route." + std::to_string(routeIndex) + ".occ." +
                std::to_string(index);
            std::string epochVariable;
            if (index == 0)
            {
                epochVariable = "tile.event." + parse.getNodeName(edge.first);
            }
            else if (index + 1 == route.second.size() && distance == 0)
            {
                epochVariable = "tile.event." + parse.getNodeName(edge.second);
            }
            else if (index + 1 == route.second.size())
            {
                epochVariable =
                    "tile.route." + std::to_string(routeIndex) +
                    ".next_sink.epoch";
            }
            else
            {
                const auto key = std::make_pair(edge.first, tile);
                const auto found = trunkEpochVariables.find(key);
                if (found != trunkEpochVariables.end())
                {
                    epochVariable = found->second;
                }
                else
                {
                    epochVariable =
                        "tile.trunk." +
                        std::to_string(nextTrunkVariable++) + ".epoch";
                    trunkEpochVariables.emplace(key, epochVariable);
                }
            }
            problem.occurrences.push_back(RouteOccurrenceSpec{
                occurrence, tileResourceId(tile), std::move(epochVariable)});
            occurrenceIds.push_back(occurrence);
        }
        problem.routes.push_back(FixedRouteSpec{
            id,
            parse.getNodeName(edge.first),
            parse.getNodeName(edge.second),
            distance,
            std::move(occurrenceIds)});
        ++routeIndex;
    }

    std::set<std::string> removedQNames;
    for (const int q : netlist.removedQNodes)
    {
        removedQNames.insert(parse.getNodeName(q));
    }
    for (const auto &input : parse.getVec_inputNodeName())
    {
        if (removedQNames.count(input) == 0)
        {
            problem.anchors.push_back(EpochAnchorSpec{input, 0});
        }
    }
    if (problem.anchors.empty() && !graph.nodeIndex_pos.empty())
    {
        problem.anchors.push_back(EpochAnchorSpec{
            parse.getNodeName(graph.nodeIndex_pos.begin()->first), 0});
    }
    return problem;
}

std::map<position, int> tilePhaseMap(
    const GlobalClockSolution &solution)
{
    std::map<position, int> phases;
    constexpr char prefix[] = "tile.";
    for (const auto &[resource, phase] : solution.clockResourcePhase)
    {
        if (resource.rfind(prefix, 0) != 0)
        {
            throw std::runtime_error(
                "unexpected tile clock resource id: " + resource);
        }
        const std::size_t separator =
            resource.find('.', sizeof(prefix) - 1);
        if (separator == std::string::npos ||
            resource.find('.', separator + 1) != std::string::npos)
        {
            throw std::runtime_error(
                "malformed tile clock resource id: " + resource);
        }
        const unsigned long x = std::stoul(resource.substr(
            sizeof(prefix) - 1,
            separator - (sizeof(prefix) - 1)));
        const unsigned long y = std::stoul(resource.substr(separator + 1));
        phases.emplace(position{static_cast<unsigned int>(x),
                                static_cast<unsigned int>(y)},
                       phase);
    }
    return phases;
}

std::size_t measureMaxSamePhaseTileRun(
    const GlobalClockProblem &problem,
    const GlobalClockSolution &solution)
{
    std::map<std::string, const RouteOccurrenceSpec *> occurrenceById;
    for (const auto &occurrence : problem.occurrences)
    {
        occurrenceById.emplace(occurrence.id, &occurrence);
    }
    std::size_t globalMaximum = 0;
    for (const auto &route : problem.routes)
    {
        std::size_t current = 0;
        int previous = 0;
        bool hasPrevious = false;
        for (const auto &occurrenceId : route.occurrences)
        {
            const auto occurrence = occurrenceById.find(occurrenceId);
            if (occurrence == occurrenceById.end())
            {
                throw std::runtime_error(
                    "tile route references an unknown occurrence");
            }
            const int phase = solution.clockResourcePhase.at(
                occurrence->second->clockResource);
            current = hasPrevious && phase == previous ? current + 1 : 1;
            globalMaximum = std::max(globalMaximum, current);
            previous = phase;
            hasPrevious = true;
        }
    }
    return globalMaximum;
}

MappedPhysicalLayout buildMappedPhysicalLayout(
    Parse &parse,
    const std::map<int, position> &nodePositions,
    const RouteMap &routes,
    const PhysicalNetlist &netlist)
{
    MappedPhysicalLayout result;
    NodeLinkMap nodeLinks;
    for (const auto &node : nodePositions)
    {
        nodeLinks.try_emplace(
            std::make_pair(node.second, parse.getNodeType(node.first)),
            std::make_pair(std::vector<position>{}, std::vector<position>{}));
    }

    std::vector<std::vector<position>> routePaths;
    std::vector<unsigned int> routeIterationDistances;
    for (const auto &route : routes)
    {
        if (route.second.size() < 2)
        {
            throw std::runtime_error("P&R emitted a route shorter than two cells");
        }
        const int source = static_cast<int>(route.first.first);
        const int sink = static_cast<int>(route.first.second);
        const position sourcePosition = nodePositions.at(source);
        const position sinkPosition = nodePositions.at(sink);
        nodeLinks[{sourcePosition, parse.getNodeType(source)}].second.push_back(
            route.second[1]);
        nodeLinks[{sinkPosition, parse.getNodeType(sink)}].first.push_back(
            route.second[route.second.size() - 2]);
        routePaths.push_back(route.second);
        result.edges.push_back({source, sink});
        const int iterationDistance = netlist.distance.at({source, sink});
        if (iterationDistance < 0)
        {
            throw std::runtime_error("negative iteration distance in physical netlist");
        }
        routeIterationDistances.push_back(
            static_cast<unsigned int>(iterationDistance));
    }
    for (auto &node : nodeLinks)
    {
        for (auto *ports : {&node.second.first, &node.second.second})
        {
            std::sort(ports->begin(), ports->end());
            ports->erase(std::unique(ports->begin(), ports->end()), ports->end());
        }
    }

    Mapping mapping;
    mapping.node_mapping(nodeLinks, fcngraph::MappingMode::Sequential);
    const auto routeCells = mapping.mapping_line(
        routePaths,
        fcngraph::MappingMode::Sequential,
        routeIterationDistances);
    std::string error;
    if (!mapping.validate_crossovers(&error))
    {
        throw std::runtime_error("QCA Mapping DRC failed: " + error);
    }
    result.coarseRoutes = routePaths;
    result.orderedRoutes = mapping.orderedLayerAwarePhysicalRoutes(routePaths);
    result.routeCells = routeCells;
    result.crossoverCells = mapping.crossline_list;
    result.nodeCells = mapping.nodecell_list;
    result.cellSites = mapping.physicalCellSites(routePaths);
    for (const auto &route : result.crossoverCells)
    {
        result.crossoverSegments += route.second.size();
    }
    for (const auto &type : mapping.nodecell_list)
    {
        result.uniqueCells.insert(type.second.begin(), type.second.end());
    }
    for (const auto &route : routeCells)
    {
        for (const auto &segment : route.second)
        {
            result.uniqueCells.insert(segment.begin(), segment.end());
        }
    }
    for (const auto &route : mapping.crossline_list)
    {
        for (const auto &segment : route.second)
        {
            result.uniqueCells.insert(segment.begin(), segment.end());
        }
    }
    for (const PhysicalCellSite &site : result.cellSites)
    {
        result.uniqueCells.insert(site.xy);
    }
    return result;
}

std::size_t validateAndCountMappedCells(
    Parse &parse,
    const std::map<int, position> &nodePositions,
    const RouteMap &routes,
    const PhysicalNetlist &netlist,
    std::size_t *crossoverSegments)
{
    const MappedPhysicalLayout mapped = buildMappedPhysicalLayout(
        parse, nodePositions, routes, netlist);
    if (crossoverSegments != nullptr)
    {
        *crossoverSegments = mapped.crossoverSegments;
    }
    return mapped.uniqueCells.size();
}

std::size_t validateAndCountMappedCells(Parse &parse,
                                        const CircuitGraph &graph,
                                        const PhysicalNetlist &netlist,
                                        std::size_t *crossoverSegments)
{
    return validateAndCountMappedCells(
        parse,
        graph.nodeIndex_pos,
        graph.routes,
        netlist,
        crossoverSegments);
}

enum class RouteOrientation
{
    Horizontal,
    Vertical,
    Bend
};

RouteOrientation routeOrientationAt(
    const std::vector<position> &path, std::size_t index)
{
    const position before = path[index - 1];
    const position after = path[index + 1];
    if (before.second == after.second)
    {
        return RouteOrientation::Horizontal;
    }
    if (before.first == after.first)
    {
        return RouteOrientation::Vertical;
    }
    return RouteOrientation::Bend;
}

bool validateCyclicGeometry(
    Parse &parse,
    const PhysicalNetlist &netlist,
    const std::map<int, position> &nodePositions,
    const RouteMap &routes,
    std::string *error)
{
    const auto fail = [&](const std::string &message) {
        if (error != nullptr)
        {
            *error = message;
        }
        return false;
    };

    if (routes.size() != netlist.distance.size())
    {
        return fail("route set does not match the physical netlist size");
    }
    for (const auto &net : netlist.distance)
    {
        if (net.first.first < 0 || net.first.second < 0)
        {
            return fail("physical netlist contains a negative node index");
        }
        const RouteKey key{
            static_cast<unsigned int>(net.first.first),
            static_cast<unsigned int>(net.first.second)};
        if (routes.count(key) == 0)
        {
            return fail("physical route is missing from the candidate");
        }
    }

    std::set<position> nodeSites;
    for (const auto &node : nodePositions)
    {
        if (!nodeSites.insert(node.second).second)
        {
            return fail("two gates occupy the same coordinate");
        }
    }

    std::map<int, std::vector<const std::vector<position> *>> sourcePaths;
    std::set<std::pair<int, position>> sinkPorts;
    std::map<position,
             std::map<int, std::set<RouteOrientation>>> interiorUse;
    std::map<std::pair<position, position>, std::set<int>> segmentSources;
    for (const auto &route : routes)
    {
        const Edge edge{static_cast<int>(route.first.first),
                        static_cast<int>(route.first.second)};
        if (netlist.distance.count(edge) == 0)
        {
            return fail("candidate contains a route outside the physical netlist");
        }
        const auto source = nodePositions.find(edge.first);
        const auto sink = nodePositions.find(edge.second);
        if (source == nodePositions.end() || sink == nodePositions.end())
        {
            return fail("route endpoint gate is absent from placement");
        }
        const auto &path = route.second;
        if (path.size() < 2)
        {
            return fail("route contains fewer than two coordinates");
        }
        if (path.front() != source->second || path.back() != sink->second)
        {
            return fail("route endpoints do not match gate coordinates");
        }
        std::set<position> pathSites;
        for (std::size_t index = 0; index < path.size(); ++index)
        {
            if (!pathSites.insert(path[index]).second)
            {
                return fail("route self-intersects or repeats a coordinate");
            }
            if (index != 0 && manhattan(path[index - 1], path[index]) != 1)
            {
                return fail("route is not unit Manhattan connected");
            }
            if (index != 0)
            {
                const std::pair<position, position> segment =
                    path[index - 1] < path[index]
                        ? std::make_pair(path[index - 1], path[index])
                        : std::make_pair(path[index], path[index - 1]);
                segmentSources[segment].insert(edge.first);
            }
            if (index != 0 && index + 1 != path.size() &&
                nodeSites.count(path[index]) != 0)
            {
                return fail("route passes through an interior gate");
            }
            if (index != 0 && index + 1 != path.size())
            {
                interiorUse[path[index]][edge.first].insert(
                    routeOrientationAt(path, index));
            }
        }
        if (!sinkPorts.emplace(edge.second, path[path.size() - 2]).second)
        {
            return fail("two nets enter a sink through the same port coordinate");
        }
        sourcePaths[edge.first].push_back(&path);
    }

    for (const auto &segment : segmentSources)
    {
        if (segment.second.size() > 1)
        {
            return fail("different source trees share a grid segment");
        }
    }

    // A routed fanout may share only its source-rooted prefix.  Once two
    // branches split, allowing them to meet again would short distinct sink
    // paths and invalidate the physical fanout tree.
    for (const auto &fanout : sourcePaths)
    {
        const auto &paths = fanout.second;
        for (std::size_t leftIndex = 0; leftIndex < paths.size(); ++leftIndex)
        {
            for (std::size_t rightIndex = leftIndex + 1;
                 rightIndex < paths.size(); ++rightIndex)
            {
                const auto &left = *paths[leftIndex];
                const auto &right = *paths[rightIndex];
                std::size_t commonPrefix = 0;
                while (commonPrefix < left.size() &&
                       commonPrefix < right.size() &&
                       left[commonPrefix] == right[commonPrefix])
                {
                    ++commonPrefix;
                }
                if (commonPrefix == 0)
                {
                    return fail("same-source routes do not share their source");
                }
                std::set<position> leftTail(
                    left.begin() + static_cast<std::ptrdiff_t>(commonPrefix),
                    left.end());
                for (auto it = right.begin() +
                               static_cast<std::ptrdiff_t>(commonPrefix);
                     it != right.end(); ++it)
                {
                    if (leftTail.count(*it) != 0)
                    {
                        return fail("fanout branches split and reconverge");
                    }
                }
            }
        }
    }

    std::map<std::pair<int, int>, std::size_t> sourcePairCrossings;
    for (const auto &site : interiorUse)
    {
        if (site.second.size() <= 1)
        {
            continue;
        }
        if (site.second.size() != 2)
        {
            return fail("more than two source trees share one wire coordinate");
        }
        auto first = site.second.begin();
        auto second = std::next(first);
        if (first->second.size() != 1 || second->second.size() != 1)
        {
            return fail("a fanout junction overlaps another source tree");
        }
        const RouteOrientation firstOrientation = *first->second.begin();
        const RouteOrientation secondOrientation = *second->second.begin();
        if (firstOrientation == RouteOrientation::Bend ||
            secondOrientation == RouteOrientation::Bend ||
            firstOrientation == secondOrientation)
        {
            return fail("inter-source overlap is not a straight H/V crossover");
        }
        const std::pair<int, int> sourcePair{
            std::min(first->first, second->first),
            std::max(first->first, second->first)};
        if (++sourcePairCrossings[sourcePair] > 1)
        {
            return fail("two source trees cross at more than one coordinate");
        }
    }

    try
    {
        (void)validateAndCountMappedCells(
            parse, nodePositions, routes, netlist);
    }
    catch (const std::exception &mappingError)
    {
        return fail(mappingError.what());
    }
    if (error != nullptr)
    {
        error->clear();
    }
    return true;
}

std::string geometrySignature(const GeometryCandidate &candidate)
{
    std::ostringstream signature;
    for (const auto &node : candidate.nodePositions)
    {
        signature << 'N' << node.first << '@' << node.second.first << ','
                  << node.second.second << ';';
    }
    for (const auto &route : candidate.routes)
    {
        signature << 'R' << route.first.first << '>' << route.first.second
                  << ':';
        for (const position &site : route.second)
        {
            signature << site.first << ',' << site.second << '/';
        }
        signature << ';';
    }
    return signature.str();
}

struct GeometryBounds
{
    unsigned int minX = 0;
    unsigned int maxX = 0;
    unsigned int minY = 0;
    unsigned int maxY = 0;
    bool initialized = false;
};

GeometryBounds geometryBounds(const GeometryCandidate &candidate)
{
    GeometryBounds bounds;
    const auto observe = [&](const position &site) {
        if (!bounds.initialized)
        {
            bounds.minX = bounds.maxX = site.first;
            bounds.minY = bounds.maxY = site.second;
            bounds.initialized = true;
            return;
        }
        bounds.minX = std::min(bounds.minX, site.first);
        bounds.maxX = std::max(bounds.maxX, site.first);
        bounds.minY = std::min(bounds.minY, site.second);
        bounds.maxY = std::max(bounds.maxY, site.second);
    };
    for (const auto &node : candidate.nodePositions)
    {
        observe(node.second);
    }
    for (const auto &route : candidate.routes)
    {
        for (const position &site : route.second)
        {
            observe(site);
        }
    }
    return bounds;
}

position contractCoordinate(position site, bool xAxis, unsigned int cut)
{
    if (xAxis && site.first > cut)
    {
        --site.first;
    }
    if (!xAxis && site.second > cut)
    {
        --site.second;
    }
    return site;
}

GeometryCandidate contractSeam(
    const GeometryCandidate &candidate, bool xAxis, unsigned int cut)
{
    GeometryCandidate contracted;
    for (const auto &node : candidate.nodePositions)
    {
        contracted.nodePositions.emplace(
            node.first, contractCoordinate(node.second, xAxis, cut));
    }
    for (const auto &route : candidate.routes)
    {
        std::vector<position> path;
        path.reserve(route.second.size());
        for (const position &site : route.second)
        {
            const position transformed =
                contractCoordinate(site, xAxis, cut);
            if (path.empty() || path.back() != transformed)
            {
                path.push_back(transformed);
            }
        }
        contracted.routes.emplace(route.first, std::move(path));
    }
    contracted.metrics = geometryMetrics(
        contracted.nodePositions, contracted.routes);
    return contracted;
}

std::unordered_map<position, fcngraph::GridCell,
                   fcngraph::PositionHash>
rebuildGridSnapshot(const GeometryCandidate &candidate)
{
    std::unordered_map<position, fcngraph::GridCell,
                       fcngraph::PositionHash> snapshot;
    std::set<position> nodeSites;
    for (const auto &node : candidate.nodePositions)
    {
        nodeSites.insert(node.second);
        snapshot[node.second].put_node();
    }
    for (const auto &route : candidate.routes)
    {
        for (const position &site : route.second)
        {
            if (nodeSites.count(site) == 0 &&
                snapshot[site].get_current_weight() == 0)
            {
                snapshot[site].put_wire();
            }
        }
    }
    return snapshot;
}

struct SeamCompactionResult
{
    GeometryCandidate best;
    std::vector<GeometryCandidate> improvingCandidates;
    SeamCompactionStats stats;
};

SeamCompactionResult exactSeamCompact(
    Parse &parse,
    const PhysicalNetlist &netlist,
    const GeometryCandidate &seed,
    std::size_t maxStates)
{
    SeamCompactionResult result;
    result.best = seed;
    std::string validationError;
    if (!validateCyclicGeometry(
            parse, netlist, seed.nodePositions, seed.routes,
            &validationError))
    {
        return result;
    }

    GeometryCandidate root = seed;
    root.gridCells.clear();
    root.seamCompaction = {};
    std::deque<GeometryCandidate> frontier;
    frontier.push_back(std::move(root));
    std::set<std::string> seen;
    seen.insert(geometrySignature(seed));

    while (!frontier.empty() && result.stats.statesExplored < maxStates)
    {
        GeometryCandidate current = std::move(frontier.front());
        frontier.pop_front();
        ++result.stats.statesExplored;
        const GeometryBounds bounds = geometryBounds(current);
        if (!bounds.initialized)
        {
            continue;
        }

        const auto exploreAxis = [&](bool xAxis,
                                     unsigned int minimum,
                                     unsigned int maximum) {
            for (unsigned int cut = minimum; cut < maximum; ++cut)
            {
                GeometryCandidate child = contractSeam(current, xAxis, cut);
                const std::string signature = geometrySignature(child);
                if (!seen.insert(signature).second)
                {
                    continue;
                }
                std::string childError;
                if (!validateCyclicGeometry(
                        parse, netlist, child.nodePositions, child.routes,
                        &childError))
                {
                    continue;
                }
                ++result.stats.legalMoves;
                if (betterGeometry(child.metrics, result.best.metrics))
                {
                    result.best = child;
                    result.improvingCandidates.push_back(child);
                }
                frontier.push_back(std::move(child));
            }
        };
        exploreAxis(true, bounds.minX, bounds.maxX);
        exploreAxis(false, bounds.minY, bounds.maxY);
    }

    result.stats.optimalityProven = frontier.empty();
    result.stats.columnsRemoved =
        seed.metrics.width - result.best.metrics.width;
    result.stats.rowsRemoved =
        seed.metrics.height - result.best.metrics.height;
    result.best.gridCells = rebuildGridSnapshot(result.best);
    result.best.seamCompaction = result.stats;
    return result;
}

void augmentWithSeamCompaction(
    Parse &parse,
    const PhysicalNetlist &netlist,
    std::size_t maxStates,
    std::size_t seedLimit,
    std::vector<GeometryCandidate> *candidates,
    GeometrySelectionStats *selectionStats)
{
    if (candidates == nullptr || selectionStats == nullptr)
    {
        return;
    }
    selectionStats->rawDistinctCandidates = candidates->size();
    selectionStats->compactionMaxStates = maxStates;
    if (candidates->empty())
    {
        selectionStats->allCompactionOptimalityProven = false;
        return;
    }

    const std::vector<GeometryCandidate> rawCandidates = *candidates;
    std::vector<std::size_t> seedIndices;
    seedIndices.reserve(rawCandidates.size());
    for (std::size_t index = 0; index < rawCandidates.size(); ++index)
    {
        seedIndices.push_back(index);
    }
    std::stable_sort(
        seedIndices.begin(), seedIndices.end(),
        [&](std::size_t left, std::size_t right) {
            if (betterGeometry(rawCandidates[left].metrics,
                               rawCandidates[right].metrics))
            {
                return true;
            }
            if (betterGeometry(rawCandidates[right].metrics,
                               rawCandidates[left].metrics))
            {
                return false;
            }
            return std::tie(rawCandidates[left].nodePositions,
                            rawCandidates[left].routes) <
                   std::tie(rawCandidates[right].nodePositions,
                            rawCandidates[right].routes);
        });
    if (seedIndices.size() > seedLimit)
    {
        seedIndices.resize(seedLimit);
    }
    selectionStats->compactionSeedCandidates = seedIndices.size();
    selectionStats->allRawCandidatesCompacted =
        seedIndices.size() == rawCandidates.size();

    std::vector<GeometryCandidate> augmented = rawCandidates;
    std::map<std::string, std::size_t> signatureIndex;
    for (std::size_t index = 0; index < augmented.size(); ++index)
    {
        signatureIndex.emplace(geometrySignature(augmented[index]), index);
    }

    for (const std::size_t index : seedIndices)
    {
        SeamCompactionResult compacted = exactSeamCompact(
            parse, netlist, rawCandidates[index], maxStates);
        selectionStats->compactionStatesExplored +=
            compacted.stats.statesExplored;
        selectionStats->compactionLegalMoves += compacted.stats.legalMoves;
        if (compacted.stats.optimalityProven)
        {
            ++selectionStats->compactionProvenCandidates;
        }

        const bool reduced = betterGeometry(
            compacted.best.metrics, rawCandidates[index].metrics);
        if (!reduced)
        {
            augmented[index].seamCompaction = compacted.stats;
            continue;
        }
        ++selectionStats->compactionReducedCandidates;
        const std::string bestSignature = geometrySignature(compacted.best);
        for (GeometryCandidate &improvement : compacted.improvingCandidates)
        {
            SeamCompactionStats candidateStats = compacted.stats;
            candidateStats.columnsRemoved =
                rawCandidates[index].metrics.width - improvement.metrics.width;
            candidateStats.rowsRemoved =
                rawCandidates[index].metrics.height - improvement.metrics.height;
            const std::string signature = geometrySignature(improvement);
            candidateStats.optimalityProven =
                compacted.stats.optimalityProven &&
                signature == bestSignature;
            improvement.seamCompaction = candidateStats;
            improvement.gridCells = rebuildGridSnapshot(improvement);

            const auto existing = signatureIndex.find(signature);
            if (existing == signatureIndex.end())
            {
                const std::size_t newIndex = augmented.size();
                augmented.push_back(std::move(improvement));
                signatureIndex.emplace(signature, newIndex);
            }
            else if (candidateStats.optimalityProven)
            {
                augmented[existing->second].seamCompaction = candidateStats;
            }
        }
    }

    selectionStats->allCompactionOptimalityProven =
        !seedIndices.empty() &&
        selectionStats->compactionProvenCandidates == seedIndices.size();
    *candidates = std::move(augmented);
}

std::string jsonEscape(const std::string &value)
{
    std::ostringstream result;
    for (const char character : value)
    {
        if (character == '\\' || character == '"')
        {
            result << '\\';
        }
        result << character;
    }
    return result.str();
}

void writeClockProblemJson(const std::string &path,
                           const GlobalClockProblem &problem)
{
    if (path.empty())
    {
        return;
    }
    const std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream output(path);
    if (!output)
    {
        throw std::runtime_error("cannot write clock problem: " + path);
    }
    const auto quoted = [](const std::string &text) {
        return "\"" + jsonEscape(text) + "\"";
    };
    const auto writeStringArray = [&](const auto &values) {
        output << '[';
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            output << quoted(values[index]);
        }
        output << ']';
    };
    output << "{\n  \"schema\": \"ifcn.global-clock-problem.v1\",\n"
           << "  \"occurrence_granularity\": \"tile\",\n"
           << "  \"phase_count\": " << problem.phaseCount << ",\n"
           << "  \"max_consecutive_same_phase_occurrences\": "
           << problem.maxConsecutiveSamePhaseCells << ",\n"
           << "  \"ii_candidates\": [";
    for (std::size_t index = 0; index < problem.iiCandidates.size(); ++index)
    {
        if (index != 0)
        {
            output << ',';
        }
        output << problem.iiCandidates[index];
    }
    output << "],\n  \"events\": ";
    writeStringArray(problem.events);
    output << ",\n  \"clock_resources\": [\n";
    for (std::size_t index = 0; index < problem.clockResources.size(); ++index)
    {
        const auto &resource = problem.clockResources[index];
        output << "    {\"id\": " << quoted(resource.id)
               << ", \"sharing\": \""
               << (resource.sharing == ClockResourceSharing::ExclusiveOrAliased
                       ? "exclusive_or_aliased"
                       : "phase_shared_independent_epochs")
               << "\"}" << (index + 1 == problem.clockResources.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"occurrences\": [\n";
    for (std::size_t index = 0; index < problem.occurrences.size(); ++index)
    {
        const auto &occurrence = problem.occurrences[index];
        output << "    {\"id\": " << quoted(occurrence.id)
               << ", \"clock_resource\": " << quoted(occurrence.clockResource)
               << ", \"epoch_variable\": " << quoted(occurrence.epochVariable)
               << "}" << (index + 1 == problem.occurrences.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"routes\": [\n";
    for (std::size_t index = 0; index < problem.routes.size(); ++index)
    {
        const auto &route = problem.routes[index];
        output << "    {\"id\": " << quoted(route.id)
               << ", \"source_event\": " << quoted(route.sourceEvent)
               << ", \"sink_event\": " << quoted(route.sinkEvent)
               << ", \"iteration_distance\": " << route.iterationDistance
               << ", \"occurrences\": ";
        writeStringArray(route.occurrences);
        output << '}' << (index + 1 == problem.routes.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"timing_arcs\": [\n";
    for (std::size_t index = 0; index < problem.timingArcs.size(); ++index)
    {
        const auto &arc = problem.timingArcs[index];
        output << "    {\"id\": " << quoted(arc.id)
               << ", \"source_event\": " << quoted(arc.sourceEvent)
               << ", \"sink_event\": " << quoted(arc.sinkEvent)
               << ", \"iteration_distance\": " << arc.iterationDistance
               << ", \"latency_epochs\": " << arc.latencyEpochs << '}'
               << (index + 1 == problem.timingArcs.size() ? "\n" : ",\n");
    }
    output << "  ],\n  \"anchors\": [\n";
    for (std::size_t index = 0; index < problem.anchors.size(); ++index)
    {
        const auto &anchor = problem.anchors[index];
        output << "    {\"event\": " << quoted(anchor.event)
               << ", \"epoch\": " << anchor.epoch << '}'
               << (index + 1 == problem.anchors.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    if (!output)
    {
        throw std::runtime_error("failed while writing clock problem: " + path);
    }
}

GlobalClockSolution readClockSolution(const std::string &path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("cannot read external phase solution: " + path);
    }
    GlobalClockSolution solution;
    bool header = false;
    bool sat = false;
    bool statusSeen = false;
    bool phaseCountSeen = false;
    bool initiationIntervalSeen = false;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::vector<std::string> fields;
        std::stringstream stream(line);
        std::string field;
        while (std::getline(stream, field, '\t'))
        {
            fields.push_back(field);
        }
        if (!header)
        {
            if (fields.size() != 1 ||
                fields[0] != "ifcn.global-clock-solution.v1")
            {
                throw std::runtime_error("external phase solution has an invalid header");
            }
            header = true;
            continue;
        }
        const auto integer = [&](std::size_t index) -> std::int64_t {
            if (index >= fields.size())
            {
                throw std::runtime_error("missing field in external phase solution at line " +
                                         std::to_string(lineNumber));
            }
            std::size_t consumed = 0;
            const auto value = std::stoll(fields[index], &consumed);
            if (consumed != fields[index].size())
            {
                throw std::runtime_error("invalid integer in external phase solution at line " +
                                         std::to_string(lineNumber));
            }
            return value;
        };
        const auto boundedInteger = [&](std::size_t index) -> int {
            const auto parsed = integer(index);
            if (parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max())
            {
                throw std::runtime_error(
                    "integer is outside the int range in external phase solution at line " +
                    std::to_string(lineNumber));
            }
            return static_cast<int>(parsed);
        };
        if (fields.size() == 2 && fields[0] == "status")
        {
            if (statusSeen || fields[1] != "SAT")
            {
                throw std::runtime_error(
                    "external phase solution must contain exactly one SAT status");
            }
            statusSeen = true;
            sat = true;
        }
        else if (fields.size() == 2 && fields[0] == "phase_count")
        {
            if (phaseCountSeen)
            {
                throw std::runtime_error(
                    "duplicate phase_count in external phase solution");
            }
            phaseCountSeen = true;
            solution.phaseCount = boundedInteger(1);
        }
        else if (fields.size() == 2 && fields[0] == "ii")
        {
            if (initiationIntervalSeen)
            {
                throw std::runtime_error("duplicate ii in external phase solution");
            }
            initiationIntervalSeen = true;
            solution.initiationInterval = boundedInteger(1);
        }
        else if (fields.size() == 3 && fields[0] == "event")
        {
            if (!solution.eventEpoch.emplace(fields[1], integer(2)).second)
            {
                throw std::runtime_error(
                    "duplicate event in external phase solution at line " +
                    std::to_string(lineNumber));
            }
        }
        else if (fields.size() == 3 && fields[0] == "occurrence")
        {
            if (!solution.occurrenceEpoch.emplace(fields[1], integer(2)).second)
            {
                throw std::runtime_error(
                    "duplicate occurrence in external phase solution at line " +
                    std::to_string(lineNumber));
            }
        }
        else if (fields.size() == 3 && fields[0] == "resource")
        {
            if (!solution.clockResourcePhase.emplace(
                    fields[1], boundedInteger(2)).second)
            {
                throw std::runtime_error(
                    "duplicate resource in external phase solution at line " +
                    std::to_string(lineNumber));
            }
        }
        else
        {
            throw std::runtime_error("invalid external phase solution record at line " +
                                     std::to_string(lineNumber));
        }
    }
    if (!header || !statusSeen || !sat || !phaseCountSeen ||
        !initiationIntervalSeen)
    {
        throw std::runtime_error(
            "external phase solution is missing required SAT metadata");
    }
    return solution;
}

void writeLayout(const std::string &path,
                 Parse &parse,
                 const CircuitGraph &graph,
                 const PhysicalNetlist &netlist,
                 const std::map<position, int> &phases,
                 const GlobalClockSolution &solution,
                 std::size_t mappedCells,
                 std::size_t mappedLayerCellRecords,
                 std::size_t crossoverSegments,
                 int maxSamePhaseTiles,
                 std::size_t maxObservedSamePhaseTileRun,
                 const GlobalClockSolveStats &solveStats,
                 const std::string &phaseBackend,
                 double frontendSeconds,
                 double geometrySeconds,
                 double phaseSeconds,
                 double mappingSeconds,
                 double totalSeconds,
                 const GeometrySelectionStats &geometrySelection)
{
    const std::filesystem::path outputPath(path);
    if (!outputPath.parent_path().empty())
    {
        std::filesystem::create_directories(outputPath.parent_path());
    }
    std::ofstream output(path);
    if (!output)
    {
        throw std::runtime_error("cannot write IFCN layout: " + path);
    }
    output << "#circuit name: " << parse.get_moduleName() << "\n"
           << "#mapping mode: sequential\n"
           << "#flow: sampled-state physical-feedback P&R experiment v0\n"
           << "#status: physical feedback retained; state device uncharacterized\n"
           << "#phase count: 4\n"
           << "#phase granularity: tile\n"
           << "#tile phase drc scope: ordered_route_tiles\n"
           << "#max same phase tiles: " << maxSamePhaseTiles << "\n"
           << "#observed max same phase tile run: "
           << maxObservedSamePhaseTileRun << "\n"
           << "#initiation interval epochs: "
           << solution.initiationInterval << "\n"
           << "#mapped unique xy sites: " << mappedCells << "\n"
           << "#mapped qca cells: " << mappedCells << "\n"
           << "#mapped layer cell records: "
           << mappedLayerCellRecords << "\n";
    for (const auto &edge : netlist.feedbackEdges)
    {
        output << "#physical feedback route: "
               << parse.getNodeName(edge.first) << "(t) -> "
               << parse.getNodeName(edge.second)
               << "(t+1), distance=1\n";
    }
    output << "\n#nodes info\n"
           << "### nodeIndex, nodeName, nodeType, nodePosition ###\n";
    for (const auto &node : graph.nodeIndex_pos)
    {
        output << node.first << ", " << parse.getNodeName(node.first) << ", "
               << parse.getNodeType(node.first) << ", ("
               << node.second.first << ',' << node.second.second << ");\n";
    }
    output << "#nodes info\n\n#paths info\n"
           << "### {node1, node2} : path ###\n";
    for (const auto &route : graph.routes)
    {
        const Edge edge{static_cast<int>(route.first.first),
                        static_cast<int>(route.first.second)};
        output << "#iteration_distance=" << netlist.distance.at(edge) << "\n"
               << '(' << edge.first << ',' << edge.second << "): ";
        for (std::size_t index = 0; index < route.second.size(); ++index)
        {
            if (index != 0)
            {
                output << ',';
            }
            output << '(' << route.second[index].first << ','
                   << route.second[index].second << ')';
        }
        output << ";\n";
    }
    output << "#paths info\n\n#phase map\n"
           << "### authoritative clock tile (x,y) : zero-based phase ###\n"
           << "### every mapped QCA cell in this tile inherits the same phase ###\n";
    for (const auto &phase : phases)
    {
        output << '(' << phase.first.first << ',' << phase.first.second
               << "): " << phase.second << ";\n";
    }
    output << "#phase map\n";
    if (!output)
    {
        throw std::runtime_error("failed while writing IFCN layout: " + path);
    }

    std::ofstream report(path + ".json");
    if (!report)
    {
        throw std::runtime_error("cannot write cyclic P&R report");
    }
    std::set<position> occupiedSites;
    std::size_t routeSteps = 0;
    for (const auto &node : graph.nodeIndex_pos)
    {
        occupiedSites.insert(node.second);
    }
    for (const auto &route : graph.routes)
    {
        occupiedSites.insert(route.second.begin(), route.second.end());
        routeSteps += route.second.empty() ? 0 : route.second.size() - 1;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    if (!occupiedSites.empty())
    {
        auto minX = occupiedSites.begin()->first;
        auto maxX = minX;
        auto minY = occupiedSites.begin()->second;
        auto maxY = minY;
        for (const auto &site : occupiedSites)
        {
            minX = std::min(minX, site.first);
            maxX = std::max(maxX, site.first);
            minY = std::min(minY, site.second);
            maxY = std::max(maxY, site.second);
        }
        width = maxX - minX + 1;
        height = maxY - minY + 1;
    }
    report << std::fixed << std::setprecision(9)
           << "{\n"
           << "  \"schema\": \"ifcn.paper-cyclic-pnr-report.v0\",\n"
           << "  \"module\": \"" << jsonEscape(parse.get_moduleName())
           << "\",\n"
           << "  \"mapping_mode\": \"sequential\",\n"
           << "  \"phase_granularity\": \"tile\",\n"
           << "  \"tile_phase_drc_scope\": \"ordered_route_tiles\",\n"
           << "  \"tile_clock_resources\": "
           << solution.clockResourcePhase.size() << ",\n"
           << "  \"max_same_phase_tiles\": " << maxSamePhaseTiles << ",\n"
           << "  \"max_observed_same_phase_tile_run\": "
           << maxObservedSamePhaseTileRun << ",\n"
           << "  \"tile_phase_drc\": true,\n"
           << "  \"status\": \"success_physical_feedback_uncharacterized_state\",\n"
           << "  \"initiation_interval\": "
           << solution.initiationInterval << ",\n"
           << "  \"nodes\": " << graph.nodeIndex_pos.size() << ",\n"
           << "  \"routes\": " << graph.routes.size() << ",\n"
           << "  \"route_steps\": " << routeSteps << ",\n"
           << "  \"unique_clock_sites\": " << occupiedSites.size() << ",\n"
           << "  \"bbox_width\": " << width << ",\n"
           << "  \"bbox_height\": " << height << ",\n"
           << "  \"bbox_area\": "
           << static_cast<std::uint64_t>(width) * height << ",\n"
           << "  \"feedback_routes\": "
           << netlist.feedbackEdges.size() << ",\n"
           << "  \"q_pseudo_nodes_removed\": "
           << netlist.removedQNodes.size() << ",\n"
           << "  \"directed_cycle_present\": true,\n"
           << "  \"mapped_unique_xy_sites\": " << mappedCells << ",\n"
           << "  \"mapped_qca_cells\": " << mappedCells << ",\n"
           << "  \"mapped_layer_cell_records\": "
           << mappedLayerCellRecords << ",\n"
           << "  \"crossover_segments\": " << crossoverSegments << ",\n"
           << "  \"mapping_drc\": true,\n"
           << "  \"geometry_optimization\": {\"objective\": "
              "\"bbox_area_route_steps_max_dimension_perimeter\", "
              "\"placement_candidates\": "
           << geometrySelection.placementCandidates
           << ", \"q_filtered_placement_candidates\": "
           << geometrySelection.qFilteredPlacementCandidates
           << ", \"legacy_placement_fallback_candidates\": "
           << geometrySelection.legacyPlacementFallbackCandidates
           << ", \"search_cost_candidates\": "
           << geometrySelection.searchCostCandidates
           << ", \"routing_window_templates\": "
           << geometrySelection.routingWindowTemplates
           << ", \"bounded_routing_attempts\": "
           << geometrySelection.boundedRoutingAttempts
           << ", \"unbounded_routing_attempts\": "
           << geometrySelection.unboundedRoutingAttempts
           << ", \"bounded_routed_candidates\": "
           << geometrySelection.boundedRoutedCandidates
           << ", \"unbounded_routed_candidates\": "
           << geometrySelection.unboundedRoutedCandidates
           << ", \"routed_candidates\": "
           << geometrySelection.routedCandidates
           << ", \"drc_valid_candidates\": "
           << geometrySelection.drcValidCandidates
           << ", \"raw_distinct_candidates\": "
           << geometrySelection.rawDistinctCandidates
           << ", \"compaction_seed_candidates\": "
           << geometrySelection.compactionSeedCandidates
           << ", \"distinct_candidates\": "
           << geometrySelection.distinctCandidates
           << ", \"selected_rank\": "
           << geometrySelection.selectedRank
           << ", \"compaction_model\": "
              "\"monotone_unit_cut_contraction\""
           << ", \"compaction_seed_policy\": "
              "\"top_lexicographic_geometry\""
           << ", \"all_raw_candidates_compacted\": "
           << (geometrySelection.allRawCandidatesCompacted
                   ? "true" : "false")
           << ", \"compaction_max_states_per_candidate\": "
           << geometrySelection.compactionMaxStates
           << ", \"compaction_states_explored\": "
           << geometrySelection.compactionStatesExplored
           << ", \"compaction_legal_moves\": "
           << geometrySelection.compactionLegalMoves
           << ", \"compaction_reduced_candidates\": "
           << geometrySelection.compactionReducedCandidates
           << ", \"compaction_proven_candidates\": "
           << geometrySelection.compactionProvenCandidates
           << ", \"all_compaction_optimality_proven\": "
           << (geometrySelection.allCompactionOptimalityProven
                   ? "true" : "false")
           << ", \"selected_seam_optimality_proven\": "
           << (geometrySelection.selectedCompaction.optimalityProven
                   ? "true" : "false")
           << ", \"selected_rows_removed\": "
           << geometrySelection.selectedCompaction.rowsRemoved
           << ", \"selected_columns_removed\": "
           << geometrySelection.selectedCompaction.columnsRemoved
           << ", \"selected_compaction_states\": "
           << geometrySelection.selectedCompaction.statesExplored
           << ", \"optimality_scope\": "
           << (geometrySelection.allRawCandidatesCompacted
                   ? "\"enumerated_routes_plus_monotone_seam_contractions\""
                   : "\"top_ranked_enumerated_routes_plus_monotone_seam_contractions\"")
           << ", \"global_pnr_optimality_claimed\": false},\n"
           << "  \"phase_backend\": \"" << jsonEscape(phaseBackend)
           << "\",\n"
           << "  \"phase_solver\": {\"dfs_nodes\": "
           << solveStats.dfsNodes << ", \"decisions\": "
           << solveStats.decisions << ", \"forced_edges\": "
           << solveStats.forcedEdges << ", \"conflicts\": "
           << solveStats.conflicts << ", \"ii_candidates_tried\": "
           << solveStats.iiCandidatesTried << "},\n"
           << "  \"runtime_seconds\": {\"frontend\": " << frontendSeconds
           << ", \"geometry\": " << geometrySeconds
           << ", \"phase\": " << phaseSeconds
           << ", \"mapping\": " << mappingSeconds
           << ", \"total_before_io\": " << totalSeconds << "},\n"
           << "  \"physical_state_signoff\": \"not_characterized\"\n"
           << "}\n";
}

const char *statusName(GlobalClockSolveStatus status)
{
    switch (status)
    {
    case GlobalClockSolveStatus::Sat:
        return "SAT";
    case GlobalClockSolveStatus::Unsat:
        return "UNSAT";
    case GlobalClockSolveStatus::Limit:
        return "LIMIT";
    case GlobalClockSolveStatus::InvalidInput:
        return "INVALID_INPUT";
    }
    return "UNKNOWN";
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const auto totalStart = SteadyClock::now();
        const CommandLine command = parseCommandLine(argc, argv);
        Parse parse;
        parse.parseVerilog(command.input);
        if (parse.getm_numVertices() == 0)
        {
            throw std::runtime_error("legacy DAG parser produced an empty graph");
        }
        parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
        parse.caculateSameLayerNodeRoutePair();
        const PhysicalNetlist netlist = makePhysicalNetlist(parse, command.states);
        const auto frontendStop = SteadyClock::now();

        GridChessboard board;
        Astar router(board, false, command.routeSearchCost);
        router.setAllowInterSourceWireOverlap(false);
        CircuitGraph graph(parse, command.input, board, router);
        // Q pseudo-inputs exist only to cut the schedule DAG.  Excluding them
        // before layer centering prevents removed state ports from reserving
        // ghost columns in the physical placement.
        const auto physicalLayers = physicalPlacementLayers(
            parse, netlist.removedQNodes);
        const auto physicalLayerOrders = feedbackAwareLayerOrders(
            physicalLayers, netlist);
        const auto legacyLayers = parseLayers(parse);

        bool routed = false;
        GeometryMetrics bestGeometry;
        GeometrySelectionStats geometrySelection;
        std::vector<GeometryCandidate> geometryCandidates;
        std::vector<double> routeSearchCosts{command.routeSearchCost};
        if (command.routeSearchCost < 120.0)
        {
            routeSearchCosts.push_back(120.0);
        }
        geometrySelection.searchCostCandidates = routeSearchCosts.size();
        // Unit-spacing seeds are legal on the coarse grid and expose compact
        // topologies that cannot be reached by projecting an already routed
        // even-spacing layout.  Keep the previous isotropic ladder as the
        // congestion/routability fallback.
        std::vector<std::pair<unsigned int, unsigned int>> legacySpacings;
        for (const unsigned int spacing :
             {command.spacing, command.spacing + 2, command.spacing + 4,
              command.spacing + 6})
        {
            legacySpacings.emplace_back(spacing, spacing);
        }
        std::vector<std::pair<unsigned int, unsigned int>> compactSpacings;
        const auto addCompactSpacing =
            [&](const std::pair<unsigned int, unsigned int> &spacing) {
                if (std::find(compactSpacings.begin(), compactSpacings.end(),
                              spacing) == compactSpacings.end())
                {
                    compactSpacings.push_back(spacing);
                }
            };
        addCompactSpacing({command.spacing, command.spacing});
        std::vector<std::pair<unsigned int, unsigned int>>
            compactSeedSpacings;
        const auto addCompactSeedSpacing =
            [&](const std::pair<unsigned int, unsigned int> &spacing) {
                if (std::find(compactSeedSpacings.begin(),
                              compactSeedSpacings.end(), spacing) ==
                    compactSeedSpacings.end())
                {
                    compactSeedSpacings.push_back(spacing);
                }
            };
        addCompactSeedSpacing({1, 1});
        addCompactSeedSpacing({1, command.spacing});
        addCompactSeedSpacing({command.spacing, 1});
        addCompactSeedSpacing({command.spacing, command.spacing});
        const std::vector<double> compactSearchCosts{
            command.routeSearchCost};

        struct PlacementFamily
        {
            const std::vector<std::vector<int>> *layers = nullptr;
            const std::vector<std::pair<unsigned int, unsigned int>> *spacings =
                nullptr;
            const std::vector<double> *searchCosts = nullptr;
            bool qFiltered = false;
            bool exploreCompactWindows = false;
        };
        std::vector<PlacementFamily> placementFamilies;
        placementFamilies.reserve(physicalLayerOrders.size() + 2);
        for (std::size_t index = 0;
             index < physicalLayerOrders.size(); ++index)
        {
            if (index == 0)
            {
                // Tight windows are most useful for unit/anisotropic seeds.
                // Running the complete window ladder again for every loose
                // spacing/search-cost pair only duplicates seam candidates
                // and becomes expensive on difficult routing instances.
                placementFamilies.push_back(PlacementFamily{
                    &physicalLayerOrders[index], &compactSeedSpacings,
                    &compactSearchCosts, true, true});
                placementFamilies.push_back(PlacementFamily{
                    &physicalLayerOrders[index], &legacySpacings,
                    &routeSearchCosts, true, false});
            }
            else
            {
                placementFamilies.push_back(PlacementFamily{
                    &physicalLayerOrders[index], &compactSpacings,
                    &compactSearchCosts, true, true});
            }
        }
        // Retain the old Q-inclusive centering and unbounded routing as a
        // regression-safe fallback.  Filtering ghost slots changes layer
        // offsets and is not guaranteed to dominate every topology.
        placementFamilies.push_back(PlacementFamily{
            &legacyLayers, &legacySpacings, &routeSearchCosts, false, false});

        for (const PlacementFamily &family : placementFamilies)
        {
            for (const auto &spacing : *family.spacings)
            {
                for (const double searchCost : *family.searchCosts)
                {
                    ++geometrySelection.placementCandidates;
                    if (family.qFiltered)
                    {
                        ++geometrySelection.qFilteredPlacementCandidates;
                    }
                    else
                    {
                        ++geometrySelection.legacyPlacementFallbackCandidates;
                    }
                    router.setMaxSearchCost(searchCost);
                    graph.sortNodesByFixedLayerOrder(
                        *family.layers, spacing.first, spacing.second);
                    for (const int q : netlist.removedQNodes)
                    {
                        graph.nodeIndex_pos.erase(q);
                    }
                    if (!hasDirectedCycle(netlist, graph.nodeIndex_pos))
                    {
                        throw std::runtime_error(
                            "physical feedback transformation did not create a directed cycle");
                    }
                    GeometryMetrics candidateGeometry;
                    if (routePhysicalNetlist(
                            graph, parse, board, router, netlist,
                            family.exploreCompactWindows,
                            &candidateGeometry, &geometrySelection,
                            &geometryCandidates))
                    {
                        if (!routed || betterGeometry(
                                candidateGeometry, bestGeometry))
                        {
                            routed = true;
                            bestGeometry = candidateGeometry;
                        }
                    }
                }
            }
        }
        if (!routed)
        {
            throw std::runtime_error("cyclic placement/routing failed");
        }
        augmentWithSeamCompaction(
            parse, netlist, command.compactionMaxStates,
            command.compactionSeedLimit,
            &geometryCandidates, &geometrySelection);
        std::sort(geometryCandidates.begin(), geometryCandidates.end(),
                  [](const GeometryCandidate &left,
                     const GeometryCandidate &right) {
                      if (betterGeometry(left.metrics, right.metrics))
                      {
                          return true;
                      }
                      if (betterGeometry(right.metrics, left.metrics))
                      {
                          return false;
                      }
                      return std::tie(left.nodePositions, left.routes) <
                             std::tie(right.nodePositions, right.routes);
                  });
        geometrySelection.distinctCandidates = geometryCandidates.size();
        geometrySelection.selectedRank = command.geometryRank;
        if (command.geometryRank >= geometryCandidates.size())
        {
            throw std::runtime_error(
                "--geometry-rank " + std::to_string(command.geometryRank) +
                " is unavailable; only " +
                std::to_string(geometryCandidates.size()) +
                " distinct DRC-valid candidates were found");
        }
        const GeometryCandidate &selected = geometryCandidates[command.geometryRank];
        bestGeometry = selected.metrics;
        geometrySelection.selectedCompaction = selected.seamCompaction;
        graph.nodeIndex_pos = selected.nodePositions;
        graph.routes = selected.routes;
        // Routing mutates the shared chessboard for every explored candidate.
        // Restore the exact board snapshot belonging to the selected geometry;
        // otherwise phase assignment and native TikZ export observe the last
        // attempted route instead of the ranked winner.
        board.gridMap = selected.gridCells;
        router.reset();

        const auto geometryStop = SteadyClock::now();
        const MappedPhysicalLayout mappedLayout = buildMappedPhysicalLayout(
            parse, graph.nodeIndex_pos, graph.routes, netlist);
        const GlobalClockProblem problem = buildTileClockProblem(
            parse, graph, netlist, command.iiCandidates,
            command.maxSamePhaseTiles, command.maxDfsNodes);
        const auto mappingPreparationStop = SteadyClock::now();
        writeClockProblemJson(command.clockProblemOutput, problem);
        if (command.deferPhase)
        {
            if (command.clockProblemOutput.empty())
            {
                throw std::runtime_error("--defer-phase requires --clock-problem-out");
            }
            std::cout << "paper_cyclic_pnr=geometry_ready"
                      << " module=" << parse.get_moduleName()
                      << " nodes=" << graph.nodeIndex_pos.size()
                      << " routes=" << graph.routes.size()
                      << " feedback_routes=" << netlist.feedbackEdges.size()
                      << " geometry_rank=" << command.geometryRank
                      << " geometry_candidates=" << geometryCandidates.size()
                      << " bbox=" << bestGeometry.width << 'x'
                      << bestGeometry.height
                      << " seam_rows_removed="
                      << geometrySelection.selectedCompaction.rowsRemoved
                      << " seam_columns_removed="
                      << geometrySelection.selectedCompaction.columnsRemoved
                      << " seam_optimality_proven="
                      << (geometrySelection.selectedCompaction.optimalityProven
                              ? "true" : "false")
                      << " clock_problem=" << command.clockProblemOutput << '\n';
            return 0;
        }
        GlobalClockSolveStats solveStats;
        std::string phaseBackend;
        GlobalClockSolution solution;
        if (!command.phaseSolutionInput.empty())
        {
            solution = readClockSolution(command.phaseSolutionInput);
            phaseBackend = "external_z3";
        }
        else
        {
            const GlobalClockSolveResult solve = GlobalPhaseSolver(problem).solve();
            solveStats = solve.stats;
            if (solve.status != GlobalClockSolveStatus::Sat ||
                !solve.solution.has_value())
            {
                throw std::runtime_error(
                    std::string("global cyclic phase/epoch solve ") +
                    statusName(solve.status) + ": " + solve.message +
                    " (dfs_nodes=" + std::to_string(solve.stats.dfsNodes) +
                    ", decisions=" + std::to_string(solve.stats.decisions) +
                    ", conflicts=" + std::to_string(solve.stats.conflicts) +
                    ", ii_tried=" +
                    std::to_string(solve.stats.iiCandidatesTried) + ")");
            }
            solution = solve.solution.value();
            phaseBackend = "bounded_reference_dfs";
        }
        std::string validationError;
        if (!GlobalPhaseSolver::validateSolution(
                problem, solution, &validationError))
        {
            throw std::runtime_error(
                "global cyclic phase/epoch validation failed: " + validationError);
        }
        const auto phaseStop = SteadyClock::now();

        const std::size_t maxObservedSamePhaseTileRun =
            measureMaxSamePhaseTileRun(problem, solution);
        if (maxObservedSamePhaseTileRun >
            static_cast<std::size_t>(command.maxSamePhaseTiles))
        {
            throw std::runtime_error(
                "tile phase validation exceeded the configured same-phase run");
        }
        const auto phases = tilePhaseMap(solution);
        for (const auto &phase : phases)
        {
            board.gridMap[phase.first].setPhase(phase.second + 1);
        }
        const std::size_t crossoverSegments = mappedLayout.crossoverSegments;
        const std::size_t mappedCells = mappedLayout.uniqueCells.size();
        const auto mappingStop = SteadyClock::now();
        const auto seconds = [](const auto &start, const auto &stop) {
            return std::chrono::duration<double>(stop - start).count();
        };
        writeLayout(command.output, parse, graph, netlist, phases,
                    solution, mappedCells, mappedLayout.cellSites.size(),
                    crossoverSegments, command.maxSamePhaseTiles,
                    maxObservedSamePhaseTileRun,
                    solveStats, phaseBackend,
                    seconds(totalStart, frontendStop),
                    seconds(frontendStop, geometryStop),
                    seconds(mappingPreparationStop, phaseStop),
                    seconds(geometryStop, mappingPreparationStop) +
                        seconds(phaseStop, mappingStop),
                    seconds(totalStart, mappingStop), geometrySelection);

        if (!command.latexOutput.empty())
        {
            const std::filesystem::path latexPath(command.latexOutput);
            if (!latexPath.parent_path().empty())
            {
                std::filesystem::create_directories(latexPath.parent_path());
            }
            // Keep the repository's established node/route style.  The new
            // flow supplies only the transformed node set, coordinates,
            // closed-loop paths, and solved phases.
            graph.printLaTex(command.latexOutput);
        }

        std::cout << "paper_cyclic_pnr=success"
                  << " module=" << parse.get_moduleName()
                  << " nodes=" << graph.nodeIndex_pos.size()
                  << " routes=" << graph.routes.size()
                  << " feedback_routes=" << netlist.feedbackEdges.size()
                  << " q_pseudo_nodes=0"
                  << " directed_cycle=true"
                  << " II=" << solution.initiationInterval
                  << " phase_backend=" << phaseBackend
                  << " phase_granularity=tile"
                  << " max_same_phase_tile_run="
                  << maxObservedSamePhaseTileRun
                  << " bbox=" << bestGeometry.width << 'x'
                  << bestGeometry.height
                  << " route_steps=" << bestGeometry.routeSteps
                  << " compact_candidates="
                  << geometryCandidates.size()
                  << " geometry_rank=" << command.geometryRank
                  << " seam_rows_removed="
                  << geometrySelection.selectedCompaction.rowsRemoved
                  << " seam_columns_removed="
                  << geometrySelection.selectedCompaction.columnsRemoved
                  << " seam_optimality_proven="
                  << (geometrySelection.selectedCompaction.optimalityProven
                          ? "true" : "false")
                  << " mapped_qca_cells=" << mappedCells
                  << " crossover_segments=" << crossoverSegments
                  << " output=" << command.output;
        if (!command.latexOutput.empty())
        {
            std::cout << " tex=" << command.latexOutput;
        }
        std::cout << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "ifcn_paper_cyclic_pnr failed: " << error.what() << '\n';
        return 1;
    }
}
