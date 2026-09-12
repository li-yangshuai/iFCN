#include <autopr/algorithms/irregularLayout.h>
#include <autopr/graph/circuitGraph.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <tuple>
#include <unistd.h>

int main()
{
    using namespace fcngraph;
    char temporary[] = "/tmp/ifcn-irregular-contract-XXXXXX";
    if (!mkdtemp(temporary)) return 1;
    const auto directory = std::filesystem::path(temporary);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{directory};
    const auto source = directory / "and.v";
    std::ofstream(source) << "module example(input a, input b, output y); assign y=a&b; endmodule\n";
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    try {
        IrregularLayoutOptions options;
        options.timeBudgetSeconds = 1.0;
        options.maxAttempts = 1;
        struct Observation {
            GraphDrawCandidate layout;
            std::vector<std::pair<int, int>> edges;
            std::map<int, std::tuple<std::string, std::string, bool>> nodes;
        };
        std::vector<Observation> observations;
        options.onCandidate = [&](Parse& parse, const GraphDrawCandidate& layout,
                                  const CombinationalLayoutMetrics& metrics, const std::string& seed) {
            require(metrics.valid && !seed.empty(), "Observer received an invalid or unnamed candidate");
            GridChessboard board;
            Astar router(board, false, 240.0);
            CircuitGraph graph(parse, source.string(), board, router);
            graph.nodeIndex_pos = layout.nodePositions;
            graph.routes = layout.routes;
            board.gridMap = layout.gridCells;
            const auto checked = validateCombinationalLayout(parse, graph, 4, 4);
            require(checked.valid && checked.physicalCells == metrics.physicalCells &&
                    checked.area == metrics.area, "Observed snapshot failed independent native DRC");
            Observation item{layout, parse.getEffectiveEdges(), {}};
            for (const auto& node : layout.nodePositions)
                item.nodes.emplace(node.first, std::make_tuple(parse.getNodeName(node.first),
                    parse.getNodeType(node.first), parse.getOutputNodesIndex().count(node.first) != 0));
            observations.push_back(std::move(item));
        };
        // An accepted incumbent must survive the deadline, including time
        // spent delivering progress to a GUI consumer.
        const auto result = searchIrregularLayout(source.string(), options,
            [](const GraphDrawSearchProgress& event) {
                if (event.improved) std::this_thread::sleep_for(std::chrono::milliseconds(1050));
            });
        require(result.success && result.parse && result.budgetExpired,
                "Deadline discarded a completed legal incumbent");
        require(result.metrics.valid && result.metrics.area > 0 && result.metrics.physicalCells > 0,
                "Winner lacks physical geometry metrics");
        require(!observations.empty(), "No completed candidate was observed");
        bool observedWinner = false;
        for (const auto& observation : observations) {
            if (observation.layout.nodePositions != result.layout.nodePositions ||
                observation.layout.routes != result.layout.routes) continue;
            bool same = observation.edges == result.parse->getEffectiveEdges();
            for (const auto& node : observation.nodes)
                same = same && node.second == std::make_tuple(result.parse->getNodeName(node.first),
                    result.parse->getNodeType(node.first), result.parse->getOutputNodesIndex().count(node.first) != 0);
            same = same && observation.layout.gridCells.size() == result.layout.gridCells.size();
            for (const auto& cell : result.layout.gridCells) {
                const auto found = observation.layout.gridCells.find(cell.first);
                same = same && found != observation.layout.gridCells.end() &&
                       found->second.getPhase() == cell.second.getPhase() &&
                       found->second.get_current_weight() == cell.second.get_current_weight();
            }
            observedWinner = observedWinner || same;
        }
        require(observedWinner, "The final winner was never observed");
        for (const auto& attempt : result.attempts) if (attempt.valid)
            require(std::tie(result.metrics.area, result.metrics.physicalCells, result.metrics.routeLength) <=
                    std::tie(attempt.metrics.area, attempt.metrics.physicalCells, attempt.metrics.routeLength),
                    "Winner does not minimize the documented lexicographic objective");
        GridChessboard board;
        Astar router(board, false, 240.0);
        CircuitGraph graph(*result.parse, source.string(), board, router);
        graph.nodeIndex_pos = result.layout.nodePositions;
        graph.routes = result.layout.routes;
        board.gridMap = result.layout.gridCells;
        graph.setCancellationCallback([] { return true; });
        const auto savedNodes = graph.nodeIndex_pos;
        const auto savedRoutes = graph.routes;
        require(graph.compactMappedLayout(4, 12, 96, 8), "Cancelled refinement lost its legal input");
        require(graph.nodeIndex_pos == savedNodes && graph.routes == savedRoutes,
                "Cancelled refinement changed the incumbent geometry");
        int observedGraphImprovements = 0, graphProgressImprovements = 0;
        const auto validateObservation = options.onCandidate;
        options.timeBudgetSeconds = 2.0;
        options.onCandidate = [&](Parse& parse, const GraphDrawCandidate& layout,
                                  const CombinationalLayoutMetrics& metrics, const std::string& seed) {
            validateObservation(parse, layout, metrics, seed);
            if (seed.rfind("graph_placements_attempt_", 0) == 0) ++observedGraphImprovements;
        };
        const auto observedSearch = searchIrregularLayout(source.string(), options,
            [&](const GraphDrawSearchProgress& event) {
                if (event.improved && event.attempt > 0) ++graphProgressImprovements;
            });
        require(observedSearch.success && graphProgressImprovements > 0 &&
                observedGraphImprovements == graphProgressImprovements,
                "Graph-search improvements were omitted or recursively forwarded");
        options.timeBudgetSeconds = 0;
        const auto observedBeforeInvalidOptions = observations.size();
        require(!searchIrregularLayout(source.string(), options).success, "Invalid budget accepted");
        options.timeBudgetSeconds = 1;
        options.phaseCount = 2;
        require(!searchIrregularLayout(source.string(), options).success, "Unsupported clock scheme accepted");
        require(observations.size() == observedBeforeInvalidOptions,
                "Invalid search options emitted a candidate");
        options.phaseCount = 4;
        options.onCandidate = [](Parse&, const GraphDrawCandidate&,
                                 const CombinationalLayoutMetrics&, const std::string&) {
            throw std::runtime_error("observer write failure");
        };
        bool observerFailurePropagated = false;
        try { searchIrregularLayout(source.string(), options); }
        catch (const std::runtime_error& error) {
            observerFailurePropagated = std::string(error.what()) == "observer write failure";
        }
        require(observerFailurePropagated, "Observer error was swallowed as a routing failure");
        std::cout << "Irregular incumbent, deadline, cancellation and objective contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
