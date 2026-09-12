#include <autopr/algorithms/genetic.h>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <cmath>

// Test/benchmark adapter for the same production GA used by the GUI.
// Randomness intentionally follows the production API; successful geometry is
// not deterministic across invocations, and callers must independently validate.
int main(int argc, char** argv) {
    using namespace fcngraph;
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: heuristic_layout_driver USE|RES|2DDWave source.v candidate.json\n"
                     "Production GA: 32 generations, population 64, crossover 0.9, mutation 0.5.\n"
                     "The caller owns the wall-clock timeout and independent validation.\n";
        return 0;
    }
    if (argc != 4) {
        std::cerr << "Expected scheme, Verilog input, and JSON output; use --help.\n";
        return 2;
    }
    try {
    const std::string scheme = argv[1];
    const auto clock = scheme == "USE" ? CLOCK_SCHEME::USE :
                       scheme == "RES" ? CLOCK_SCHEME::RES : CLOCK_SCHEME::TDD;
    if (scheme != "USE" && scheme != "RES" && scheme != "2DDWave") return 2;
    Parse parse;
    parse.parseVerilog(argv[2], ifcn::verilog::OutputBoundaryMode::Combinational, false);
    parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    parse.optimizeBufferNode();
    parse.caculateSameLayerNodeRoutePair();
    const unsigned nodesCount = parse.getEffectiveNodes().size();
    if (!nodesCount || !parse.get_input_num()) throw std::runtime_error("Empty or unsupported circuit");
    std::map<unsigned, unsigned> depths;
    for (const auto node : parse.getEffectiveNodes()) depths[node] = 0;
    for (unsigned iteration = 0; iteration < nodesCount; ++iteration) {
        bool changed = false;
        for (const auto edge : parse.getEffectiveEdges()) {
            const unsigned depth = depths[edge.first] + 1;
            if (depths[edge.second] < depth) { depths[edge.second] = depth; changed = true; }
        }
        if (!changed) break;
    }
    unsigned maxDepth = 0; for (const auto& node : depths) maxDepth = std::max(maxDepth, node.second);
    const unsigned inputsCount = parse.get_input_num();
    const unsigned targetSide = std::max(unsigned(std::ceil(std::sqrt(4.0 * nodesCount))), inputsCount + maxDepth + 4);
    const unsigned side = std::max(8u, ((targetSide + 3) / 4) * 4);
    GridChessboard board(clock, {0, 0}, {side, side});
    Astar router(board);
    router.setAllowInterSourceWireOverlap(false);
    GeneticAlgorithm algorithm(parse, board, router, 32, 64, .9, .5);
    std::size_t incumbents = 0;
    algorithm.setFitnessCallback([&](double fitness) {
        std::cerr << "Legal incumbent " << ++incumbents << ", fitness=" << fitness << '\n';
    });
    const auto start = std::chrono::steady_clock::now();
    const bool success = algorithm.gaRun();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    QJsonObject result{{"schema", "ifcn.heuristic_runtime.v1"}, {"scheme", QString::fromStdString(scheme)},
        {"routed", success}, {"native_layout_valid", success}, {"run_time_s", seconds},
        {"grid_width", int(side)}, {"grid_height", int(side)}, {"parsed_nodes", int(nodesCount)}, {"topological_depth", int(maxDepth)}, {"parsed_inputs", int(inputsCount)}, {"preserve_majority", false}, {"generations", 32}, {"population", 64},
        {"crossover_rate", .9}, {"mutation_rate", .5}, {"legal_incumbents", int(incumbents)},
        {"randomness", "Production std::random_device initialization; no deterministic seed API"}};
    if (success) {
        const auto nodes = algorithm.getNodePos();
        const auto routes = algorithm.getRoutes();
        QJsonArray jsonNodes, jsonRoutes, jsonEdges, jsonLayers, jsonCells;
        std::set<position> used;
        for (const auto& node : nodes) {
            const auto type = parse.getNodeType(node.first);
            jsonNodes.append(QJsonObject{{"id", int(node.first)}, {"name", QString::fromStdString(parse.getNodeName(node.first))},
                {"type", QString::fromStdString(type)}, {"is_input", type == "input"},
                {"is_output", parse.getOutputNodesIndex().count(node.first) != 0},
                {"x", int(node.second.first)}, {"y", int(node.second.second)}});
            used.insert(node.second);
        }
        for (const auto& route : routes) {
            QJsonArray path;
            for (const auto& point : route.second) { path.append(QJsonArray{int(point.first), int(point.second)}); used.insert(point); }
            jsonRoutes.append(QJsonObject{{"source", int(route.first.first)}, {"target", int(route.first.second)}, {"path", path}});
        }
        for (const auto& edge : parse.getEffectiveEdges()) jsonEdges.append(QJsonArray{int(edge.first), int(edge.second)});
        for (const auto& layer : parse.getlayerNodeDivVec()) {
            QJsonArray value;
            for (const auto node : layer) value.append(node);
            jsonLayers.append(value);
        }
        for (const auto& point : used) jsonCells.append(QJsonObject{{"x", int(point.first)}, {"y", int(point.second)},
            {"phase", int(board.getCoorPos_Phase(point.first, point.second))}});
        result["nodes"] = jsonNodes; result["routes"] = jsonRoutes; result["edges"] = jsonEdges;
        result["layers"] = jsonLayers; result["cells"] = jsonCells;
        result["fitness"] = double(algorithm.best_individuals.back().getfitness());
    }
    std::ofstream output(argv[3]);
    if (!output) throw std::runtime_error("Cannot open candidate output");
    output << QJsonDocument(result).toJson().constData();
    if (!output) throw std::runtime_error("Cannot write candidate output");
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return success ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Heuristic layout failed: " << error.what() << '\n';
        return 2;
    }
}
