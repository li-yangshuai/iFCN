#include "autopr/algorithms/astar.h"
#include "autopr/graph/circuitGraph.h"
#include "autopr/graph/parse.h"
#include "autopr/grid/grid.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string jsonEscape(const std::string &value)
{
    std::ostringstream escaped;
    for (const char character : value) {
        switch (character) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            escaped << character;
            break;
        }
    }
    return escaped.str();
}

void dumpPaperStage(const std::string &stageName,
                    fcngraph::Parse &parse,
                    fcngraph::CircuitGraph &graph,
                    const fcngraph::GridChessboard &board,
                    const std::vector<std::vector<int>> &orderedLayers)
{
    const char *directory = std::getenv("IFCN_PAPER_STAGE_DIR");
    if (directory == nullptr || *directory == '\0') {
        return;
    }
    mkdir(directory, 0775);
    const std::string path = std::string(directory) + "/" + stageName + ".json";
    std::ofstream output(path);
    if (!output) {
        std::cerr << "Unable to write paper stage " << path << ".\n";
        return;
    }

    output << "{\n  \"stage\": \"" << jsonEscape(stageName) << "\",\n";
    output << "  \"layers\": [";
    for (std::size_t layer = 0; layer < orderedLayers.size(); ++layer) {
        if (layer != 0) {
            output << ',';
        }
        output << '[';
        for (std::size_t index = 0; index < orderedLayers[layer].size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            output << orderedLayers[layer][index];
        }
        output << ']';
    }
    output << "],\n  \"nodes\": [";
    bool first = true;
    for (unsigned int node = 0; node < parse.getm_numVertices(); ++node) {
        if (!first) {
            output << ',';
        }
        first = false;
        const auto position = graph.nodeIndex_pos.find(static_cast<int>(node));
        output << "{\"id\":" << node
               << ",\"name\":\"" << jsonEscape(parse.getNodeName(node)) << "\""
               << ",\"type\":\"" << jsonEscape(parse.getNodeType(node)) << "\"";
        if (position != graph.nodeIndex_pos.end()) {
            output << ",\"x\":" << position->second.first
                   << ",\"y\":" << position->second.second;
        }
        output << '}';
    }
    output << "],\n  \"edges\": [";
    const auto edges = parse.getEffectiveEdges();
    for (std::size_t index = 0; index < edges.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << '[' << edges[index].first << ',' << edges[index].second << ']';
    }
    output << "],\n  \"routes\": [";
    first = true;
    for (const auto &route : graph.routes) {
        if (!first) {
            output << ',';
        }
        first = false;
        output << "{\"source\":" << route.first.first
               << ",\"target\":" << route.first.second
               << ",\"path\":[";
        for (std::size_t index = 0; index < route.second.size(); ++index) {
            if (index != 0) {
                output << ',';
            }
            output << '[' << route.second[index].first << ','
                   << route.second[index].second << ']';
        }
        output << "]}";
    }
    output << "],\n  \"cells\": [";
    first = true;
    for (const auto &cell : board.gridMap) {
        if (cell.second.get_current_weight() == 0) {
            continue;
        }
        if (!first) {
            output << ',';
        }
        first = false;
        output << "{\"x\":" << cell.first.first
               << ",\"y\":" << cell.first.second
               << ",\"phase\":" << cell.second.getPhase()
               << ",\"weight\":"
               << static_cast<int>(cell.second.get_current_weight()) << '}';
    }
    output << "]\n}\n";
    output.close();
    graph.printLaTex(std::string(directory) + "/" + stageName + ".tex");
}

int occupiedArea(const fcngraph::GridChessboard &board,
                 int *widthOut = nullptr,
                 int *heightOut = nullptr)
{
    bool initialized = false;
    unsigned int minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto &cell : board.gridMap) {
        if (cell.second.get_current_weight() == 0) {
            continue;
        }
        if (!initialized) {
            minX = maxX = cell.first.first;
            minY = maxY = cell.first.second;
            initialized = true;
        }
        minX = std::min(minX, cell.first.first);
        maxX = std::max(maxX, cell.first.first);
        minY = std::min(minY, cell.first.second);
        maxY = std::max(maxY, cell.first.second);
    }
    assert(initialized);
    const int width = static_cast<int>(maxX - minX + 1);
    const int height = static_cast<int>(maxY - minY + 1);
    if (widthOut != nullptr) {
        *widthOut = width;
    }
    if (heightOut != nullptr) {
        *heightOut = height;
    }
    return width * height;
}

void applyBarycenterOrder(std::vector<std::vector<int>> &layers,
                          const std::vector<std::pair<int, int>> &edges)
{
    std::map<int, std::vector<int>> predecessors;
    std::map<int, std::vector<int>> successors;
    std::map<int, int> layerOf;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        for (int node : layers[layer]) {
            layerOf[node] = static_cast<int>(layer);
        }
    }
    for (const auto &edge : edges) {
        if (layerOf.count(edge.first) != 0 && layerOf.count(edge.second) != 0 &&
            layerOf[edge.second] == layerOf[edge.first] + 1) {
            predecessors[edge.second].push_back(edge.first);
            successors[edge.first].push_back(edge.second);
        }
    }
    const auto reorder = [](std::vector<int> &nodes,
                            const std::vector<int> &reference,
                            const std::map<int, std::vector<int>> &neighbors) {
        std::map<int, int> referenceOrder;
        std::map<int, int> oldOrder;
        for (std::size_t index = 0; index < reference.size(); ++index) {
            referenceOrder[reference[index]] = static_cast<int>(index);
        }
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            oldOrder[nodes[index]] = static_cast<int>(index);
        }
        const auto center = [&](int node) {
            const auto found = neighbors.find(node);
            if (found == neighbors.end() || found->second.empty()) {
                return static_cast<double>(oldOrder[node]);
            }
            double total = 0.0;
            int count = 0;
            for (int adjacent : found->second) {
                if (referenceOrder.count(adjacent) != 0) {
                    total += referenceOrder[adjacent];
                    ++count;
                }
            }
            return count == 0 ? static_cast<double>(oldOrder[node]) : total / count;
        };
        std::stable_sort(nodes.begin(), nodes.end(), [&](int left, int right) {
            const double leftCenter = center(left);
            const double rightCenter = center(right);
            return leftCenter == rightCenter ? oldOrder[left] < oldOrder[right]
                                            : leftCenter < rightCenter;
        });
    };
    for (int pass = 0; pass < 8; ++pass) {
        for (std::size_t layer = 1; layer < layers.size(); ++layer) {
            reorder(layers[layer], layers[layer - 1], predecessors);
        }
        for (std::size_t offset = 1; offset < layers.size(); ++offset) {
            const std::size_t layer = layers.size() - 1 - offset;
            reorder(layers[layer], layers[layer + 1], successors);
        }
    }
}

bool applyOgdfOrder(std::vector<std::vector<int>> &layers,
                    const std::vector<std::pair<int, int>> &edges)
{
    const std::string executable = std::string(IFCN_TEST_SOURCE_DIR) +
        "/build-ogdf/ifcn_ogdf_layer_order";
    if (access(executable.c_str(), X_OK) != 0) {
        return false;
    }
    char inputTemplate[] = "/tmp/ifcn-ogdf-input-XXXXXX";
    char outputTemplate[] = "/tmp/ifcn-ogdf-output-XXXXXX";
    const int inputFd = mkstemp(inputTemplate);
    const int outputFd = mkstemp(outputTemplate);
    if (inputFd < 0 || outputFd < 0) {
        return false;
    }
    close(inputFd);
    close(outputFd);
    std::map<int, int> layerOf;
    int nodeCount = 0;
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        nodeCount += static_cast<int>(layers[layer].size());
        for (int node : layers[layer]) {
            layerOf[node] = static_cast<int>(layer);
        }
    }
    std::vector<std::pair<int, int>> adjacentEdges;
    for (const auto &edge : edges) {
        if (layerOf.count(edge.first) != 0 && layerOf.count(edge.second) != 0 &&
            layerOf[edge.second] == layerOf[edge.first] + 1) {
            adjacentEdges.push_back(edge);
        }
    }
    {
        std::ofstream input(inputTemplate);
        input << nodeCount << ' ' << adjacentEdges.size() << ' ' << layers.size() << '\n';
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            for (std::size_t order = 0; order < layers[layer].size(); ++order) {
                input << layers[layer][order] << ' ' << layer << ' ' << order << '\n';
            }
        }
        for (const auto &edge : adjacentEdges) {
            input << edge.first << ' ' << edge.second << '\n';
        }
    }
    const std::string command = executable + " < " + inputTemplate + " > " + outputTemplate;
    const bool launched = std::system(command.c_str()) == 0;
    std::vector<std::vector<int>> ordered(layers.size());
    bool valid = launched;
    if (valid) {
        std::ifstream output(outputTemplate);
        std::string marker;
        double milliseconds = 0.0;
        int crossings = 0;
        valid = static_cast<bool>(output >> marker >> milliseconds >> crossings) && marker == "OGDF";
        int layer = 0, order = 0, node = 0;
        while (valid && output >> layer >> order >> node) {
            if (layer < 0 || layer >= static_cast<int>(ordered.size()) ||
                order != static_cast<int>(ordered[layer].size())) {
                valid = false;
                break;
            }
            ordered[layer].push_back(node);
        }
    }
    std::remove(inputTemplate);
    std::remove(outputTemplate);
    if (valid) {
        for (std::size_t layer = 0; layer < layers.size(); ++layer) {
            if (std::set<int>(layers[layer].begin(), layers[layer].end()) !=
                std::set<int>(ordered[layer].begin(), ordered[layer].end())) {
                valid = false;
                break;
            }
        }
    }
    if (valid) {
        layers = std::move(ordered);
    }
    return valid;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string source = argc > 1
        ? std::string(argv[1])
        : std::string(IFCN_TEST_SOURCE_DIR) + "/tests/benchmarks_f/TOY/xor2.v";
    fcngraph::Parse parse;
    parse.parseVerilog(source);
    parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    const bool keepBufferTopology = argc > 2 && std::string(argv[2]) == "--keep-buffers";
    if (keepBufferTopology) {
        parse.addLayerRedundancyNode();
    } else {
        parse.optimizeBufferNode();
    }
    parse.caculateSameLayerNodeRoutePair();

    std::vector<std::vector<int>> orderedLayers;
    for (const auto &layer : parse.getlayerNodeDivVec()) {
        orderedLayers.emplace_back(layer.begin(), layer.end());
    }
    const auto effectiveEdges = parse.getEffectiveEdges();
    if (!applyOgdfOrder(orderedLayers, effectiveEdges)) {
        applyBarycenterOrder(orderedLayers, effectiveEdges);
    }
    std::size_t maxLayerWidth = 0;
    for (const auto &layer : orderedLayers) {
        maxLayerWidth = std::max(maxLayerWidth, layer.size());
    }
    std::cout << "Circuit nodes=" << parse.getm_numVertices()
              << ", edges=" << parse.getEffectiveEdges().size()
              << ", layers=" << orderedLayers.size()
              << ", max-layer-width=" << maxLayerWidth << ".\n";

    fcngraph::GridChessboard board;
    fcngraph::Astar legacyRouter(board, false, 600.0);
    legacyRouter.setAllowInterSourceWireOverlap(false);
    if (!keepBufferTopology) {
        legacyRouter.setOccupiedWirePenalty(0.0);
    }
    fcngraph::CircuitGraph graph(parse, source, board, legacyRouter);
    if (const char *directory = std::getenv("IFCN_PAPER_STAGE_DIR");
        directory != nullptr && *directory != '\0') {
        const std::string stageDirectory(directory);
        mkdir(stageDirectory.c_str(), 0775);
        graph.setStageCallback([&graph, stageDirectory](const std::string &stage) {
            graph.printLaTex(stageDirectory + "/" + stage + ".tex");
            if (stage == "quantized_placement" ||
                stage == "routed_unphased") {
                graph.printLaTex(stageDirectory + "/" + stage + "_2dd.tex",
                                 true);
            }
        });
    }
    if (parse.getm_numVertices() > 200 || std::getenv("IFCN_TEST_VERBOSE") != nullptr) {
        graph.setFitnessCallback([](const std::string &message) {
            std::cout << message << std::endl;
        });
    }
    bool routed = false;
    bool usedGraphvizFallback = false;
    struct Candidate {
        unsigned int xSpacing;
        unsigned int ySpacing;
        double searchCost;
    };
    std::vector<Candidate> candidates = {{8, 8, 480.0}};
    const bool forcedGraphviz = argc > 5 && std::string(argv[2]) == "--graphviz";
    const bool forcedGraphvizOverlap = argc > 5 && std::string(argv[2]) == "--graphviz-overlap";
    const bool forcedGraphvizPhase = argc > 5 && std::string(argv[2]) == "--graphviz-phase";
    const bool forcedLayered = argc > 6 && std::string(argv[2]) == "--layered";
    const bool forcedAdaptiveCompact =
        argc > 4 && std::string(argv[2]) == "--adaptive-compact";
    const bool expectedGraphvizXyFailure =
        argc > 6 && std::string(argv[2]) == "--graphviz-xy-failure";
    const bool forcedGraphvizXy =
        argc > 6 && (std::string(argv[2]) == "--graphviz-xy" ||
                     expectedGraphvizXyFailure);
    const bool forcedKeepGraphviz = keepBufferTopology && argc > 5;
    const bool forcedCandidate = argc > 4 && !keepBufferTopology && !forcedGraphviz &&
        !forcedGraphvizOverlap && !forcedGraphvizPhase && !forcedLayered &&
        !forcedAdaptiveCompact &&
        !forcedGraphvizXy;
    if (forcedCandidate) {
        candidates = {{static_cast<unsigned int>(std::stoul(argv[2])),
                       static_cast<unsigned int>(std::stoul(argv[3])),
                       std::stod(argv[4])}};
    } else if (forcedGraphviz || forcedGraphvizOverlap || forcedGraphvizPhase ||
               forcedLayered || forcedAdaptiveCompact || forcedGraphvizXy ||
               forcedKeepGraphviz) {
        candidates.clear();
    }
    if (forcedAdaptiveCompact) {
        graph.sortNodesByFixedLayerOrder(orderedLayers, 1, 1);
        std::set<unsigned int> compactSeedRows;
        for (const auto &node : graph.nodeIndex_pos) {
            compactSeedRows.insert(node.second.second);
        }
        if (compactSeedRows.size() != orderedLayers.size() ||
            *compactSeedRows.rbegin() - *compactSeedRows.begin() + 1 !=
                orderedLayers.size()) {
            std::cerr << "Compact seed does not use consecutive unit-spaced layers.\n";
            return 1;
        }
        for (const auto &layer : orderedLayers) {
            for (std::size_t index = 1; index < layer.size(); ++index) {
                const auto previous = graph.nodeIndex_pos.at(layer[index - 1]);
                const auto current = graph.nodeIndex_pos.at(layer[index]);
                if (current.first != previous.first + 1 ||
                    current.second != previous.second) {
                    std::cerr << "Compact seed does not use unit horizontal spacing.\n";
                    return 1;
                }
            }
        }
        dumpPaperStage("02_compact_seed", parse, graph, board, orderedLayers);
        routed = graph.routeCompactRandomClockWithExpansion(
            4,
            std::stoi(argv[3]),
            std::stoi(argv[4]),
            argc > 5 ? std::stod(argv[5]) : 160.0);
        const auto &stats = graph.getAdaptiveExpansionStats();
        std::cout << "Adaptive compact expansion: rows=" << stats.insertedRows
                  << ", columns=" << stats.insertedColumns
                  << ", removed-rows=" << stats.removedRows
                  << ", removed-columns=" << stats.removedColumns
                  << ", rounds=" << stats.acceptedRounds << ".\n";
        if (source.find("paper_2ddwave_crossing_demo.v") != std::string::npos &&
            routed) {
            if (stats.insertedRows != 1 || stats.insertedColumns != 2 ||
                graph.nodeIndex_pos.at(3).second !=
                    graph.nodeIndex_pos.at(7).second + 1 ||
                occupiedArea(board) != 42) {
                std::cerr << "Crossing demo adaptive layout regressed.\n";
                return 1;
            }
        }
        if (source.find("1bitAdderMaj.v") != std::string::npos && routed) {
            if (stats.insertedRows != 1 || stats.insertedColumns != 1 ||
                graph.nodeIndex_pos.at(3).second !=
                    graph.nodeIndex_pos.at(4).second + 1 ||
                occupiedArea(board) != 42) {
                std::cerr << "MAJ adder adaptive layout regressed.\n";
                return 1;
            }
        }
    } else if (forcedGraphvizXy) {
        if (std::getenv("IFCN_PAPER_STAGE_DIR") != nullptr) {
            graph.processAndGenerateGraph(false, true, true, true);
            graph.sortNodesByYThenXCoordinate(std::stod(argv[3]),
                                              std::stod(argv[4]));
            dumpPaperStage("02_quantized_seed", parse, graph, board,
                           orderedLayers);
        }
        legacyRouter.setMaxSearchCost(std::stod(argv[5]));
        routed = graph.placeAndRouteJuneRandomClockAnisotropic(
            4, std::stod(argv[3]), std::stod(argv[4]), std::stoi(argv[6]));
        usedGraphvizFallback = routed;
    } else if (forcedLayered) {
        legacyRouter.setMaxSearchCost(std::stod(argv[5]));
        routed = graph.placeAndRouteCompactLayeredClock(
            4,
            static_cast<unsigned int>(std::stoul(argv[3])),
            static_cast<unsigned int>(std::stoul(argv[4])),
            std::stoi(argv[6]));
    } else if (forcedKeepGraphviz) {
        legacyRouter.setMaxSearchCost(std::stod(argv[4]));
        routed = graph.placeAndRouteJuneRandomClock(
            4, std::stod(argv[3]), std::stoi(argv[5]));
        usedGraphvizFallback = routed;
    } else if (forcedGraphvizPhase) {
        graph.processAndGenerateGraph(false, true, true, true);
        graph.sortNodesByYThenXCoordinate(std::stod(argv[3]));
        routed = graph.placeAndRoutePhaseAware(
            4, 4, std::stod(argv[4]), std::stoi(argv[5]));
        usedGraphvizFallback = routed;
    } else if (forcedGraphviz || forcedGraphvizOverlap) {
        if (forcedGraphvizOverlap) {
            legacyRouter.setAllowInterSourceWireOverlap(true);
            legacyRouter.setOccupiedWirePenalty(24.0);
        }
        legacyRouter.setMaxSearchCost(std::stod(argv[4]));
        routed = graph.placeAndRouteJuneRandomClock(
            4, std::stod(argv[3]), std::stoi(argv[5]));
        usedGraphvizFallback = routed;
    } else if (!forcedCandidate) {
        legacyRouter.setMaxSearchCost(80.0);
        routed = graph.placeAndRouteJuneRandomClock(4, 40.0, 6);
        if (!routed) {
            legacyRouter.setMaxSearchCost(140.0);
            routed = graph.placeAndRouteJuneRandomClock(4, 37.0, 6);
        }
        if (!routed) {
            legacyRouter.setMaxSearchCost(180.0);
            routed = graph.placeAndRouteJuneRandomClock(4, 32.0, 6);
        }
        if (!routed) {
            legacyRouter.setMaxSearchCost(360.0);
            routed = graph.placeAndRouteJuneRandomClock(
                4, 20.0, keepBufferTopology ? 24 : 6);
        }
        usedGraphvizFallback = routed;
    }
    if (!routed) {
        for (const Candidate &candidate : candidates) {
            graph.sortNodesByFixedLayerOrder(
                orderedLayers,
                candidate.xSpacing,
                candidate.ySpacing);
            routed = graph.placeAndRoutePhaseAware(4, 4, candidate.searchCost);
            if (routed) {
                usedGraphvizFallback = false;
                break;
            }
        }
    }
    if (expectedGraphvizXyFailure) {
        if (routed) {
            std::cerr << "Expected the forced compact Graphviz candidate to be blocked, "
                         "but routing succeeded.\n";
            return 1;
        }
        std::cout << "Expected compact routing failure reproduced; "
                     "routing_failed stage exported when IFCN_PAPER_STAGE_DIR is set.\n";
        return 0;
    }
    assert(routed);
    dumpPaperStage("03_routed_clocked", parse, graph, board, orderedLayers);
    assert(graph.validateAssignedRoutePhases(4));
    unsigned int minNodeY = std::numeric_limits<unsigned int>::max();
    unsigned int maxNodeY = 0;
    for (const auto &node : graph.nodeIndex_pos) {
        minNodeY = std::min(minNodeY, node.second.second);
        maxNodeY = std::max(maxNodeY, node.second.second);
    }
    // Every placement engine is normalized to iFCN's top-left coordinate
    // convention before routing: primary inputs are above primary outputs.
    const unsigned int expectedInputY = minNodeY;
    const unsigned int expectedOutputY = maxNodeY;
    for (const auto &node : graph.nodeIndex_pos) {
        if (parse.getNodeType(node.first) == "input") {
            assert(node.second.second == expectedInputY);
        }
        if (parse.getOutputNodesIndex().count(static_cast<unsigned int>(node.first)) != 0) {
            assert(node.second.second == expectedOutputY);
        }
    }
    unsigned int minNodeX = std::numeric_limits<unsigned int>::max();
    unsigned int maxNodeX = 0;
    for (const auto &node : graph.nodeIndex_pos) {
        minNodeX = std::min(minNodeX, node.second.first);
        maxNodeX = std::max(maxNodeX, node.second.first);
    }
    const int nodeWidth = static_cast<int>(maxNodeX - minNodeX + 1);
    const int nodeHeight = static_cast<int>(maxNodeY - minNodeY + 1);
    int routedWidth = 0;
    int routedHeight = 0;
    const int areaBefore = occupiedArea(board, &routedWidth, &routedHeight);

    if (std::getenv("IFCN_TEST_PHASE_CYCLE_COMPACT") != nullptr) {
        graph.compactClockPhaseCycles(4, 2);
    } else if (!forcedAdaptiveCompact &&
               (!usedGraphvizFallback ||
                std::getenv("IFCN_TEST_COMPACT_FALLBACK") != nullptr)) {
        graph.compactPhaseAware(4, 4, 300.0, 3);
    }
    assert(graph.validateAssignedRoutePhases(4));
    const int areaAfter = occupiedArea(board);
    dumpPaperStage("04_after_compaction", parse, graph, board, orderedLayers);
    assert(areaAfter <= areaBefore);
    if (forcedGraphvizXy && argc > 7) {
        assert(areaAfter <= std::stoi(argv[7]));
    }

    std::cout << "Stochastic compact graph integration test passed: "
              << areaBefore << " -> " << areaAfter
              << "; nodes=" << nodeWidth << "x" << nodeHeight
              << ", routed=" << routedWidth << "x" << routedHeight
              << (usedGraphvizFallback ? " (Graphviz fallback).\n" : ".\n");
    return 0;
}
