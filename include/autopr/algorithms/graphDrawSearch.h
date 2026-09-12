#pragma once

#include <autopr/grid/grid.h>

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace fcngraph {

class Parse;

struct GraphDrawBounds {
    int minX = 0, maxX = 0, minY = 0, maxY = 0;
    int width = 0, height = 0, area = 0;
};

struct GraphDrawPhaseRuns { int maxRun = 1; };

struct GraphDrawCandidate {
    GraphDrawBounds bounds;
    int routeLength = 0;
    GraphDrawPhaseRuns phaseRuns;
    std::size_t physicalCellCount = 0;
    unsigned int xSpacing = 0, ySpacing = 0;
    double searchCost = 0.0;
    bool elasticPlacement = false;
    bool horizontal = false;
    std::map<int, position> nodePositions;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
    std::unordered_map<position, GridCell, PositionHash> gridCells;
};

struct GraphDrawSearchOptions {
    int phaseCount = 4;
    int maxAttempts = 320;
    // Negative values retain the original small/large-circuit defaults.
    int refinementRounds = -1;
    int refinementEvaluations = -1;
    int refinementRouteRetries = -1;
    // Checked between candidates and inside routing/refinement. Expiration
    // returns the best completed legal candidate, if one has been found.
    double timeBudgetSeconds = 0.0;
    bool writeInitialSvg = false;
};

struct GraphDrawSearchProgress {
    int attempt = 0;
    int attemptLimit = 0;
    int bestUpdates = 0;
    bool improved = false;
    bool refining = false;
    std::string message;
    // Valid only during the synchronous callback; the search owns the value.
    const GraphDrawCandidate* best = nullptr;
};

struct GraphDrawSearchResult {
    bool success = false;
    bool budgetExpired = false;
    GraphDrawCandidate layout;
    std::string error;
    int attempts = 0;
    double elapsedSeconds = 0.0;
};

using GraphDrawSearchCallback = std::function<void(const GraphDrawSearchProgress&)>;

// The caller parses/normalizes and applies optimizeAIOG_DRC,
// optimizeBufferNode and caculateSameLayerNodeRoutePair before searching.
// No Qt objects, GUI event loop, or external-process adapters are required.
GraphDrawSearchResult searchGraphDrawCandidates(
    Parse& parse, const std::string& sourceFile,
    const GraphDrawSearchOptions& options = {},
    const GraphDrawSearchCallback& progress = {});

} // namespace fcngraph
