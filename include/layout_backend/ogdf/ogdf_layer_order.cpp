#include <ogdf/basic/Graph.h>
#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/layered/BarycenterHeuristic.h>
#include <ogdf/layered/SugiyamaLayout.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct InputNode {
    std::int64_t id = 0;
    int layer = 0;
    int initialOrder = 0;
    ogdf::node handle = nullptr;
};

} // namespace

int main()
{
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    int nodeCount = 0;
    int edgeCount = 0;
    int layerCount = 0;
    if (!(std::cin >> nodeCount >> edgeCount >> layerCount)
        || nodeCount < 0 || edgeCount < 0 || layerCount < 0) {
        std::cerr << "invalid OGDF layer-order header\n";
        return 2;
    }

    ogdf::Graph graph;
    std::vector<InputNode> nodes;
    nodes.reserve(static_cast<std::size_t>(nodeCount));
    std::unordered_map<std::int64_t, std::size_t> nodeIndex;

    for (int i = 0; i < nodeCount; ++i) {
        InputNode item;
        if (!(std::cin >> item.id >> item.layer >> item.initialOrder)
            || item.layer < 0 || item.layer >= layerCount
            || nodeIndex.find(item.id) != nodeIndex.end()) {
            std::cerr << "invalid OGDF node record at index " << i << '\n';
            return 2;
        }
        item.handle = graph.newNode();
        nodeIndex.emplace(item.id, nodes.size());
        nodes.push_back(item);
    }

    for (int i = 0; i < edgeCount; ++i) {
        std::int64_t source = 0;
        std::int64_t target = 0;
        if (!(std::cin >> source >> target)) {
            std::cerr << "invalid OGDF edge record at index " << i << '\n';
            return 2;
        }
        const auto sourceIt = nodeIndex.find(source);
        const auto targetIt = nodeIndex.find(target);
        if (sourceIt == nodeIndex.end() || targetIt == nodeIndex.end()) {
            continue;
        }
        const InputNode &u = nodes[sourceIt->second];
        const InputNode &v = nodes[targetIt->second];
        if (u.layer >= v.layer) {
            continue;
        }
        graph.newEdge(u.handle, v.handle);
    }

    ogdf::GraphAttributes attributes(
        graph,
        ogdf::GraphAttributes::nodeGraphics | ogdf::GraphAttributes::edgeGraphics);
    ogdf::NodeArray<int> rank(graph, 0);
    for (const InputNode &item : nodes) {
        rank[item.handle] = item.layer;
        attributes.width(item.handle) = 1.0;
        attributes.height(item.handle) = 1.0;
    }

    ogdf::SugiyamaLayout layout;
    ogdf::setSeed(1);
    layout.setCrossMin(new ogdf::BarycenterHeuristic());
    layout.runs(8);
    layout.fails(2);
    layout.transpose(true);
    layout.arrangeCCs(false);
    layout.maxThreads(1);
    layout.permuteFirst(false);

    const auto started = std::chrono::steady_clock::now();
    try {
        layout.call(attributes, rank);
    } catch (const std::exception &error) {
        std::cerr << "OGDF Sugiyama failed: " << error.what() << '\n';
        return 3;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();

    std::vector<std::vector<const InputNode *>> layers(
        static_cast<std::size_t>(layerCount));
    for (const InputNode &item : nodes) {
        layers[static_cast<std::size_t>(item.layer)].push_back(&item);
    }

    std::cout << "OGDF " << std::fixed << std::setprecision(6) << elapsed
              << ' ' << layout.numberOfCrossings() << '\n';
    for (int layer = 0; layer < layerCount; ++layer) {
        auto &layerNodes = layers[static_cast<std::size_t>(layer)];
        std::stable_sort(
            layerNodes.begin(),
            layerNodes.end(),
            [&](const InputNode *left, const InputNode *right) {
                const double leftX = attributes.x(left->handle);
                const double rightX = attributes.x(right->handle);
                if (leftX != rightX) {
                    return leftX < rightX;
                }
                if (left->initialOrder != right->initialOrder) {
                    return left->initialOrder < right->initialOrder;
                }
                return left->id < right->id;
            });
        for (std::size_t order = 0; order < layerNodes.size(); ++order) {
            std::cout << layer << ' ' << order << ' ' << layerNodes[order]->id << '\n';
        }
    }
    return 0;
}
