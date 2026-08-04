#pragma once

#include "parse.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>

namespace fcngraph {

struct LegacyGraphvizOptions
{
    bool showCircuitLabels = true;
    bool boxNodes = true;
    bool orthogonalEdges = true;
};

struct LegacyGraphvizResult
{
    bool success = false;
    std::string svg;
    std::map<int, std::pair<double, double>> nodePositions;
    std::string error;
    std::size_t nodeCount = 0;
    std::size_t edgeCount = 0;
};

// Recreates the Graphviz DOT drawing stage used by the June 2025 Graph Draw
// flow.  The renderer is intentionally independent of placement/routing and
// returns an in-memory SVG so callers do not overwrite files beside a netlist.
LegacyGraphvizResult renderLegacyGraphviz(
    Parse &parse,
    const LegacyGraphvizOptions &options = LegacyGraphvizOptions{});

} // namespace fcngraph
