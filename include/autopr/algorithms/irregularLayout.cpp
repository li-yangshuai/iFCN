#include "irregularLayout.h"

#include <autopr/graph/circuitGraph.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace fcngraph {
namespace {
using Clock = std::chrono::steady_clock;

std::shared_ptr<Parse> prepare(const std::string& source, bool layerBuffers)
{
    // Parse owns a shared graph internally. A fresh parse is required before
    // changing buffering, rather than mutating a shallow copy of a contender.
    auto parse = std::make_shared<Parse>();
    parse->parseVerilog(source);
    if (!parse->get_input_num() || !parse->get_output_num())
        throw std::runtime_error("Circuit must have primary inputs and outputs");
    parse->optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    if (layerBuffers) parse->addLayerRedundancyNode();
    else parse->optimizeBufferNode();
    parse->caculateSameLayerNodeRoutePair();
    return parse;
}

void orderLayers(std::vector<std::vector<int>>& layers, Parse& parse)
{
    std::map<int, std::vector<int>> incoming, outgoing;
    for (const auto edge : parse.getEffectiveEdges()) {
        incoming[edge.second].push_back(edge.first);
        outgoing[edge.first].push_back(edge.second);
    }
    for (int sweep = 0; sweep < 8; ++sweep) {
        const bool forward = sweep % 2 == 0;
        const auto& adjacent = forward ? incoming : outgoing;
        std::map<int, double> positions;
        for (const auto& layer : layers)
            for (std::size_t i = 0; i < layer.size(); ++i) positions[layer[i]] = i;
        for (std::size_t step = 0; step < layers.size(); ++step) {
            auto& layer = layers[forward ? step : layers.size() - 1 - step];
            std::map<int, double> scores;
            for (const int node : layer) {
                const auto found = adjacent.find(node);
                double sum = 0;
                if (found != adjacent.end()) for (const int other : found->second) sum += positions[other];
                scores[node] = found != adjacent.end() && !found->second.empty()
                    ? sum / found->second.size() : positions[node];
            }
            std::stable_sort(layer.begin(), layer.end(), [&](int a, int b) { return scores[a] < scores[b]; });
            for (std::size_t i = 0; i < layer.size(); ++i) positions[layer[i]] = i;
        }
    }
}

GraphDrawCandidate capture(CircuitGraph& graph, GridChessboard& board,
                           const CombinationalLayoutMetrics& metrics)
{
    GraphDrawCandidate candidate;
    candidate.nodePositions = graph.nodeIndex_pos;
    candidate.routes = graph.routes;
    candidate.gridCells = board.gridMap;
    candidate.physicalCellCount = metrics.physicalCells;
    candidate.routeLength = static_cast<int>(metrics.routeLength);
    unsigned int minX = std::numeric_limits<unsigned int>::max(), minY = minX, maxX = 0, maxY = 0;
    const auto include = [&](position point) {
        minX = std::min(minX, point.first); minY = std::min(minY, point.second);
        maxX = std::max(maxX, point.first); maxY = std::max(maxY, point.second);
    };
    for (const auto& node : graph.nodeIndex_pos) include(node.second);
    for (const auto& route : graph.routes) {
        int previous = -1, run = 0;
        for (const auto point : route.second) {
            include(point);
            const int phase = board.gridMap.at(point).getPhase();
            run = phase == previous ? run + 1 : 1;
            candidate.phaseRuns.maxRun = std::max(candidate.phaseRuns.maxRun, run);
            previous = phase;
        }
    }
    candidate.bounds = {static_cast<int>(minX), static_cast<int>(maxX),
                        static_cast<int>(minY), static_cast<int>(maxY),
                        static_cast<int>(metrics.width), static_cast<int>(metrics.height),
                        static_cast<int>(metrics.area)};
    return candidate;
}
}

IrregularLayoutResult searchIrregularLayout(const std::string& sourceFile,
                                           const IrregularLayoutOptions& options,
                                           const GraphDrawSearchCallback& progress)
{
    IrregularLayoutResult result;
    const auto start = Clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - start).count(); };
    if (options.phaseCount < 3 || options.phaseCount > 4 || options.maxAttempts < 1 ||
        !std::isfinite(options.timeBudgetSeconds) || options.timeBudgetSeconds <= 0) {
        result.error = "Irregular layout requires 3/4 phases, positive attempts and a finite positive time budget";
        return result;
    }
    std::exception_ptr observerFailure;
    const auto expired = [&] { return observerFailure || elapsed() >= options.timeBudgetSeconds; };
    const auto observe = [&](Parse& parse, const GraphDrawCandidate& candidate,
                             const CombinationalLayoutMetrics& metrics, const std::string& seed) {
        if (!options.onCandidate || !metrics.valid || observerFailure) return;
        try {
            options.onCandidate(parse, candidate, metrics, seed);
        } catch (...) {
            // Search helpers catch routing exceptions internally. Retain an
            // observer I/O failure separately so it cannot become a successful
            // partial export or be mistaken for an infeasible placement seed.
            observerFailure = std::current_exception();
        }
    };
    struct Contender {
        std::string seed;
        std::shared_ptr<Parse> parse;
        GraphDrawCandidate layout;
        CombinationalLayoutMetrics metrics;
    };
    std::vector<Contender> contenders;
    bool collecting = true;
    const auto consider = [&](const std::string& seed, std::shared_ptr<Parse> parse,
                               CircuitGraph& graph, GridChessboard& board) {
        const auto metrics = validateCombinationalLayout(*parse, graph, options.phaseCount, 4);
        if (metrics.valid && options.onCandidate)
            observe(*parse, capture(graph, board, metrics), metrics, seed);
        if (metrics.valid && collecting) {
            const bool duplicate = std::any_of(contenders.begin(), contenders.end(),
                [&](const Contender& item) {
                    return item.layout.nodePositions == graph.nodeIndex_pos && item.layout.routes == graph.routes;
                });
            if (!duplicate) {
                contenders.push_back({seed, parse, capture(graph, board, metrics), metrics});
                std::stable_sort(contenders.begin(), contenders.end(), [](const Contender& a, const Contender& b) {
                    return std::tie(a.metrics.area, a.metrics.physicalCells, a.metrics.routeLength) <
                           std::tie(b.metrics.area, b.metrics.physicalCells, b.metrics.routeLength);
                });
                // Preserve a second buffering/placement basin. A larger
                // starting layout can contract below the smallest raw seed.
                while (contenders.size() > 5) {
                    auto remove = std::prev(contenders.end());
                    if (remove->seed == "graphviz_40x40") --remove;
                    contenders.erase(remove);
                }
            }
        }
        if (metrics.valid && (!result.success ||
            std::tie(metrics.area, metrics.physicalCells, metrics.routeLength) <
            std::tie(result.metrics.area, result.metrics.physicalCells, result.metrics.routeLength))) {
            result.success = true;
            result.selectedSeed = seed;
            result.parse = std::move(parse);
            result.metrics = metrics;
            result.layout = capture(graph, board, metrics);
            if (progress) {
                GraphDrawSearchProgress event;
                event.improved = true;
                event.best = &result.layout;
                event.message = "Accepted smaller DRC-clean irregular layout";
                progress(event);
            }
        }
        return metrics;
    };
    const auto runSeed = [&](const std::string& name, std::shared_ptr<Parse> parse,
                              double stopAt, const auto& route) {
        if (expired()) return;
        IrregularLayoutAttempt attempt;
        attempt.seed = name;
        const auto seedStart = Clock::now();
        GridChessboard board;
        Astar router(board, false, 240.0);
        router.setAllowInterSourceWireOverlap(false);
        CircuitGraph graph(*parse, sourceFile, board, router);
        graph.setCancellationCallback([&] { return expired() || elapsed() >= stopAt; });
        try {
            if (route(graph, board, router)) {
                attempt.metrics = consider(name, parse, graph, board);
                attempt.valid = attempt.metrics.valid;
                attempt.error = attempt.metrics.error;
            } else attempt.error = "No completed legal candidate from this placement seed";
        } catch (const std::exception& error) { attempt.error = error.what(); }
        attempt.elapsedSeconds = std::chrono::duration<double>(Clock::now() - seedStart).count();
        attempt.budgetExpired = expired() || elapsed() >= stopAt;
        result.attempts.push_back(std::move(attempt));
    };
    try {
        // A cheap, buffered Graphviz placement provides an early fallback.
        auto buffered = prepare(sourceFile, true);
        const double seedDeadline = std::min(options.timeBudgetSeconds * 0.15, 12.0);
        for (const auto scale : std::vector<std::pair<double, double>>{
                 {40,40}, {32,32}, {48,48}, {56,56}, {64,64}, {80,80},
                 {48,64}, {64,48}, {32,64}, {64,32}, {96,96}}) {
            if (elapsed() >= seedDeadline) break;
            const auto name = "graphviz_" + std::to_string(static_cast<int>(scale.first)) +
                              "x" + std::to_string(static_cast<int>(scale.second));
            runSeed(name, buffered, seedDeadline,
                [&](CircuitGraph& graph, GridChessboard&, Astar& router) {
                    router.setMaxSearchCost(80.0);
                    return graph.routeGraphvizSeedAnisotropic(options.phaseCount,
                        scale.first, scale.second, 24, 4);
                });
        }

        auto compact = prepare(sourceFile, false);
        if (compact->getEffectiveEdges().size() <= 32) {
            std::vector<std::vector<int>> ordered;
            for (const auto& layer : compact->getlayerNodeDivVec()) ordered.emplace_back(layer.begin(), layer.end());
            orderLayers(ordered, *compact);
            std::vector<std::size_t> binaryLayers;
            for (std::size_t layer = 0; layer < ordered.size() && binaryLayers.size() < 3; ++layer)
                if (ordered[layer].size() == 2) binaryLayers.push_back(layer);
            for (unsigned int mask = 0; mask < (1u << binaryLayers.size()); ++mask) {
                auto layers = ordered;
                for (std::size_t bit = 0; bit < binaryLayers.size(); ++bit)
                    if (mask & (1u << bit)) std::reverse(layers[binaryLayers[bit]].begin(), layers[binaryLayers[bit]].end());
                for (const auto spacing : std::vector<std::pair<unsigned int, unsigned int>>{{2,1},{3,1},{2,2}}) {
                    if (elapsed() >= seedDeadline) break;
                    const auto name = "layer_order_" + std::to_string(mask) + "_" +
                                      std::to_string(spacing.first) + "x" + std::to_string(spacing.second);
                    runSeed(name, compact, seedDeadline,
                        [&](CircuitGraph& graph, GridChessboard&, Astar&) {
                            graph.sortNodesByFixedLayerOrder(layers, spacing.first, spacing.second, 4, 4);
                            return graph.routeGraphPlacement(options.phaseCount);
                        });
                }
                if (elapsed() >= seedDeadline) break;
            }
        }

        // Search a range of regular, elastic and Graphviz placements with the
        // same routed-geometry/clock acceptance and exact area objective.
        if (!expired()) {
            IrregularLayoutAttempt attempt;
            attempt.seed = "graph_placements";
            GraphDrawSearchOptions search;
            search.phaseCount = options.phaseCount;
            search.maxAttempts = options.maxAttempts;
            search.timeBudgetSeconds = std::max(0.001, (options.timeBudgetSeconds - elapsed()) * 0.72);
            const auto graphProgress = [&](const GraphDrawSearchProgress& event) {
                if (observerFailure) std::rethrow_exception(observerFailure);
                if (options.onCandidate && event.improved && event.best && !observerFailure) {
                    GridChessboard board;
                    Astar router(board, false, 240.0);
                    CircuitGraph graph(*compact, sourceFile, board, router);
                    graph.nodeIndex_pos = event.best->nodePositions;
                    graph.routes = event.best->routes;
                    board.gridMap = event.best->gridCells;
                    const auto metrics = validateCombinationalLayout(*compact, graph, options.phaseCount, 4);
                    // Forward only to the observer: consider() would also
                    // mutate the contender beam and recursively send progress.
                    observe(*compact, *event.best, metrics,
                            "graph_placements_attempt_" + std::to_string(event.attempt));
                }
                if (progress) progress(event);
            };
            const auto candidates = searchGraphDrawCandidates(*compact, sourceFile, search,
                options.onCandidate ? GraphDrawSearchCallback(graphProgress) : progress);
            attempt.elapsedSeconds = candidates.elapsedSeconds;
            attempt.budgetExpired = candidates.budgetExpired;
            attempt.error = candidates.error;
            if (candidates.success) {
                GridChessboard board;
                Astar router(board, false, 240.0);
                CircuitGraph graph(*compact, sourceFile, board, router);
                graph.nodeIndex_pos = candidates.layout.nodePositions;
                graph.routes = candidates.layout.routes;
                board.gridMap = candidates.layout.gridCells;
                attempt.metrics = consider(attempt.seed, compact, graph, board);
                attempt.valid = attempt.metrics.valid;
                if (!attempt.valid) attempt.error = attempt.metrics.error;
            }
            result.attempts.push_back(std::move(attempt));
        }

        // Tight initial spacing is useful for small graphs. Large-graph
        // baselines spend their budget expanding it, so use that time on
        // refinement of the already legal incumbent instead.
        if (!expired() && compact->getEffectiveEdges().size() <= 24) {
            runSeed("minimum_spacing", compact, options.timeBudgetSeconds * 0.93,
                [&](CircuitGraph& graph, GridChessboard&, Astar& router) {
                    std::vector<std::vector<int>> layers;
                    for (const auto& layer : compact->getlayerNodeDivVec()) layers.emplace_back(layer.begin(), layer.end());
                    orderLayers(layers, *compact);
                    router.setOccupiedWirePenalty(0.0);
                    graph.sortNodesByFixedLayerOrder(layers, 1, 1, 2, 2);
                    return graph.routeWithCapacityExpansion(options.phaseCount, 12, 8, 240.0, 4);
                });
        }

        collecting = false;
        for (std::size_t index = 0; index < contenders.size() && !expired(); ++index) {
            const auto& contender = contenders[index];
            const double stopAt = elapsed() + (options.timeBudgetSeconds - elapsed()) /
                                              static_cast<double>(contenders.size() - index);
            runSeed(contender.seed + "_compacted", contender.parse, stopAt,
                [&](CircuitGraph& graph, GridChessboard& board, Astar&) {
                    graph.nodeIndex_pos = contender.layout.nodePositions;
                    graph.routes = contender.layout.routes;
                    board.gridMap = contender.layout.gridCells;
                    return graph.compactMappedLayout(options.phaseCount, 24, 192, 8);
                });
        }

    } catch (const std::exception& error) { result.error = error.what(); }
    if (observerFailure) std::rethrow_exception(observerFailure);
    result.elapsedSeconds = elapsed();
    result.budgetExpired = expired();
    if (result.success) result.error.clear();
    else if (result.error.empty()) result.error = "No placement candidate passed routing, physical mapping and global clock validation";
    return result;
}
}
