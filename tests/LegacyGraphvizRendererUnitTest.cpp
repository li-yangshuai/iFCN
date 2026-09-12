#include "autopr/graph/legacyGraphvizRenderer.h"
#include "autopr/graph/circuitGraph.h"
#include "autopr/graph/parse.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

fcngraph::LegacyGraphvizResult renderCircuit(const std::string &path)
{
    fcngraph::Parse parse;
    parse.parseVerilog(path);
    parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    parse.optimizeBufferNode();
    parse.addLayerRedundancyNode();
    parse.caculateSameLayerNodeRoutePair();

    const fcngraph::LegacyGraphvizResult result = fcngraph::renderLegacyGraphviz(parse);
    assert(result.success);
    assert(result.error.empty());
    assert(result.nodeCount > 0);
    assert(result.edgeCount > 0);
    assert(result.nodePositions.size() == result.nodeCount);
    assert(result.svg.find("<svg") != std::string::npos);
    assert(result.svg.find("</svg>") != std::string::npos);
    for (const auto &entry : result.nodePositions) {
        assert(std::isfinite(entry.second.first));
        assert(std::isfinite(entry.second.second));
    }
    return result;
}

void checkRepeatedMixedLayouts(const std::string &toyPath, const std::string &majPath)
{
    fcngraph::Parse parse;
    parse.parseVerilog(toyPath);
    parse.optimizeAIOG_DRC(2, 2, 2, 2, 2, 2);
    parse.optimizeBufferNode();
    parse.addLayerRedundancyNode();
    parse.caculateSameLayerNodeRoutePair();

    fcngraph::GridChessboard board;
    fcngraph::Astar router(board, false);
    fcngraph::CircuitGraph graph(parse, toyPath, board, router);
    decltype(graph.nodeIndex_pos) firstPlacement;
    for (int iteration = 0; iteration < 16; ++iteration) {
        // The SVG renderer and placement generator must borrow the same
        // live context. Separate per-entry-point contexts still poison the
        // process-wide Pango font cache when calls are interleaved.
        const auto rendered = renderCircuit(iteration % 2 == 0 ? toyPath : majPath);
        assert(rendered.success);
        graph.processAndGenerateGraph(false, true, true, true);
        graph.sortNodesByYThenXCoordinate(40.0);
        assert(!graph.nodeIndex_pos.empty());
        if (iteration == 0) {
            firstPlacement = graph.nodeIndex_pos;
        } else {
            assert(graph.nodeIndex_pos == firstPlacement);
        }
    }

    fcngraph::Parse emptyParse;
    fcngraph::CircuitGraph emptyGraph(emptyParse, "", board, router);
    bool rejected = false;
    try {
        emptyGraph.processAndGenerateGraph();
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    assert(rejected);
    // A rejected layout must release its resources and mutex as well.
    assert(renderCircuit(toyPath).success);
}

} // namespace

int main()
{
    fcngraph::Parse emptyParse;
    const fcngraph::LegacyGraphvizResult emptyResult =
        fcngraph::renderLegacyGraphviz(emptyParse);
    assert(!emptyResult.success);
    assert(!emptyResult.error.empty());

    const std::string toyPath = std::string(IFCN_TEST_SOURCE_DIR) +
        "/tests/benchmarks_f/TOY/xor2.v";
    const std::string majPath = std::string(IFCN_TEST_SOURCE_DIR) +
        "/tests/benchmarks_f/MAJ/1bitAdderMaj.v";

    const fcngraph::LegacyGraphvizResult toy = renderCircuit(toyPath);
    const fcngraph::LegacyGraphvizResult maj = renderCircuit(majPath);
    const fcngraph::LegacyGraphvizResult toyRepeated = renderCircuit(toyPath);

    assert(toy.nodeCount == toyRepeated.nodeCount);
    assert(toy.edgeCount == toyRepeated.edgeCount);
    assert(toy.nodePositions.size() == toyRepeated.nodePositions.size());
    assert(maj.svg.find(">M</text>") != std::string::npos);
    checkRepeatedMixedLayouts(toyPath, majPath);

    std::cout << "Legacy Graphviz renderer tests passed: TOY "
              << toy.nodeCount << '/' << toy.edgeCount << ", MAJ "
              << maj.nodeCount << '/' << maj.edgeCount << ".\n";
    return 0;
}
