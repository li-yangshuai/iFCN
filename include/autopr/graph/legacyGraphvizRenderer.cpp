#include "legacyGraphvizRenderer.h"

#include <graphviz/gvc.h>

#include <cmath>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>

namespace fcngraph {
namespace {

std::mutex &graphvizMutex()
{
    static std::mutex mutex;
    return mutex;
}

struct GraphvizResources
{
    GVC_t *context = nullptr;
    Agraph_t *graph = nullptr;
    bool hasLayout = false;

    ~GraphvizResources()
    {
        if (hasLayout && context != nullptr && graph != nullptr) {
            gvFreeLayout(context, graph);
        }
        if (graph != nullptr) {
            agclose(graph);
        }
        if (context != nullptr) {
            gvFreeContext(context);
        }
    }
};

struct GraphvizRenderBuffer
{
    char *data = nullptr;

    ~GraphvizRenderBuffer()
    {
        if (data != nullptr) {
            gvFreeRenderData(data);
        }
    }
};

void setNodeAppearance(Agnode_t *node,
                       int nodeIndex,
                       Parse &parse,
                       const LegacyGraphvizOptions &options)
{
    const std::string nodeType = parse.getNodeType(nodeIndex);
    if (options.boxNodes) {
        agsafeset(node, const_cast<char *>("shape"), const_cast<char *>("box"),
                  const_cast<char *>(""));
    }
    agsafeset(node, const_cast<char *>("width"), const_cast<char *>("0.5"),
              const_cast<char *>(""));
    agsafeset(node, const_cast<char *>("height"), const_cast<char *>("0.5"),
              const_cast<char *>(""));
    agsafeset(node, const_cast<char *>("style"), const_cast<char *>("filled"),
              const_cast<char *>(""));
    agsafeset(node, const_cast<char *>("fillcolor"), const_cast<char *>("#87ceeb"),
              const_cast<char *>(""));
    agsafeset(node, const_cast<char *>("fixedsize"), const_cast<char *>("true"),
              const_cast<char *>(""));

    const char *stroke = (nodeType == "not") ? "red"
        : ((nodeType == "fanout" || nodeType == "wire") ? "green" : "black");
    agsafeset(node, const_cast<char *>("color"), const_cast<char *>(stroke),
              const_cast<char *>(""));

    if (!options.showCircuitLabels) {
        return;
    }

    std::string label;
    if (nodeType == "and") {
        label = "&";
    } else if (nodeType == "or") {
        label = "|";
    } else if (nodeType == "maj") {
        label = "M";
    } else if (nodeType == "not") {
        label = "~";
    } else if (nodeType == "fanout") {
        label = "F";
    } else if (nodeType == "wire") {
        label = "W";
    } else if (nodeType == "input") {
        label = "I" + std::to_string(nodeIndex);
    }

    if (!label.empty()) {
        agsafeset(node, const_cast<char *>("label"),
                  const_cast<char *>(label.c_str()), const_cast<char *>(""));
    }
}

} // namespace

LegacyGraphvizResult renderLegacyGraphviz(Parse &parse,
                                          const LegacyGraphvizOptions &options)
{
    LegacyGraphvizResult result;
    std::lock_guard<std::mutex> lock(graphvizMutex());

    try {
        const auto &layerNodes = parse.getlayerNodeDivVec();
        std::size_t nodeCount = 0;
        for (const auto &layer : layerNodes) {
            nodeCount += layer.size();
        }
        if (nodeCount == 0) {
            result.error = "The parsed circuit contains no drawable nodes.";
            return result;
        }

        GraphvizResources resources;
        resources.context = gvContext();
        if (resources.context == nullptr) {
            result.error = "Graphviz could not create a layout context.";
            return result;
        }

        resources.graph = agopen(const_cast<char *>("G"), Agdirected, nullptr);
        if (resources.graph == nullptr) {
            result.error = "Graphviz could not create a directed graph.";
            return result;
        }

        std::map<int, Agnode_t *> nodes;
        for (const auto &layer : layerNodes) {
            for (int nodeIndex : layer) {
                const std::string nodeName = std::to_string(nodeIndex);
                Agnode_t *node = agnode(resources.graph,
                                       const_cast<char *>(nodeName.c_str()), TRUE);
                if (node == nullptr) {
                    throw std::runtime_error("Graphviz failed to create node " + nodeName + ".");
                }
                nodes[nodeIndex] = node;
                setNodeAppearance(node, nodeIndex, parse, options);
            }
        }

        const auto edges = parse.getEffectiveEdges();
        for (const auto &edge : edges) {
            const auto source = nodes.find(edge.first);
            const auto sink = nodes.find(edge.second);
            if (source == nodes.end() || sink == nodes.end()) {
                throw std::runtime_error(
                    "The circuit contains an edge whose endpoint is not in a logic layer: " +
                    std::to_string(edge.first) + " -> " + std::to_string(edge.second) + ".");
            }
            if (agedge(resources.graph, source->second, sink->second, nullptr, TRUE) == nullptr) {
                throw std::runtime_error(
                    "Graphviz failed to create edge " + std::to_string(edge.first) + " -> " +
                    std::to_string(edge.second) + ".");
            }
        }

        for (std::size_t layerIndex = 0; layerIndex < layerNodes.size(); ++layerIndex) {
            const std::string layerName = "layer" + std::to_string(layerIndex);
            Agraph_t *subgraph = agsubg(resources.graph,
                                        const_cast<char *>(layerName.c_str()), TRUE);
            if (subgraph == nullptr) {
                throw std::runtime_error("Graphviz failed to create rank group " + layerName + ".");
            }
            for (int nodeIndex : layerNodes[layerIndex]) {
                agsubnode(subgraph, nodes.at(nodeIndex), TRUE);
            }
            agsafeset(subgraph, const_cast<char *>("rank"), const_cast<char *>("same"),
                      const_cast<char *>("same"));
        }

        agsafeset(resources.graph, const_cast<char *>("rankdir"), const_cast<char *>("TB"),
                  const_cast<char *>(""));
        agsafeset(resources.graph, const_cast<char *>("nodesep"), const_cast<char *>(".6"),
                  const_cast<char *>(""));
        agsafeset(resources.graph, const_cast<char *>("ranksep"), const_cast<char *>("1"),
                  const_cast<char *>(""));
        if (options.orthogonalEdges) {
            agsafeset(resources.graph, const_cast<char *>("splines"),
                      const_cast<char *>("ortho"), const_cast<char *>(""));
        }

        if (gvLayout(resources.context, resources.graph, "dot") != 0) {
            result.error = "Graphviz DOT could not lay out the circuit.";
            return result;
        }
        resources.hasLayout = true;

        for (const auto &entry : nodes) {
            const pointf coordinate = ND_coord(entry.second);
            if (!std::isfinite(coordinate.x) || !std::isfinite(coordinate.y)) {
                throw std::runtime_error("Graphviz returned a non-finite node coordinate.");
            }
            result.nodePositions[entry.first] = {coordinate.x, coordinate.y};
        }

        GraphvizRenderBuffer svgBuffer;
        unsigned int svgLength = 0;
        if (gvRenderData(resources.context, resources.graph, "svg",
                         &svgBuffer.data, &svgLength) != 0 ||
            svgBuffer.data == nullptr || svgLength == 0) {
            result.error = "Graphviz could not render the DOT layout as SVG.";
            return result;
        }
        result.svg.assign(svgBuffer.data, svgLength);

        result.nodeCount = nodes.size();
        result.edgeCount = edges.size();
        result.success = true;
        return result;
    } catch (const std::exception &ex) {
        result.error = ex.what();
    } catch (...) {
        result.error = "Legacy Graphviz rendering failed with an unknown error.";
    }
    return result;
}

} // namespace fcngraph
