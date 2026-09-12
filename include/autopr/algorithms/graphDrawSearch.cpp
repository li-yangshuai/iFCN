#include <autopr/algorithms/graphDrawSearch.h>
#include <autopr/algorithms/astar.h>
#include <autopr/algorithms/combinationalValidation.h>
#include <autopr/graph/circuitGraph.h>
#include <autopr/graph/parse.h>

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace fcngraph {
namespace {
using LayoutBounds = GraphDrawBounds;
using PhaseRunStats = GraphDrawPhaseRuns;
using LayoutSearchResult = GraphDrawCandidate;
struct LayoutAttempt {
    unsigned int xSpacing = 4;
    unsigned int ySpacing = 4;
    double searchCost = 90.0;
    bool reverseWithinLayer = false;
    bool elasticPlacement = false;
    bool horizontal = false;
};

std::vector<LayoutAttempt> buildLayoutAttempts()
{
    std::vector<LayoutAttempt> generated;
    for (unsigned int ySpacing = 1; ySpacing <= 14; ++ySpacing) {
        for (unsigned int xSpacing = 1; xSpacing <= 14; ++xSpacing) {
            generated.push_back({xSpacing, ySpacing, 90.0});
            if (xSpacing <= 8 && ySpacing <= 8) {
                generated.push_back({xSpacing, ySpacing, 150.0});
            }
            if (xSpacing <= 3 || ySpacing <= 3 || xSpacing >= 7 || ySpacing >= 7) {
                generated.push_back({xSpacing, ySpacing, 240.0});
            }
            if (xSpacing <= 4 || ySpacing <= 4 || xSpacing >= 7 || ySpacing >= 7) {
                generated.push_back({xSpacing, ySpacing, 600.0});
            }
        }
    }

    std::sort(generated.begin(), generated.end(), [](const LayoutAttempt &lhs, const LayoutAttempt &rhs) {
        const auto lhsArea = lhs.xSpacing * lhs.ySpacing;
        const auto rhsArea = rhs.xSpacing * rhs.ySpacing;
        if (lhsArea != rhsArea) {
            return lhsArea < rhsArea;
        }
        if (lhs.searchCost != rhs.searchCost) {
            return lhs.searchCost < rhs.searchCost;
        }
        if (lhs.ySpacing != rhs.ySpacing) {
            return lhs.ySpacing < rhs.ySpacing;
        }
        return lhs.xSpacing < rhs.xSpacing;
    });

    // Layered circuits primarily need vertical clearance.  Put a short set
    // of compact, vertically biased candidates before the exhaustive area
    // ordering so small legal layouts are evaluated before wide fallbacks.
    std::vector<LayoutAttempt> attempts;
    std::set<std::tuple<unsigned int, unsigned int, double, bool, bool, bool>> inserted;
    const auto appendUnique = [&attempts, &inserted](const LayoutAttempt &attempt) {
        const auto key = std::make_tuple(
            attempt.xSpacing, attempt.ySpacing, attempt.searchCost,
            attempt.reverseWithinLayer, attempt.elasticPlacement, attempt.horizontal);
        if (inserted.insert(key).second) {
            attempts.push_back(attempt);
        }
    };
    for (const LayoutAttempt &attempt : std::vector<LayoutAttempt>{
             // Elastic seeds use xSpacing/ySpacing only as a starting scale;
             // non-input gates receive independent physical Y coordinates.
             {1, 2, 90.0, false, true}, {2, 2, 90.0, false, true},
             {1, 3, 150.0, false, true}, {2, 3, 150.0, false, true},
             {2, 4, 240.0, false, true}, {3, 4, 240.0, false, true},
             {2, 5, 240.0, false, true}, {3, 5, 600.0, false, true},
             {3, 6, 600.0, false, true}, {4, 6, 600.0, false, true},
             // Some shallow MAJ networks need horizontal port clearance.
             {3, 1, 90.0}, {4, 1, 90.0},
             {5, 1, 90.0}, {6, 1, 90.0},
             {3, 4, 240.0, true}, {3, 4, 240.0},
             {1, 2, 90.0, true, true}, {2, 2, 90.0, true, true},
             {2, 3, 150.0, true, true}, {3, 4, 240.0, true, true},
             {1, 1, 90.0, true}, {1, 2, 90.0, true},
             {2, 2, 90.0, true}, {2, 3, 150.0, true},
             {2, 4, 240.0, true}, {3, 4, 240.0, true},
             {3, 5, 600.0, true}, {4, 6, 600.0, true},
             {1, 1, 90.0}, {1, 2, 90.0}, {2, 2, 90.0},
             {2, 3, 150.0}, {2, 4, 240.0}, {3, 4, 240.0},
             {3, 5, 600.0}, {4, 6, 600.0}}) {
        appendUnique(attempt);
        // The physical crossing implementation is direction sensitive. Search
        // both flow directions before ranking the resulting mapped layouts.
        auto horizontal = attempt;
        horizontal.horizontal = true;
        appendUnique(horizontal);
    }
    for (const LayoutAttempt &attempt : generated) {
        appendUnique(attempt);
    }
    return attempts;
}

std::optional<LayoutBounds> calculateOccupiedBounds(const CircuitGraph& graph)
{
    if (graph.nodeIndex_pos.empty()) return std::nullopt;
    unsigned int minX = std::numeric_limits<unsigned int>::max();
    unsigned int minY = minX, maxX = 0, maxY = 0;
    const auto include = [&](const position& point) {
        minX = std::min(minX, point.first); minY = std::min(minY, point.second);
        maxX = std::max(maxX, point.first); maxY = std::max(maxY, point.second);
    };
    for (const auto& node : graph.nodeIndex_pos) include(node.second);
    for (const auto& route : graph.routes)
        for (const auto& point : route.second) include(point);
    return LayoutBounds{static_cast<int>(minX), static_cast<int>(maxX),
                        static_cast<int>(minY), static_cast<int>(maxY),
                        static_cast<int>(maxX - minX + 1), static_cast<int>(maxY - minY + 1),
                        static_cast<int>((maxX - minX + 1) * (maxY - minY + 1))};
}

PhaseRunStats calculatePhaseRunStats(
    const std::map<std::pair<unsigned int, unsigned int>, std::vector<fcngraph::position>> &routes,
    const std::unordered_map<fcngraph::position, fcngraph::GridCell, fcngraph::PositionHash> &gridCells)
{
    PhaseRunStats stats;
    for (const auto &route : routes) {
        int previousPhase = -1;
        int currentRun = 1;
        for (const auto &pos : route.second) {
            auto cell = gridCells.find(pos);
            const int phase = (cell != gridCells.end()) ? cell->second.getPhase() : -1;
            if (phase >= 1 && previousPhase >= 1) {
                if (phase == previousPhase) {
                    ++currentRun;
                    stats.maxRun = std::max(stats.maxRun, currentRun);
                } else {
                    currentRun = 1;
                }
            } else if (phase < 1) {
                currentRun = 1;
            }
            previousPhase = phase;
        }
    }
    return stats;
}

bool isBetterLayout(const GraphDrawCandidate& candidate,
                    const GraphDrawCandidate& current)
{
    const auto score = [](const GraphDrawCandidate& layout) {
        return std::make_tuple(layout.bounds.area, layout.physicalCellCount,
                               layout.routeLength, layout.phaseRuns.maxRun,
                               std::max(layout.bounds.width, layout.bounds.height),
                               layout.bounds.width, layout.bounds.height,
                               layout.xSpacing * layout.ySpacing, layout.searchCost);
    };
    return score(candidate) < score(current);
}

GraphDrawCandidate captureCandidate(Parse& parse, CircuitGraph& graph,
                                    GridChessboard& board, const LayoutAttempt& attempt, int phaseCount)
{
    const auto bounds = calculateOccupiedBounds(graph);
    if (!bounds) throw std::runtime_error("empty graph-draw layout");
    GraphDrawCandidate result;
    result.bounds = *bounds;
    result.phaseRuns = calculatePhaseRunStats(graph.routes, board.getGridMap());
    result.xSpacing = attempt.xSpacing;
    result.ySpacing = attempt.ySpacing;
    result.searchCost = attempt.searchCost;
    result.elasticPlacement = attempt.elasticPlacement;
    result.horizontal = attempt.horizontal;
    result.nodePositions = graph.nodeIndex_pos;
    result.routes = graph.routes;
    result.gridCells = board.getGridMap();
    const auto validation = validateCombinationalLayout(parse, graph, phaseCount);
    if (!validation.valid) throw std::runtime_error(validation.error);
    result.physicalCellCount = validation.physicalCells;
    result.routeLength = static_cast<int>(validation.routeLength);
    return result;
}
} // namespace

GraphDrawSearchResult searchGraphDrawCandidates(
    Parse& parse, const std::string& sourceFile,
    const GraphDrawSearchOptions& options,
    const GraphDrawSearchCallback& progress)
{
    GraphDrawSearchResult result;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    };
    const auto expired = [&]() {
        return options.timeBudgetSeconds > 0.0 && elapsed() >= options.timeBudgetSeconds;
    };
    if ((options.phaseCount != 3 && options.phaseCount != 4) || options.maxAttempts < 1 ||
        parse.get_input_num() == 0 || parse.get_output_num() == 0 ||
        parse.getEffectiveEdges().empty()) {
        result.error = "graph-draw search requires a normalized circuit and valid phase/attempt options";
        return result;
    }
    const auto attempts = buildLayoutAttempts();
    const int limit = std::min(options.maxAttempts, static_cast<int>(attempts.size()));
    std::optional<GraphDrawCandidate> best;
    int updates = 0;
    std::string lastFailure;
    for (int index = 0; index < limit; ++index) {
        if (expired()) { result.budgetExpired = true; break; }
        const auto& attempt = attempts[index];
        result.attempts = index + 1;
        if (progress) {
            std::ostringstream message;
            message << "Candidate " << index + 1 << '/' << limit << ": spacing=("
                    << attempt.xSpacing << ',' << attempt.ySpacing << "), search cost="
                    << attempt.searchCost << ", phase=" << options.phaseCount
                    << "; placement=" << (attempt.elasticPlacement ? "elastic" : "regular")
                    << "; flow=" << (attempt.horizontal ? "horizontal" : "vertical");
            progress({index + 1, limit, updates, false, false, message.str(), best ? &*best : nullptr});
        }
        try {
            GridChessboard board;
            Astar router(board, false, attempt.searchCost);
            CircuitGraph graph(parse, sourceFile, board, router);
            graph.setCancellationCallback(expired);
            graph.processAndGenerateGraph(options.writeInitialSvg && index == 0, true, true, true);
            if (attempt.elasticPlacement)
                graph.sortNodesByElasticLayeredGrid(attempt.xSpacing, attempt.ySpacing, 4, 4,
                                                     attempt.reverseWithinLayer);
            else
                graph.sortNodesByLayeredGrid(attempt.xSpacing, attempt.ySpacing, 4, 4,
                                              attempt.reverseWithinLayer);
            if (attempt.horizontal)
                for (auto& node : graph.nodeIndex_pos) std::swap(node.second.first, node.second.second);
            if (!graph.routeGraphPlacement(options.phaseCount)) { lastFailure = "route failed"; continue; }
            if (!graph.assignPhases(options.phaseCount)) { lastFailure = "phase assignment failed"; continue; }
            auto candidate = captureCandidate(parse, graph, board, attempt, options.phaseCount);
            if (!best || isBetterLayout(candidate, *best)) {
                best = std::move(candidate);
                ++updates;
                if (progress) {
                    std::ostringstream message;
                    message << "Candidate " << index + 1 << '/' << limit << " success: "
                            << best->bounds.width << 'x' << best->bounds.height << '=' << best->bounds.area
                            << ", cells=" << best->physicalCellCount << ", max same-phase run="
                            << best->phaseRuns.maxRun << "/4";
                    progress({index + 1, limit, updates, true, false, message.str(), &*best});
                }
            }
        } catch (const std::exception& error) {
            lastFailure = error.what();
        }
    }
    if (best && !expired()) {
        if (progress) progress({result.attempts, limit, updates, false, true,
                                "Refining individual gate coordinates and compacting routed rows/columns", &*best});
        try {
            GridChessboard board;
            Astar router(board, false, std::max(240.0, best->searchCost));
            CircuitGraph graph(parse, sourceFile, board, router);
            graph.setCancellationCallback(expired);
            graph.nodeIndex_pos = best->nodePositions;
            graph.routes = best->routes;
            board.gridMap = best->gridCells;
            const bool small = parse.getEffectiveEdges().size() <= 24;
            const int rounds = options.refinementRounds >= 0 ? options.refinementRounds : (small ? 10 : 5);
            const int evaluations = options.refinementEvaluations >= 0 ? options.refinementEvaluations : (small ? 48 : 20);
            const int retries = options.refinementRouteRetries >= 0 ? options.refinementRouteRetries : (small ? 8 : 4);
            if (graph.compactMappedLayout(options.phaseCount, rounds, evaluations, retries)) {
                const LayoutAttempt seed{best->xSpacing, best->ySpacing, best->searchCost,
                                         false, best->elasticPlacement, best->horizontal};
                auto refined = captureCandidate(parse, graph, board, seed, options.phaseCount);
                if (isBetterLayout(refined, *best)) best = std::move(refined);
            }
        } catch (const std::exception& error) {
            lastFailure = std::string("non-uniform refinement: ") + error.what();
        }
    } else if (expired()) {
        result.budgetExpired = true;
    }
    result.budgetExpired = result.budgetExpired || expired();
    result.elapsedSeconds = elapsed();
    if (!best) {
        result.error = "Place, route, and phase assignment failed after " +
                       std::to_string(result.attempts) + " attempts. Last failure: " + lastFailure;
        return result;
    }
    result.layout = std::move(*best);
    result.success = true;
    return result;
}
} // namespace fcngraph
