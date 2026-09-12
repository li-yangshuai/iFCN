#include "autopr/graph/legacyGraphvizRenderer.h"
#include "autopr/graph/parse.h"

#include <cassert>
#include <cmath>
#include <iostream>
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

    std::cout << "Legacy Graphviz renderer tests passed: TOY "
              << toy.nodeCount << '/' << toy.edgeCount << ", MAJ "
              << maj.nodeCount << '/' << maj.edgeCount << ".\n";
    return 0;
}
