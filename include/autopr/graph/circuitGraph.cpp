#include "circuitGraph.h"
#include "autopr/algorithms/mapping.h"
#include "autopr/algorithms/phaseSolver.h"
#include "autopr/algorithms/astarwithphase.h"
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <tuple>

namespace fcngraph
{
    namespace
    {
        std::string escapeLatexText(const std::string &text)
        {
            std::string escaped;
            escaped.reserve(text.size());
            for (const char character : text)
            {
                switch (character)
                {
                case '_': escaped += "\\_"; break;
                case '&': escaped += "\\&"; break;
                case '%': escaped += "\\%"; break;
                case '#': escaped += "\\#"; break;
                case '$': escaped += "\\$"; break;
                case '{': escaped += "\\{"; break;
                case '}': escaped += "\\}"; break;
                case '~': escaped += "\\textasciitilde{}"; break;
                case '^': escaped += "\\textasciicircum{}"; break;
                case '\\': escaped += "\\textbackslash{}"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        int phaseAfter(int startPhase, int steps, int phaseCount)
        {
            phaseCount = std::max(2, phaseCount);
            int phase = (startPhase - 1 + steps) % phaseCount;
            if (phase < 0)
            {
                phase += phaseCount;
            }
            return phase + 1;
        }

        int phaseDistance(int from, int to, int phaseCount)
        {
            phaseCount = std::max(2, phaseCount);
            if (from == to)
            {
                return 0;
            }
            if (to > from)
            {
                return to - from;
            }
            return (phaseCount - from) + to;
        }

        bool isForwardPhaseStep(int from, int to, int phaseCount)
        {
            return from == to || phaseAfter(from, 1, phaseCount) == to;
        }

        std::vector<bool> buildWaitSteps(int steps, int waits)
        {
            std::vector<bool> waitSteps(static_cast<std::size_t>(steps) + 1, false);
            for (int waitIndex = 1; waitIndex <= waits; ++waitIndex)
            {
                int pos = (waitIndex * (steps + 1)) / (waits + 1);
                pos = std::max(1, std::min(steps, pos));
                const int preferred = pos;
                while (pos <= steps && waitSteps[static_cast<std::size_t>(pos)])
                {
                    ++pos;
                }
                if (pos > steps)
                {
                    pos = preferred - 1;
                    while (pos >= 1 && waitSteps[static_cast<std::size_t>(pos)])
                    {
                        --pos;
                    }
                }
                if (pos >= 1)
                {
                    waitSteps[static_cast<std::size_t>(pos)] = true;
                }
            }
            return waitSteps;
        }

        std::vector<int> interpolatePhaseSegment(int fromPhase, int toPhase, int steps, int phaseCount)
        {
            if (steps < 0)
            {
                throw std::invalid_argument("Negative phase segment length");
            }

            std::vector<int> phases(static_cast<std::size_t>(steps) + 1, fromPhase);
            int minAdvances = phaseDistance(fromPhase, toPhase, phaseCount);
            if (minAdvances > steps)
            {
                throw std::runtime_error("Phase continuity violation");
            }

            int advances = minAdvances + ((steps - minAdvances) / phaseCount) * phaseCount;
            int waits = steps - advances;
            int usedAdvances = 0;
            const auto waitSteps = buildWaitSteps(steps, waits);

            for (int i = 1; i <= steps; ++i)
            {
                if (waitSteps[static_cast<std::size_t>(i)])
                {
                    phases[static_cast<std::size_t>(i)] = phases[static_cast<std::size_t>(i - 1)];
                }
                else
                {
                    ++usedAdvances;
                    phases[static_cast<std::size_t>(i)] = phaseAfter(fromPhase, usedAdvances, phaseCount);
                }
            }

            return phases;
        }

        std::vector<int> solveForwardPhasePath(const std::vector<int> &fixedPhases,
                                               int phaseCount,
                                               int preferredStartPhase = -1)
        {
            phaseCount = std::max(2, phaseCount);
            if (fixedPhases.empty())
            {
                return {};
            }

            constexpr int kUnreachable = 1000000000;
            const int phaseSlots = phaseCount + 1;
            const auto length = fixedPhases.size();

            std::vector<std::vector<int>> cost(length, std::vector<int>(phaseSlots, kUnreachable));
            std::vector<std::vector<int>> previous(length, std::vector<int>(phaseSlots, -1));

            const auto phaseAllowed = [&fixedPhases](std::size_t index, int phase) {
                return fixedPhases[index] < 1 || fixedPhases[index] == phase;
            };

            auto seedStart = [&](int startPhase) {
                bool seeded = false;
                for (int phase = 1; phase <= phaseCount; ++phase)
                {
                    if (startPhase >= 1 && phase != startPhase)
                    {
                        continue;
                    }
                    if (!phaseAllowed(0, phase))
                    {
                        continue;
                    }
                    cost[0][phase] = 0;
                    seeded = true;
                }
                return seeded;
            };

            bool seeded = seedStart(preferredStartPhase);
            if (!seeded && preferredStartPhase >= 1)
            {
                seeded = seedStart(-1);
            }
            if (!seeded)
            {
                throw std::runtime_error("No valid phase path start");
            }

            for (std::size_t index = 1; index < length; ++index)
            {
                for (int prevPhase = 1; prevPhase <= phaseCount; ++prevPhase)
                {
                    if (cost[index - 1][prevPhase] >= kUnreachable)
                    {
                        continue;
                    }

                    const int candidates[2] = {prevPhase, phaseAfter(prevPhase, 1, phaseCount)};
                    for (int phase : candidates)
                    {
                        if (!phaseAllowed(index, phase))
                        {
                            continue;
                        }
                        const int nextCost = cost[index - 1][prevPhase] + ((phase == prevPhase) ? 1 : 0);
                        if (nextCost < cost[index][phase])
                        {
                            cost[index][phase] = nextCost;
                            previous[index][phase] = prevPhase;
                        }
                    }
                }
            }

            int bestPhase = -1;
            int bestCost = kUnreachable;
            for (int phase = 1; phase <= phaseCount; ++phase)
            {
                if (cost[length - 1][phase] < bestCost)
                {
                    bestCost = cost[length - 1][phase];
                    bestPhase = phase;
                }
            }
            if (bestPhase < 1)
            {
                throw std::runtime_error("No valid phase path");
            }

            std::vector<int> phases(length, 1);
            int phase = bestPhase;
            for (std::size_t offset = 0; offset < length; ++offset)
            {
                const std::size_t index = length - 1 - offset;
                phases[index] = phase;
                phase = previous[index][phase];
                if (index == 0)
                {
                    break;
                }
            }
            return phases;
        }

        bool validateJuneFanoutTrees(
            const std::map<std::pair<unsigned int, unsigned int>,
                           std::vector<position>> &routes,
            std::string &error)
        {
            using LogicalEdge = std::pair<unsigned int, unsigned int>;
            enum class SegmentOrientation
            {
                Horizontal,
                Vertical,
                Bend
            };
            struct RouteUse
            {
                unsigned int source;
                SegmentOrientation orientation;
            };
            std::map<unsigned int,
                     std::map<position, std::map<position, std::set<LogicalEdge>>>>
                incomingBySource;
            std::map<unsigned int, std::map<position, std::set<LogicalEdge>>>
                rootsBySource;
            std::map<position, std::vector<RouteUse>> interiorUses;

            for (const auto &route : routes)
            {
                const LogicalEdge edge = route.first;
                const auto &path = route.second;
                if (path.size() < 2)
                {
                    error = "route " + std::to_string(edge.first) + "->" +
                            std::to_string(edge.second) + " has fewer than two points";
                    return false;
                }

                std::set<position> uniquePositions;
                for (const position &pos : path)
                {
                    if (!uniquePositions.insert(pos).second)
                    {
                        error = "route " + std::to_string(edge.first) + "->" +
                                std::to_string(edge.second) + " repeats a grid coordinate";
                        return false;
                    }
                }

                rootsBySource[edge.first][path.front()].insert(edge);
                for (std::size_t index = 1; index < path.size(); ++index)
                {
                    const position &previous = path[index - 1];
                    const position &current = path[index];
                    const unsigned int distance =
                        (previous.first > current.first ? previous.first - current.first
                                                       : current.first - previous.first) +
                        (previous.second > current.second ? previous.second - current.second
                                                         : current.second - previous.second);
                    if (distance != 1)
                    {
                        error = "route " + std::to_string(edge.first) + "->" +
                                std::to_string(edge.second) + " is not 4-connected";
                        return false;
                    }
                    incomingBySource[edge.first][current][previous].insert(edge);
                }

                for (std::size_t index = 1; index + 1 < path.size(); ++index)
                {
                    const position &previous = path[index - 1];
                    const position &current = path[index];
                    const position &next = path[index + 1];
                    SegmentOrientation orientation = SegmentOrientation::Bend;
                    if (previous.second == current.second &&
                        current.second == next.second)
                    {
                        orientation = SegmentOrientation::Horizontal;
                    }
                    else if (previous.first == current.first &&
                             current.first == next.first)
                    {
                        orientation = SegmentOrientation::Vertical;
                    }
                    interiorUses[current].push_back({edge.first, orientation});
                }

            }

            for (const auto &sourceRoots : rootsBySource)
            {
                if (sourceRoots.second.size() != 1)
                {
                    error = "fanout source " + std::to_string(sourceRoots.first) +
                            " does not have one shared root";
                    return false;
                }
                const auto incomingSource = incomingBySource.find(sourceRoots.first);
                if (incomingSource == incomingBySource.end())
                {
                    continue;
                }
                for (const auto &incomingAtPosition : incomingSource->second)
                {
                    if (incomingAtPosition.second.size() > 1)
                    {
                        error = "fanout source " + std::to_string(sourceRoots.first) +
                                " splits and reconverges at (" +
                                std::to_string(incomingAtPosition.first.first) + "," +
                                std::to_string(incomingAtPosition.first.second) + ")";
                        return false;
                    }
                }
            }


            for (const auto &usesAtPosition : interiorUses)
            {
                std::map<unsigned int, std::set<SegmentOrientation>> orientationsBySource;
                for (const RouteUse &use : usesAtPosition.second)
                {
                    orientationsBySource[use.source].insert(use.orientation);
                }
                if (orientationsBySource.size() <= 1)
                {
                    continue;
                }
                if (orientationsBySource.size() != 2)
                {
                    error = "more than two source trees share (" +
                            std::to_string(usesAtPosition.first.first) + "," +
                            std::to_string(usesAtPosition.first.second) + ")";
                    return false;
                }

                const auto first = orientationsBySource.begin();
                const auto second = std::next(first);
                const bool firstIsHorizontal =
                    first->second.size() == 1 &&
                    first->second.count(SegmentOrientation::Horizontal) == 1;
                const bool firstIsVertical =
                    first->second.size() == 1 &&
                    first->second.count(SegmentOrientation::Vertical) == 1;
                const bool secondIsHorizontal =
                    second->second.size() == 1 &&
                    second->second.count(SegmentOrientation::Horizontal) == 1;
                const bool secondIsVertical =
                    second->second.size() == 1 &&
                    second->second.count(SegmentOrientation::Vertical) == 1;
                if (!((firstIsHorizontal && secondIsVertical) ||
                      (firstIsVertical && secondIsHorizontal)))
                {
                    error = "source trees overlap without one straight H/V crossing at (" +
                            std::to_string(usesAtPosition.first.first) + "," +
                            std::to_string(usesAtPosition.first.second) + ")";
                    return false;
                }

                // Distinct straight H/V crossings between the same two source
                // trees are electrically separate crossover cells.  They are
                // legal; only a repeated/continuous overlap on one route is
                // rejected by the path-aware router and the checks above.
            }

            error.clear();
            return true;
        }

    }

    void CircuitGraph::setFitnessCallback(const std::function<void(std::string)> &callback)
    {
        fitnessCallback = callback;
    }

    void CircuitGraph::setStageCallback(
        const std::function<void(const std::string&)> &callback)
    {
        stageCallback = callback;
    }

    void CircuitGraph::processAndGenerateGraph(bool printSVG, bool showCircuitLabel, bool isBox, bool isOGD)
    {
        node_positions.clear();
        if (gvc == nullptr)
        {
            gvc = gvContext();
        }
        Agraph_t *A = agopen(const_cast<char *>("G"), Agdirected, nullptr);
        const auto &layerNodes = parse.getlayerNodeDivVec();
        const auto &edges = parse.getEffectiveEdges();

        // 添加节点并设置层次
        std::map<int, Agnode_t *> node_map;
        for (const auto &layer : layerNodes)
        {
            for (const auto &node_index : layer)
            {
                std::string node_str = std::to_string(node_index);
                Agnode_t *node = agnode(A, const_cast<char *>(node_str.c_str()), 1);
                node_map[node_index] = node;
            }
        }

        // 添加边到图中
        for (const auto &edge : edges)
        {
            Agnode_t *node1 = node_map[edge.first];
            Agnode_t *node2 = node_map[edge.second];
            agedge(A, node1, node2, nullptr, 1);
        }

        for (size_t i = 0; i < layerNodes.size(); ++i)
        {
            Agraph_t *subgraph = agsubg(A, const_cast<char *>(("layer" + std::to_string(i)).c_str()), TRUE);
            for (int node : layerNodes[i])
            {
                agsubnode(subgraph, node_map[node], TRUE);
            }
            agsafeset(subgraph, const_cast<char *>("rank"), const_cast<char *>("same"), const_cast<char *>("same"));
        }

        // 设置节点属性 fanout和扇出位点为点，其他为方块
        for (auto &pair : node_map)
        {
            Agnode_t *node = pair.second;
            int node_index = pair.first;
            std::string node_type = parse.getNodeType(node_index);
            if (isBox)
            {
                agsafeset(node, const_cast<char *>("shape"), const_cast<char *>("box"), const_cast<char *>(""));
            }
            // agsafeset(node, const_cast<char*>("shape"), const_cast<char*>("box"), const_cast<char*>(""));
            agsafeset(node, const_cast<char *>("width"), const_cast<char *>("0.5"), const_cast<char *>(""));
            agsafeset(node, const_cast<char *>("height"), const_cast<char *>("0.5"), const_cast<char *>(""));
            agsafeset(node, const_cast<char *>("style"), const_cast<char *>("filled"), const_cast<char *>(""));
            agsafeset(node, const_cast<char *>("fillcolor"), const_cast<char *>("#87ceeb"), const_cast<char *>(""));
            agsafeset(node, const_cast<char *>("fixedsize"), const_cast<char *>("true"), const_cast<char *>(""));
            // agsafeset(node, const_cast<char*>("label"), const_cast<char*>(""), const_cast<char*>(""));

            if (node_type == "not")
            {
                agsafeset(node, const_cast<char *>("color"), const_cast<char *>("red"), const_cast<char *>(""));
            }
            else if (node_type == "fanout")
            {
                agsafeset(node, const_cast<char *>("color"), const_cast<char *>("green"), const_cast<char *>(""));
            }
            else if (node_type == "wire")
            {
                agsafeset(node, const_cast<char *>("color"), const_cast<char *>("green"), const_cast<char *>(""));
            }
            else
            {
                agsafeset(node, const_cast<char *>("color"), const_cast<char *>("black"), const_cast<char *>(""));
            }

            if (showCircuitLabel)
            {
                if (node_type == "and")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("&"), const_cast<char *>(""));
                }
                else if (node_type == "or")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("|"), const_cast<char *>(""));
                }
                else if (node_type == "maj")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("M"), const_cast<char *>(""));
                }
                else if (node_type == "not")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("~"), const_cast<char *>(""));
                }
                else if (node_type == "fanout")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("F"), const_cast<char *>(""));
                }
                else if (node_type == "wire")
                {
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>("W"), const_cast<char *>(""));
                }
                else if (node_type == "input")
                {
                    std::string inputName = "I" + std::to_string(node_index);
                    agsafeset(node, const_cast<char *>("label"), const_cast<char *>(inputName.c_str()), const_cast<char *>(""));
                }
                else
                {
                    continue;
                }
            }
        }

        // 设置边属性
        for (Agedge_t *edge = agfstedge(A, agfstnode(A)); edge; edge = agnxtedge(A, edge, agfstnode(A)))
        {
            agsafeset(edge, const_cast<char *>("splines"), const_cast<char *>("polyline"), const_cast<char *>(""));
        }

        // 设置图形属性                                               TB
        agsafeset(A, const_cast<char *>("rankdir"), const_cast<char *>("TB"), const_cast<char *>(""));
        if (isOGD)
        {
            agsafeset(A, const_cast<char *>("splines"), const_cast<char *>("ortho"), const_cast<char *>(""));
        }
        // agsafeset(A, const_cast<char*>("splines"), const_cast<char*>("ortho"), const_cast<char*>(""));
        agsafeset(A, const_cast<char *>("nodesep"), const_cast<char *>(".6"), const_cast<char *>(""));
        agsafeset(A, const_cast<char *>("ranksep"), const_cast<char *>("1"), const_cast<char *>(""));

        // 进行布局计算
        gvLayout(gvc, A, "dot");

        // 获取每个节点的坐标,svg图中的坐标
        for (const auto &node_graphNode : node_map)
        {
            pointf pos = ND_coord(node_graphNode.second);
            auto position = std::make_pair(static_cast<double>(pos.x), static_cast<double>(pos.y));
            node_positions.push_back({node_graphNode.first, position});
        }

        if (printSVG)
        {
            // 将图形输出为SVG格式的字符串
            std::string moduleName = fileName + ".svg";
            FILE *fp = fopen(moduleName.c_str(), "w");
            if (fp)
            {
                gvRender(gvc, A, "svg", fp);
                fclose(fp);
            }
            else
            {
                std::cerr << "Error: Could not open file " << moduleName << " for writing." << std::endl;
            }
        }

        gvFreeLayout(gvc, A);
        gvFreeContext(gvc);
        gvc = nullptr;
        agclose(A);
    }

    void CircuitGraph::sortNodesByYThenXCoordinate(double grid_size,
                                                   double yGridSize)
    {
        grid_positions.clear();
        nodeIndex_pos.clear();
        sorted_grid_positions.clear();

        if (grid_size <= 0.0)
        {
            grid_size = 30.0;
        }
        if (yGridSize <= 0.0)
        {
            yGridSize = grid_size;
        }

        struct QuantizedNode
        {
            int node = -1;
            double rawX = 0.0;
            unsigned int gridX = 0;
            unsigned int gridY = 0;
        };
        std::vector<QuantizedNode> quantizedNodes;
        quantizedNodes.reserve(node_positions.size());
        for (const auto &nodePosition : node_positions)
        {
            quantizedNodes.push_back({
                nodePosition.first,
                nodePosition.second.first,
                static_cast<unsigned int>(std::max(
                    0, static_cast<int>(std::round(nodePosition.second.first / grid_size)) + 20)),
                static_cast<unsigned int>(std::max(
                    0, static_cast<int>(std::round(nodePosition.second.second / yGridSize)) + 20))
            });
        }
        // Graphviz reports Cartesian coordinates (Y grows upward), whereas
        // iFCN routing, IFCN files, and the Qt scene use top-left coordinates
        // (Y grows downward).  Normalize once at placement ingestion so every
        // later algorithmic stage shares one orientation and final export
        // never needs to rotate or reflect a completed layout.
        if (!quantizedNodes.empty())
        {
            unsigned int minGridY = quantizedNodes.front().gridY;
            unsigned int maxGridY = quantizedNodes.front().gridY;
            for (const QuantizedNode &node : quantizedNodes)
            {
                minGridY = std::min(minGridY, node.gridY);
                maxGridY = std::max(maxGridY, node.gridY);
            }
            for (QuantizedNode &node : quantizedNodes)
            {
                node.gridY = minGridY + (maxGridY - node.gridY);
            }
        }
        // Resolve quantisation collisions in Graphviz X order.  The historical
        // node-index iteration could push a logically left node past a right
        // node, creating avoidable crossings and width.  Ordered insertion
        // preserves the DOT row order while an occupancy set removes the old
        // O(V^2) scan.
        std::stable_sort(quantizedNodes.begin(), quantizedNodes.end(),
            [](const QuantizedNode &left, const QuantizedNode &right) {
                if (left.gridY != right.gridY)
                {
                    return left.gridY < right.gridY;
                }
                if (left.rawX != right.rawX)
                {
                    return left.rawX < right.rawX;
                }
                return left.node < right.node;
            });
        std::set<position> occupiedPositions;
        for (const QuantizedNode &node : quantizedNodes)
        {
            position candidate{node.gridX, node.gridY};
            while (occupiedPositions.count(candidate) != 0)
            {
                ++candidate.first;
            }
            occupiedPositions.insert(candidate);
            grid_positions[node.node] = candidate;
            nodeIndex_pos[node.node] = candidate;
        }

        // Coordinates now follow the native iFCN top-left convention.
        alignPrimaryIoToBoundaryRows(false);


        // 将 grid_positions 转换为 sorted_grid_positions
        sorted_grid_positions.assign(grid_positions.begin(), grid_positions.end());

        // 先按照 y 从大到小，然后按照 x 从小到大排序
        std::sort(sorted_grid_positions.begin(), sorted_grid_positions.end(),
                  [](const std::pair<int, std::pair<int, int>> &a,
                     const std::pair<int, std::pair<int, int>> &b)
                  {
                      if (a.second.second == b.second.second)
                      {                                           // 如果 y 坐标相同
                          return a.second.first < b.second.first; // 按 x 从小到大排序
                      }
                      return a.second.second > b.second.second; // 否则按 y 从大到小排序
                  });
    }

    void CircuitGraph::sortNodesByLayeredGrid(unsigned int xSpacing,
                                              unsigned int ySpacing,
                                              unsigned int xPadding,
                                              unsigned int yPadding,
                                              bool reverseWithinLayer)
    {
        grid_positions.clear();
        nodeIndex_pos.clear();
        sorted_grid_positions.clear();

        xSpacing = std::max(1u, xSpacing);
        ySpacing = std::max(1u, ySpacing);

        std::map<int, double> graphvizXByNode;
        for (const auto &nodePos : node_positions)
        {
            graphvizXByNode[nodePos.first] = nodePos.second.first;
        }

        const auto &layerNodes = parse.getlayerNodeDivVec();
        std::size_t maxLayerWidth = 0;
        for (const auto &layer : layerNodes)
        {
            maxLayerWidth = std::max(maxLayerWidth, layer.size());
        }

        std::set<position> occupied;
        for (std::size_t layerIndex = 0; layerIndex < layerNodes.size(); ++layerIndex)
        {
            std::vector<int> nodes(layerNodes[layerIndex].begin(), layerNodes[layerIndex].end());
            std::sort(nodes.begin(), nodes.end(), [this, &graphvizXByNode, reverseWithinLayer](int lhs, int rhs) {
                const double lx = graphvizXByNode.count(lhs) ? graphvizXByNode[lhs] : static_cast<double>(lhs);
                const double rx = graphvizXByNode.count(rhs) ? graphvizXByNode[rhs] : static_cast<double>(rhs);
                const auto isPrimaryIo = [this](int node) {
                    const std::string type = parse.getNodeType(node);
                    return type == "input" || type == "output";
                };
                const bool reverse = reverseWithinLayer
                    && !isPrimaryIo(lhs) && !isPrimaryIo(rhs);
                if (lx == rx)
                {
                    return reverse ? lhs > rhs : lhs < rhs;
                }
                return reverse ? lx > rx : lx < rx;
            });

            const unsigned int layerOffset = static_cast<unsigned int>(
                ((maxLayerWidth > nodes.size()) ? (maxLayerWidth - nodes.size()) : 0) * xSpacing / 2);
            for (std::size_t nodeOffset = 0; nodeOffset < nodes.size(); ++nodeOffset)
            {
                position candidate{
                    xPadding + layerOffset + static_cast<unsigned int>(nodeOffset) * xSpacing,
                    yPadding + static_cast<unsigned int>(layerIndex) * ySpacing
                };
                while (occupied.find(candidate) != occupied.end())
                {
                    ++candidate.first;
                }
                occupied.insert(candidate);
                grid_positions[nodes[nodeOffset]] = candidate;
                nodeIndex_pos[nodes[nodeOffset]] = candidate;
            }
        }

        alignPrimaryIoToBoundaryRows(false);

        sorted_grid_positions.assign(grid_positions.begin(), grid_positions.end());
        std::sort(sorted_grid_positions.begin(), sorted_grid_positions.end(),
                  [](const std::pair<int, position> &a,
                     const std::pair<int, position> &b)
                  {
                      if (a.second.second == b.second.second)
                      {
                          return a.second.first < b.second.first;
                      }
                      return a.second.second < b.second.second;
                  });
    }

    void CircuitGraph::sortNodesByElasticLayeredGrid(
        unsigned int xSpacing,
        unsigned int ySlack,
        unsigned int xPadding,
        unsigned int yPadding,
        bool reverseWithinLayer)
    {
        grid_positions.clear();
        nodeIndex_pos.clear();
        sorted_grid_positions.clear();

        xSpacing = std::max(1u, xSpacing);
        ySlack = std::max(1u, ySlack);

        std::map<int, double> graphvizXByNode;
        for (const auto &nodePos : node_positions)
        {
            graphvizXByNode[nodePos.first] = nodePos.second.first;
        }

        const auto &layerNodes = parse.getlayerNodeDivVec();
        std::size_t maxLayerWidth = 0;
        for (const auto &layer : layerNodes)
        {
            maxLayerWidth = std::max(maxLayerWidth, layer.size());
        }

        std::set<position> occupied;
        unsigned int previousLayerMaxY = yPadding;
        for (std::size_t layerIndex = 0;
             layerIndex < layerNodes.size(); ++layerIndex)
        {
            std::vector<int> nodes(
                layerNodes[layerIndex].begin(), layerNodes[layerIndex].end());
            std::sort(nodes.begin(), nodes.end(),
                [this, &graphvizXByNode, reverseWithinLayer](int lhs, int rhs) {
                    const double lx = graphvizXByNode.count(lhs)
                        ? graphvizXByNode[lhs]
                        : static_cast<double>(lhs);
                    const double rx = graphvizXByNode.count(rhs)
                        ? graphvizXByNode[rhs]
                        : static_cast<double>(rhs);
                    const bool lhsIo = parse.getNodeType(lhs) == "input" ||
                                       parse.getNodeType(lhs) == "output";
                    const bool rhsIo = parse.getNodeType(rhs) == "input" ||
                                       parse.getNodeType(rhs) == "output";
                    const bool reverse = reverseWithinLayer && !lhsIo && !rhsIo;
                    if (lx == rx)
                    {
                        return reverse ? lhs > rhs : lhs < rhs;
                    }
                    return reverse ? lx > rx : lx < rx;
                });

            const unsigned int layerBaseY = layerIndex == 0
                ? yPadding
                : previousLayerMaxY + 1;
            unsigned int currentLayerMaxY = layerBaseY;
            const unsigned int regularOffset = static_cast<unsigned int>(
                ((maxLayerWidth > nodes.size())
                    ? (maxLayerWidth - nodes.size())
                    : 0) * xSpacing / 2);

            for (std::size_t nodeOffset = 0;
                 nodeOffset < nodes.size(); ++nodeOffset)
            {
                const int node = nodes[nodeOffset];
                const bool primaryInput = parse.getNodeType(node) == "input";
                const unsigned int portPressure = static_cast<unsigned int>(
                    parse.getFaninsIndex(static_cast<unsigned int>(node)).size() +
                    2 * parse.getFanoutsIndex(static_cast<unsigned int>(node)).size());
                const unsigned int verticalOffset =
                    layerIndex == 0 || primaryInput || ySlack == 1
                        ? 0
                        : static_cast<unsigned int>(
                              (nodeOffset + portPressure) % ySlack);
                unsigned int targetY = layerBaseY + verticalOffset;

                // Respect every already placed predecessor.  This is the
                // only vertical layer constraint; peers in this logical
                // layer deliberately retain independent physical Y values.
                for (const std::uint64_t predecessor :
                     parse.getFaninsIndex(static_cast<unsigned int>(node)))
                {
                    const auto placed = nodeIndex_pos.find(
                        static_cast<int>(predecessor));
                    if (placed != nodeIndex_pos.end() &&
                        parse.getVertexLayer(static_cast<int>(predecessor)) <
                            parse.getVertexLayer(node))
                    {
                        targetY = std::max(
                            targetY, placed->second.second + 1);
                    }
                }

                const unsigned int regularX =
                    xPadding + regularOffset +
                    static_cast<unsigned int>(nodeOffset) * xSpacing;
                std::vector<unsigned int> predecessorX;
                for (const std::uint64_t predecessor :
                     parse.getFaninsIndex(static_cast<unsigned int>(node)))
                {
                    const auto placed = nodeIndex_pos.find(
                        static_cast<int>(predecessor));
                    if (placed != nodeIndex_pos.end())
                    {
                        predecessorX.push_back(placed->second.first);
                    }
                }
                unsigned int desiredX = regularX;
                if (!predecessorX.empty())
                {
                    std::sort(predecessorX.begin(), predecessorX.end());
                    const unsigned int medianX =
                        predecessorX[predecessorX.size() / 2];
                    desiredX = static_cast<unsigned int>(
                        (static_cast<std::uint64_t>(regularX) +
                         2 * static_cast<std::uint64_t>(medianX)) / 3);
                }

                position candidate{desiredX, targetY};
                for (unsigned int radius = 0;
                     occupied.count(candidate) != 0;
                     ++radius)
                {
                    const unsigned int distance = radius + 1;
                    if (desiredX >= distance &&
                        occupied.count({desiredX - distance, targetY}) == 0)
                    {
                        candidate = {desiredX - distance, targetY};
                    }
                    else
                    {
                        candidate = {desiredX + distance, targetY};
                    }
                }

                occupied.insert(candidate);
                grid_positions[node] = candidate;
                nodeIndex_pos[node] = candidate;
                currentLayerMaxY = std::max(currentLayerMaxY, candidate.second);
            }
            previousLayerMaxY = currentLayerMaxY;
        }

        alignPrimaryIoToBoundaryRows(false);

        sorted_grid_positions.assign(
            grid_positions.begin(), grid_positions.end());
        std::sort(sorted_grid_positions.begin(), sorted_grid_positions.end(),
            [](const std::pair<int, position> &left,
               const std::pair<int, position> &right) {
                if (left.second.second != right.second.second)
                {
                    return left.second.second < right.second.second;
                }
                if (left.second.first != right.second.first)
                {
                    return left.second.first < right.second.first;
                }
                return left.first < right.first;
            });
    }

    void CircuitGraph::sortNodesByFixedLayerOrder(const std::vector<std::vector<int>> &orderedLayers,
                                                   unsigned int xSpacing,
                                                   unsigned int ySpacing,
                                                   unsigned int xPadding,
                                                   unsigned int yPadding)
    {
        grid_positions.clear();
        nodeIndex_pos.clear();
        sorted_grid_positions.clear();

        xSpacing = std::max(1u, xSpacing);
        ySpacing = std::max(1u, ySpacing);
        const auto &parserLayers = parse.getlayerNodeDivVec();
        std::vector<std::vector<int>> fallbackLayers;
        const std::vector<std::vector<int>> *layersPtr = &orderedLayers;
        if (orderedLayers.size() != parserLayers.size())
        {
            fallbackLayers.reserve(parserLayers.size());
            for (const auto &layer : parserLayers)
            {
                fallbackLayers.emplace_back(layer.begin(), layer.end());
            }
            layersPtr = &fallbackLayers;
        }
        const auto &layers = *layersPtr;

        std::size_t maxLayerWidth = 0;
        for (const auto &layer : layers)
        {
            maxLayerWidth = std::max(maxLayerWidth, layer.size());
        }

        std::set<position> occupied;
        for (std::size_t layerIndex = 0; layerIndex < layers.size(); ++layerIndex)
        {
            const auto &nodes = layers[layerIndex];
            const unsigned int layerOffset = static_cast<unsigned int>(
                ((maxLayerWidth > nodes.size()) ? (maxLayerWidth - nodes.size()) : 0) * xSpacing / 2);
            for (std::size_t nodeOffset = 0; nodeOffset < nodes.size(); ++nodeOffset)
            {
                position candidate{
                    xPadding + layerOffset + static_cast<unsigned int>(nodeOffset) * xSpacing,
                    yPadding + static_cast<unsigned int>(layerIndex) * ySpacing
                };
                while (occupied.count(candidate) != 0)
                {
                    ++candidate.first;
                }
                occupied.insert(candidate);
                grid_positions[nodes[nodeOffset]] = candidate;
                nodeIndex_pos[nodes[nodeOffset]] = candidate;
            }
        }

        alignPrimaryIoToBoundaryRows(false);

        sorted_grid_positions.assign(grid_positions.begin(), grid_positions.end());
        std::sort(sorted_grid_positions.begin(), sorted_grid_positions.end(),
                  [](const std::pair<int, position> &left,
                     const std::pair<int, position> &right) {
                      if (left.second.second != right.second.second)
                      {
                          return left.second.second < right.second.second;
                      }
                      return left.second.first < right.second.first;
                  });
    }

    void CircuitGraph::alignPrimaryIoToBoundaryRows(bool yAxisPointsUp)
    {
        if (nodeIndex_pos.empty())
        {
            return;
        }

        unsigned int minY = std::numeric_limits<unsigned int>::max();
        unsigned int maxY = 0;
        std::vector<int> inputNodes;
        std::vector<int> outputNodes;
        const auto &primaryOutputs = parse.getOutputNodesIndex();
        for (const auto &node : nodeIndex_pos)
        {
            minY = std::min(minY, node.second.second);
            maxY = std::max(maxY, node.second.second);
            if (parse.getNodeType(node.first) == "input")
            {
                inputNodes.push_back(node.first);
            }
            if (primaryOutputs.count(static_cast<unsigned int>(node.first)) != 0 &&
                parse.getFanoutsIndex(static_cast<unsigned int>(node.first)).empty())
            {
                outputNodes.push_back(node.first);
            }
        }

        const unsigned int inputY = yAxisPointsUp ? maxY : minY;
        const unsigned int outputY = yAxisPointsUp ? minY : maxY;
        std::set<position> occupied;
        for (const auto &node : nodeIndex_pos)
        {
            occupied.insert(node.second);
        }
        const auto moveGroup = [&](const std::vector<int> &nodes, unsigned int boundaryY) {
            std::vector<int> ordered = nodes;
            std::stable_sort(ordered.begin(), ordered.end(), [&](int left, int right) {
                const position leftPos = nodeIndex_pos.at(left);
                const position rightPos = nodeIndex_pos.at(right);
                return leftPos.first == rightPos.first ? left < right
                                                      : leftPos.first < rightPos.first;
            });
            for (int node : ordered)
            {
                const position old = nodeIndex_pos.at(node);
                occupied.erase(old);
                position candidate{old.first, boundaryY};
                // Preserve the Graphviz/fixed-layer horizontal order while
                // resolving a rare collision with an internal boundary node.
                while (occupied.count(candidate) != 0)
                {
                    ++candidate.first;
                }
                nodeIndex_pos[node] = candidate;
                occupied.insert(candidate);
            }
        };
        moveGroup(inputNodes, inputY);
        moveGroup(outputNodes, outputY);
        grid_positions = nodeIndex_pos;
    }

    bool CircuitGraph::placeAndRoute(int shuffledRouteOrderRetries)
    {
        return placeAndRouteInternal(
            shuffledRouteOrderRetries, nullptr, true, 6);
    }

    bool CircuitGraph::validateLegacyMappedLayout()
    {
            unsigned int inputRow = std::numeric_limits<unsigned int>::max();
            unsigned int outputRow = 0;
            bool hasInput = false;
            bool hasOutput = false;
            const auto &primaryOutputs = parse.getOutputNodesIndex();
            const auto isBoundaryOutput = [this, &primaryOutputs](int node) {
                return primaryOutputs.count(static_cast<unsigned int>(node)) != 0 &&
                       parse.getFanoutsIndex(static_cast<unsigned int>(node)).empty();
            };

            for (const auto &node : nodeIndex_pos)
            {
                if (parse.getNodeType(node.first) == "input")
                {
                    inputRow = std::min(inputRow, node.second.second);
                    hasInput = true;
                }
                if (isBoundaryOutput(node.first))
                {
                    outputRow = std::max(outputRow, node.second.second);
                    hasOutput = true;
                }
            }

            if (!hasInput || !hasOutput)
            {
                return true;
            }

            for (const auto &route : routes)
            {
                for (const position &cell : route.second)
                {
                    if (cell.second < inputRow || cell.second > outputRow)
                    {
                        return false;
                    }
                }
            }

            NodeLinkMap nodeLinks;
            for (const auto &node : nodeIndex_pos)
            {
                nodeLinks.try_emplace(
                    std::make_pair(node.second, parse.getNodeType(node.first)),
                    std::make_pair(std::vector<position>{},
                                   std::vector<position>{}));
            }

            std::map<unsigned int, std::set<position>> sinkPorts;
            // Keep the physical sink-port ownership separate from the
            // version 1.1 mapper.  A routed branch must never run through
            // the port cell of another net: the mapper quite correctly
            // interprets that cell as the gate's input connection, while
            // the router would otherwise treat it as an ordinary crossover
            // cell.  This is especially important for a fanout trunk (for
            // example 6->7/6->8) crossing the sink port of 1->8.
            std::map<position, std::vector<std::pair<unsigned int, unsigned int>>>
                sinkPortOwners;
            struct RouteRecord
            {
                std::pair<unsigned int, unsigned int> key;
                const std::vector<position> *path;
            };
            std::vector<RouteRecord> routeRecords;
            std::vector<std::vector<position>> routeLines;
            routeLines.reserve(routes.size());
            for (const auto &route : routes)
            {
                const auto &path = route.second;
                if (path.size() < 2)
                {
                    return false;
                }

                const unsigned int source = route.first.first;
                const unsigned int sink = route.first.second;
                const position sourcePos = nodeIndex_pos.at(static_cast<int>(source));
                const position sinkPos = nodeIndex_pos.at(static_cast<int>(sink));
                const position sourcePort = path[1];
                const position sinkPort = path[path.size() - 2];

                // Different fanins must not collapse onto the same physical
                // neighbor of a gate. Mapping deduplicates those neighbors,
                // which otherwise produces a partial AND/MAJ template.
                if (!sinkPorts[sink].insert(sinkPort).second)
                {
                    return false;
                }

                sinkPortOwners[sinkPort].push_back(route.first);
                routeRecords.push_back({route.first, &path});

                nodeLinks[{sourcePos, parse.getNodeType(static_cast<int>(source))}]
                    .second.push_back(sourcePort);
                nodeLinks[{sinkPos, parse.getNodeType(static_cast<int>(sink))}]
                    .first.push_back(sinkPort);
                routeLines.push_back(path);
            }

            // Reject direct source-port/sink-port reuse.  Ordinary interior
            // crossings, including a route passing through a fanout trunk,
            // remain the responsibility of the version1.1 crossover mapper.
            // This keeps the targeted 1->8 versus 6->7/6->8 repair without
            // forcing unrelated legal crossings into large detours.
            for (const auto &record : routeRecords)
            {
                const auto &path = *record.path;
                if (path.size() < 2)
                {
                    return false;
                }
                constexpr std::size_t sourcePortIndex = 1;
                if (path.size() <= sourcePortIndex + 1)
                {
                    continue;
                }
                const auto owners = sinkPortOwners.find(path[sourcePortIndex]);
                if (owners != sinkPortOwners.end())
                {
                    for (const auto &owner : owners->second)
                    {
                        if (owner != record.key)
                        {
                            return false;
                        }
                    }
                }

            }

            for (auto &node : nodeLinks)
            {
                auto &inputs = node.second.first;
                std::sort(inputs.begin(), inputs.end());
                inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
                auto &outputs = node.second.second;
                std::sort(outputs.begin(), outputs.end());
                outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
            }

            Mapping cellMapping;
            cellMapping.node_mapping(nodeLinks);
            cellMapping.mapping_line(routeLines);
            std::string mappingError;
            return cellMapping.validate_crossovers(&mappingError);
    }

    bool CircuitGraph::placeAndRouteLegacyFast()
    {
        const auto acceptsLegacyMapping = [this]() {
            return validateLegacyMappedLayout();
        };

        // Candidate generation is intentionally bounded: the controller
        // explores many elastic and regular seeds, then performs the costly
        // transactional single-node refinement only on the winner.  Seven
        // deterministic route policies plus eight seeded repairs retain the
        // version1.2 route-order coverage without compacting every seed.
        if (!placeAndRouteInternal(
                8, nullptr, true, 7, acceptsLegacyMapping))
        {
            return false;
        }
        return validateLegacyMappedLayout();
    }

    bool CircuitGraph::refineLegacyMappedLayout(
        int phaseCount,
        int maxRounds,
        int maxEvaluatedMoves,
        int shuffledRouteOrderRetries)
    {
        struct Snapshot
        {
            std::map<int, position> nodes;
            std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routedEdges;
            std::unordered_map<position, GridCell, PositionHash> cells;
        };
        struct LayoutScore
        {
            int area = std::numeric_limits<int>::max();
            int usedCells = std::numeric_limits<int>::max();
            int routeLength = std::numeric_limits<int>::max();
            int bends = std::numeric_limits<int>::max();
            int centerDistance = std::numeric_limits<int>::max();
            int maxDimension = std::numeric_limits<int>::max();
        };
        struct MoveCandidate
        {
            int node = -1;
            position target{0, 0};
            bool absorbsOwnWire = false;
            bool shrinksNodeBoundary = false;
            int centerImprovement = 0;
            int degree = 0;
        };

        phaseCount = std::max(2, phaseCount);
        maxRounds = std::max(0, maxRounds);
        maxEvaluatedMoves = std::max(0, maxEvaluatedMoves);
        shuffledRouteOrderRetries = std::max(0, shuffledRouteOrderRetries);
        if (nodeIndex_pos.empty() || routes.empty())
        {
            return false;
        }

        grid_positions = nodeIndex_pos;
        if (!validateLegacyMappedLayout())
        {
            return false;
        }

        const auto takeSnapshot = [this]() {
            return Snapshot{nodeIndex_pos, routes, chessboard.gridMap};
        };
        const auto restore = [this](const Snapshot &snapshot) {
            nodeIndex_pos = snapshot.nodes;
            grid_positions = snapshot.nodes;
            routes = snapshot.routedEdges;
            chessboard.gridMap = snapshot.cells;
            astar.reset();
        };

        unsigned int anchorMinX = std::numeric_limits<unsigned int>::max();
        unsigned int anchorMinY = std::numeric_limits<unsigned int>::max();
        unsigned int anchorMaxX = 0;
        unsigned int anchorMaxY = 0;
        for (const auto &node : nodeIndex_pos)
        {
            anchorMinX = std::min(anchorMinX, node.second.first);
            anchorMinY = std::min(anchorMinY, node.second.second);
            anchorMaxX = std::max(anchorMaxX, node.second.first);
            anchorMaxY = std::max(anchorMaxY, node.second.second);
        }
        const std::uint64_t anchorCenterXTwice =
            static_cast<std::uint64_t>(anchorMinX) + anchorMaxX;
        const std::uint64_t anchorCenterYTwice =
            static_cast<std::uint64_t>(anchorMinY) + anchorMaxY;
        const auto doubledDistance = [](unsigned int coordinate,
                                        std::uint64_t centerTwice) {
            const std::uint64_t doubled = static_cast<std::uint64_t>(coordinate) * 2;
            return static_cast<int>(doubled >= centerTwice
                ? doubled - centerTwice
                : centerTwice - doubled);
        };

        const auto scoreLayout = [this, anchorCenterXTwice,
                                  anchorCenterYTwice, &doubledDistance]() {
            LayoutScore score;
            bool initialized = false;
            unsigned int minX = 0;
            unsigned int minY = 0;
            unsigned int maxX = 0;
            unsigned int maxY = 0;
            std::set<position> used;
            const auto include = [&](const position &pos) {
                used.insert(pos);
                if (!initialized)
                {
                    minX = maxX = pos.first;
                    minY = maxY = pos.second;
                    initialized = true;
                }
                minX = std::min(minX, pos.first);
                minY = std::min(minY, pos.second);
                maxX = std::max(maxX, pos.first);
                maxY = std::max(maxY, pos.second);
            };
            for (const auto &node : nodeIndex_pos)
            {
                include(node.second);
            }
            score.centerDistance = 0;
            for (const auto &node : nodeIndex_pos)
            {
                score.centerDistance += doubledDistance(
                    node.second.first, anchorCenterXTwice);
                score.centerDistance += doubledDistance(
                    node.second.second, anchorCenterYTwice);
            }
            score.routeLength = 0;
            score.bends = 0;
            for (const auto &route : routes)
            {
                const auto &path = route.second;
                score.routeLength += static_cast<int>(path.size());
                for (std::size_t index = 0; index < path.size(); ++index)
                {
                    include(path[index]);
                    if (index < 2)
                    {
                        continue;
                    }
                    const int dx1 = static_cast<int>(path[index - 1].first) -
                                    static_cast<int>(path[index - 2].first);
                    const int dy1 = static_cast<int>(path[index - 1].second) -
                                    static_cast<int>(path[index - 2].second);
                    const int dx2 = static_cast<int>(path[index].first) -
                                    static_cast<int>(path[index - 1].first);
                    const int dy2 = static_cast<int>(path[index].second) -
                                    static_cast<int>(path[index - 1].second);
                    if (dx1 != dx2 || dy1 != dy2)
                    {
                        ++score.bends;
                    }
                }
            }
            if (!initialized)
            {
                return score;
            }
            const int width = static_cast<int>(maxX - minX + 1);
            const int height = static_cast<int>(maxY - minY + 1);
            score.area = width * height;
            score.usedCells = static_cast<int>(used.size());
            score.maxDimension = std::max(width, height);
            return score;
        };

        const auto phaseRunAcceptable = [this, phaseCount]() {
            const int refinementRunCeiling = phaseCount * 2;
            for (const auto &route : routes)
            {
                int previousPhase = -1;
                int samePhaseRun = 0;
                for (const position &cellPos : route.second)
                {
                    const auto cell = chessboard.gridMap.find(cellPos);
                    if (cell == chessboard.gridMap.end() ||
                        cell->second.getPhase() < 1)
                    {
                        return false;
                    }
                    const int phase = cell->second.getPhase();
                    samePhaseRun = phase == previousPhase
                        ? samePhaseRun + 1
                        : 1;
                    if (samePhaseRun > refinementRunCeiling)
                    {
                        return false;
                    }
                    previousPhase = phase;
                }
            }
            return true;
        };

        const auto placementLegal = [this]() {
            std::set<position> occupied;
            unsigned int inputRow = std::numeric_limits<unsigned int>::max();
            unsigned int outputRow = std::numeric_limits<unsigned int>::max();
            bool hasInput = false;
            bool hasOutput = false;
            const auto &primaryOutputs = parse.getOutputNodesIndex();
            const auto isBoundaryOutput = [this, &primaryOutputs](int node) {
                return primaryOutputs.count(static_cast<unsigned int>(node)) != 0 &&
                       parse.getFanoutsIndex(static_cast<unsigned int>(node)).empty();
            };

            for (const auto &node : nodeIndex_pos)
            {
                if (!occupied.insert(node.second).second)
                {
                    return false;
                }
                if (parse.getNodeType(node.first) == "input")
                {
                    if (!hasInput)
                    {
                        inputRow = node.second.second;
                        hasInput = true;
                    }
                    else if (node.second.second != inputRow)
                    {
                        return false;
                    }
                }
                if (isBoundaryOutput(node.first))
                {
                    if (!hasOutput)
                    {
                        outputRow = node.second.second;
                        hasOutput = true;
                    }
                    else if (node.second.second != outputRow)
                    {
                        return false;
                    }
                }
            }

            if (hasInput && hasOutput && inputRow >= outputRow)
            {
                return false;
            }
            for (const auto &node : nodeIndex_pos)
            {
                const bool isInput = parse.getNodeType(node.first) == "input";
                const bool isOutput = isBoundaryOutput(node.first);
                if (hasInput && !isInput && node.second.second <= inputRow)
                {
                    return false;
                }
                if (hasOutput && !isOutput && node.second.second >= outputRow)
                {
                    return false;
                }
            }

            for (const auto &edge : parse.getEffectiveEdges())
            {
                const auto source = nodeIndex_pos.find(edge.first);
                const auto sink = nodeIndex_pos.find(edge.second);
                if (source == nodeIndex_pos.end() || sink == nodeIndex_pos.end())
                {
                    return false;
                }
                if (parse.getVertexLayer(edge.first) < parse.getVertexLayer(edge.second) &&
                    source->second.second >= sink->second.second)
                {
                    return false;
                }
            }
            return true;
        };

        const auto scoreImproved = [](const LayoutScore &candidate,
                                      const LayoutScore &base,
                                      bool requireAreaReduction) {
            if (candidate.area < base.area)
            {
                return true;
            }
            if (requireAreaReduction || candidate.area != base.area)
            {
                return false;
            }
            if (candidate.usedCells < base.usedCells)
            {
                return true;
            }
            if (candidate.usedCells > base.usedCells)
            {
                return false;
            }
            if (candidate.routeLength < base.routeLength)
            {
                return true;
            }
            if (candidate.routeLength > base.routeLength)
            {
                return false;
            }
            if (candidate.bends < base.bends)
            {
                return true;
            }
            if (candidate.bends > base.bends)
            {
                return false;
            }
            if (candidate.centerDistance < base.centerDistance)
            {
                return true;
            }
            return candidate.centerDistance == base.centerDistance &&
                   candidate.maxDimension < base.maxDimension;
        };

        const auto tryCurrentPlacement = [this, phaseCount,
                                          shuffledRouteOrderRetries,
                                          &placementLegal,
                                          &phaseRunAcceptable,
                                          &scoreLayout,
                                          &scoreImproved,
                                          &takeSnapshot,
                                          &restore](const Snapshot &base,
                                                    const LayoutScore &baseScore,
                                                    bool requireAreaReduction) {
            if (!placementLegal())
            {
                restore(base);
                return false;
            }
            grid_positions = nodeIndex_pos;
            const auto acceptsLegacyMapping = [this]() {
                return validateLegacyMappedLayout();
            };
            if (!placeAndRouteInternal(
                    shuffledRouteOrderRetries, nullptr, false, 6,
                    acceptsLegacyMapping) ||
                !assignPhases(phaseCount) ||
                !validateAssignedRoutePhases(phaseCount) ||
                !phaseRunAcceptable())
            {
                restore(base);
                return false;
            }
            const LayoutScore candidateScore = scoreLayout();
            if (!scoreImproved(candidateScore, baseScore, requireAreaReduction))
            {
                restore(base);
                return false;
            }
            return true;
        };

        if (!assignPhases(phaseCount) ||
            !validateAssignedRoutePhases(phaseCount) ||
            !phaseRunAcceptable())
        {
            return false;
        }

        int evaluatedMoves = 0;
        int acceptedMoves = 0;
        const auto &primaryOutputs = parse.getOutputNodesIndex();
        const auto isBoundaryOutput = [this, &primaryOutputs](int node) {
            return primaryOutputs.count(static_cast<unsigned int>(node)) != 0 &&
                   parse.getFanoutsIndex(static_cast<unsigned int>(node)).empty();
        };

        while (acceptedMoves < maxRounds &&
               evaluatedMoves < maxEvaluatedMoves)
        {
            const Snapshot base = takeSnapshot();
            const LayoutScore baseScore = scoreLayout();
            unsigned int minNodeX = std::numeric_limits<unsigned int>::max();
            unsigned int minNodeY = std::numeric_limits<unsigned int>::max();
            unsigned int maxNodeX = 0;
            unsigned int maxNodeY = 0;
            std::set<position> nodeCells;
            for (const auto &node : nodeIndex_pos)
            {
                nodeCells.insert(node.second);
                minNodeX = std::min(minNodeX, node.second.first);
                minNodeY = std::min(minNodeY, node.second.second);
                maxNodeX = std::max(maxNodeX, node.second.first);
                maxNodeY = std::max(maxNodeY, node.second.second);
            }

            std::map<position, std::set<std::pair<unsigned int, unsigned int>>>
                wireOwners;
            for (const auto &route : routes)
            {
                for (std::size_t index = 1;
                     index + 1 < route.second.size(); ++index)
                {
                    wireOwners[route.second[index]].insert(route.first);
                }
            }

            std::vector<MoveCandidate> candidates;
            std::set<std::pair<int, position>> uniqueCandidates;
            const auto addCandidate = [&](int node,
                                          const position &target,
                                          bool absorbsOwnWire) {
                const position current = nodeIndex_pos.at(node);
                if (target == current || nodeCells.count(target) != 0 ||
                    !uniqueCandidates.insert({node, target}).second)
                {
                    return;
                }
                const bool isInput = parse.getNodeType(node) == "input";
                const bool isOutput = isBoundaryOutput(node);
                if ((isInput || isOutput) && target.second != current.second)
                {
                    return;
                }
                if (!absorbsOwnWire && wireOwners.count(target) != 0)
                {
                    return;
                }
                const int oldCenterDistance =
                    doubledDistance(current.first, anchorCenterXTwice) +
                    doubledDistance(current.second, anchorCenterYTwice);
                const int newCenterDistance =
                    doubledDistance(target.first, anchorCenterXTwice) +
                    doubledDistance(target.second, anchorCenterYTwice);
                const bool shrinksBoundary =
                    (current.first == minNodeX && target.first > current.first) ||
                    (current.first == maxNodeX && target.first < current.first) ||
                    (current.second == minNodeY && target.second > current.second) ||
                    (current.second == maxNodeY && target.second < current.second);
                const int degree =
                    static_cast<int>(parse.getFaninsIndex(node).size() +
                                     parse.getFanoutsIndex(node).size());
                candidates.push_back({
                    node, target, absorbsOwnWire, shrinksBoundary,
                    oldCenterDistance - newCenterDistance, degree});
            };

            for (const auto &node : nodeIndex_pos)
            {
                const int nodeIndex = node.first;
                const position current = node.second;
                const bool isInput = parse.getNodeType(nodeIndex) == "input";
                const bool isOutput = isBoundaryOutput(nodeIndex);

                if (current.first > 0)
                {
                    addCandidate(nodeIndex,
                                 {current.first - 1, current.second}, false);
                }
                addCandidate(nodeIndex,
                             {current.first + 1, current.second}, false);
                if (!isInput && !isOutput)
                {
                    if (current.second > 0)
                    {
                        addCandidate(nodeIndex,
                                     {current.first, current.second - 1}, false);
                    }
                    addCandidate(nodeIndex,
                                 {current.first, current.second + 1}, false);
                }

                std::vector<unsigned int> neighborX;
                std::vector<unsigned int> neighborY;
                for (const auto &edge : parse.getEffectiveEdges())
                {
                    int neighbor = -1;
                    if (edge.first == nodeIndex)
                    {
                        neighbor = edge.second;
                    }
                    else if (edge.second == nodeIndex)
                    {
                        neighbor = edge.first;
                    }
                    if (neighbor < 0 || nodeIndex_pos.count(neighbor) == 0)
                    {
                        continue;
                    }
                    neighborX.push_back(nodeIndex_pos.at(neighbor).first);
                    neighborY.push_back(nodeIndex_pos.at(neighbor).second);
                }
                if (!neighborX.empty())
                {
                    std::sort(neighborX.begin(), neighborX.end());
                    std::sort(neighborY.begin(), neighborY.end());
                    const unsigned int medianX = neighborX[neighborX.size() / 2];
                    const unsigned int medianY = neighborY[neighborY.size() / 2];
                    if (medianX != current.first)
                    {
                        addCandidate(nodeIndex,
                            {current.first + (medianX > current.first ? 1u : -1u),
                             current.second}, false);
                    }
                    if (!isInput && !isOutput && medianY != current.second)
                    {
                        addCandidate(nodeIndex,
                            {current.first,
                             current.second + (medianY > current.second ? 1u : -1u)},
                            false);
                    }
                }
            }

            // Moving a gate into a uniquely owned adjacent port cell mirrors
            // the version1.2 Python contraction, but every proposal is
            // rerouted and checked with the version1.1 mapper.
            for (const auto &route : routes)
            {
                if (route.second.size() < 3)
                {
                    continue;
                }
                const position sourceTarget = route.second[1];
                const position sinkTarget = route.second[route.second.size() - 2];
                const auto sourceOwners = wireOwners.find(sourceTarget);
                if (sourceOwners != wireOwners.end() &&
                    sourceOwners->second.size() == 1)
                {
                    addCandidate(static_cast<int>(route.first.first),
                                 sourceTarget, true);
                }
                const auto sinkOwners = wireOwners.find(sinkTarget);
                if (sinkOwners != wireOwners.end() &&
                    sinkOwners->second.size() == 1)
                {
                    addCandidate(static_cast<int>(route.first.second),
                                 sinkTarget, true);
                }
            }

            std::stable_sort(candidates.begin(), candidates.end(),
                [](const MoveCandidate &left, const MoveCandidate &right) {
                    return std::make_tuple(
                               !left.shrinksNodeBoundary,
                               !left.absorbsOwnWire,
                               -left.centerImprovement,
                               -left.degree,
                               left.node,
                               left.target) <
                           std::make_tuple(
                               !right.shrinksNodeBoundary,
                               !right.absorbsOwnWire,
                               -right.centerImprovement,
                               -right.degree,
                               right.node,
                               right.target);
                });

            bool accepted = false;
            for (const MoveCandidate &candidate : candidates)
            {
                if (evaluatedMoves >= maxEvaluatedMoves)
                {
                    break;
                }
                ++evaluatedMoves;
                restore(base);
                nodeIndex_pos[candidate.node] = candidate.target;
                grid_positions = nodeIndex_pos;
                if (tryCurrentPlacement(base, baseScore, false))
                {
                    ++acceptedMoves;
                    accepted = true;
                    break;
                }
            }
            if (!accepted)
            {
                restore(base);
                break;
            }
        }

        // Once nodes have independent coordinates, collapse both empty and
        // compatible occupied rows/columns.  A collision, topology reversal,
        // phase failure, or v1.1 mapping failure rolls the cut back.
        int acceptedCuts = 0;
        while (acceptedCuts < maxRounds &&
               evaluatedMoves < maxEvaluatedMoves)
        {
            const Snapshot base = takeSnapshot();
            const LayoutScore baseScore = scoreLayout();
            unsigned int minX = std::numeric_limits<unsigned int>::max();
            unsigned int minY = std::numeric_limits<unsigned int>::max();
            unsigned int maxX = 0;
            unsigned int maxY = 0;
            for (const auto &node : nodeIndex_pos)
            {
                minX = std::min(minX, node.second.first);
                minY = std::min(minY, node.second.second);
                maxX = std::max(maxX, node.second.first);
                maxY = std::max(maxY, node.second.second);
            }

            struct CutCandidate
            {
                int axis = 0;
                unsigned int line = 0;
                int movedNodes = 0;
            };
            std::vector<CutCandidate> cuts;
            for (int axis = 0; axis < 2; ++axis)
            {
                const unsigned int low = axis == 0 ? minX : minY;
                const unsigned int high = axis == 0 ? maxX : maxY;
                for (unsigned int line = low; line < high; ++line)
                {
                    int movedNodes = 0;
                    for (const auto &node : nodeIndex_pos)
                    {
                        const unsigned int coordinate =
                            axis == 0 ? node.second.first : node.second.second;
                        movedNodes += coordinate > line ? 1 : 0;
                    }
                    cuts.push_back({axis, line, movedNodes});
                }
            }
            std::stable_sort(cuts.begin(), cuts.end(),
                [](const CutCandidate &left, const CutCandidate &right) {
                    return std::make_tuple(left.movedNodes, left.axis,
                                           std::numeric_limits<unsigned int>::max() - left.line) <
                           std::make_tuple(right.movedNodes, right.axis,
                                           std::numeric_limits<unsigned int>::max() - right.line);
                });

            bool accepted = false;
            for (const CutCandidate &cut : cuts)
            {
                if (evaluatedMoves >= maxEvaluatedMoves)
                {
                    break;
                }
                ++evaluatedMoves;
                restore(base);
                for (auto &node : nodeIndex_pos)
                {
                    unsigned int &coordinate = cut.axis == 0
                        ? node.second.first
                        : node.second.second;
                    if (coordinate > cut.line)
                    {
                        --coordinate;
                    }
                }
                grid_positions = nodeIndex_pos;
                if (tryCurrentPlacement(base, baseScore, true))
                {
                    ++acceptedCuts;
                    accepted = true;
                    break;
                }
            }
            if (!accepted)
            {
                restore(base);
                break;
            }
        }

        grid_positions = nodeIndex_pos;
        return validateLegacyMappedLayout() &&
               validateAssignedRoutePhases(phaseCount) &&
               phaseRunAcceptable();
    }

    bool CircuitGraph::placeAndRouteInternal(
        int shuffledRouteOrderRetries,
        RoutingFailureInfo *failureInfo,
        bool emitConflictStages,
        int deterministicPolicyLimit,
        const std::function<bool()> &acceptRoutedLayout)
    {
        enum class RouteOrderPolicy
        {
            SinkPressureFirst,
            GroupFanoutNearFirst,
            GroupFanoutFarFirst,
            LongFirst,
            ShortFirst,
            ReverseLayerLongFirst,
            ReverseLayerShortFirst
        };

        if (failureInfo)
        {
            failureInfo->failedEdges.clear();
            failureInfo->routedEdges = 0;
            failureInfo->routedLayoutRejected = false;
        }
        bool reportInitialized = false;

        const auto prepareBoard = [this]() {
            routes.clear();
            astar.reset();
            chessboard.reset();

            for (const auto &node_pos : nodeIndex_pos)
            {
                if (!chessboard.is_placeNode(node_pos.second))
                {
                    return false;
                }
                chessboard.placeNode(node_pos.second);
            }
            return true;
        };

        const auto edgeDistance = [this](const std::pair<int, int> &edge) {
            const auto start = nodeIndex_pos.at(edge.first);
            const auto end = nodeIndex_pos.at(edge.second);
            const auto axisDistance = [](unsigned int left, unsigned int right) {
                return left >= right
                    ? static_cast<std::uint64_t>(left) - right
                    : static_cast<std::uint64_t>(right) - left;
            };
            return axisDistance(end.first, start.first) +
                   axisDistance(end.second, start.second);
        };

        const auto isMultiFanout = [this](int nodeIndex) {
            return parse.getFanoutsIndex(nodeIndex).size() > 1;
        };
        std::set<position> placedNodePositions;
        for (const auto &node : nodeIndex_pos)
        {
            placedNodePositions.insert(node.second);
        }
        const auto crossesIntermediateNode =
            [&placedNodePositions](const std::pair<int, int> &, const std::vector<position> &path) {
                if (path.size() <= 2)
                {
                    return false;
                }

                for (auto it = std::next(path.begin()); std::next(it) != path.end(); ++it)
                {
                    if (placedNodePositions.count(*it) != 0)
                    {
                        return true;
                    }
                }
                return false;
            };

        std::map<std::pair<int, int>, int> adaptivePriority;
        const auto sortEdges = [&](RouteOrderPolicy policy) {
            auto edges = parse.getEffectiveEdges();
            std::stable_sort(edges.begin(), edges.end(), [&](const auto &lhs, const auto &rhs) {
                const int lhsPriority = adaptivePriority[lhs];
                const int rhsPriority = adaptivePriority[rhs];
                if (lhsPriority != rhsPriority)
                {
                    return lhsPriority > rhsPriority;
                }
                if (policy == RouteOrderPolicy::SinkPressureFirst)
                {
                    const std::size_t lhsFanins =
                        parse.getFaninsIndex(static_cast<unsigned int>(lhs.second)).size();
                    const std::size_t rhsFanins =
                        parse.getFaninsIndex(static_cast<unsigned int>(rhs.second)).size();
                    if (lhsFanins != rhsFanins)
                    {
                        return lhsFanins > rhsFanins;
                    }
                    const bool lhsFanout = isMultiFanout(lhs.first);
                    const bool rhsFanout = isMultiFanout(rhs.first);
                    if (lhsFanout != rhsFanout)
                    {
                        return !lhsFanout;
                    }
                }
                const int lhsLayer = parse.getVertexLayer(lhs.first);
                const int rhsLayer = parse.getVertexLayer(rhs.first);
                if (lhsLayer != rhsLayer)
                {
                    const bool reverseLayer =
                        policy == RouteOrderPolicy::ReverseLayerLongFirst ||
                        policy == RouteOrderPolicy::ReverseLayerShortFirst;
                    return reverseLayer ? lhsLayer > rhsLayer : lhsLayer < rhsLayer;
                }

                const std::uint64_t lhsDistance = edgeDistance(lhs);
                const std::uint64_t rhsDistance = edgeDistance(rhs);
                const bool lhsMultiFanout = isMultiFanout(lhs.first);
                const bool rhsMultiFanout = isMultiFanout(rhs.first);

                if (policy == RouteOrderPolicy::LongFirst ||
                    policy == RouteOrderPolicy::ReverseLayerLongFirst)
                {
                    if (lhsDistance != rhsDistance)
                    {
                        return lhsDistance > rhsDistance;
                    }
                    return lhs < rhs;
                }

                if (policy == RouteOrderPolicy::ShortFirst ||
                    policy == RouteOrderPolicy::ReverseLayerShortFirst)
                {
                    if (lhsDistance != rhsDistance)
                    {
                        return lhsDistance < rhsDistance;
                    }
                    return lhs < rhs;
                }

                if (lhs.first != rhs.first)
                {
                    if (lhsMultiFanout != rhsMultiFanout)
                    {
                        return lhsMultiFanout;
                    }
                    if (lhsMultiFanout && rhsMultiFanout)
                    {
                        return lhs.first < rhs.first;
                    }
                    if (lhsDistance != rhsDistance)
                    {
                        return lhsDistance > rhsDistance;
                    }
                    return lhs < rhs;
                }

                if (lhsMultiFanout)
                {
                    if (lhsDistance != rhsDistance)
                    {
                        return policy == RouteOrderPolicy::GroupFanoutNearFirst
                            ? lhsDistance < rhsDistance
                            : lhsDistance > rhsDistance;
                    }
                }
                else if (lhsDistance != rhsDistance)
                {
                    return lhsDistance > rhsDistance;
                }
                return lhs.second < rhs.second;
            });
            return edges;
        };

        const auto routeEdges = [&](const std::vector<std::pair<int, int>> &orderedEdges) {
            if (!prepareBoard())
            {
                return false;
            }

            std::vector<std::pair<int, int>> failedEdges;
            for (std::size_t edgeIndex = 0;
                 edgeIndex < orderedEdges.size();
                 ++edgeIndex)
            {
                const auto &edge = orderedEdges[edgeIndex];
                auto start = nodeIndex_pos[edge.first];
                auto end = nodeIndex_pos[edge.second];
                auto path = astar.findPath(start, end, isMultiFanout(edge.first));
                if (path.empty())
                {
                    ++adaptivePriority[edge];
                    if (fitnessCallback)
                    {
                        fitnessCallback("A* routing failed for edge " +
                                        std::to_string(edge.first) + "->" +
                                        std::to_string(edge.second));
                    }
                    if (emitConflictStages && stageCallback)
                    {
                        stageCallback("routing_conflict");
                    }
                    failedEdges.push_back(edge);
                    if (!failureInfo)
                    {
                        return false;
                    }
                    continue;
                }
                if (crossesIntermediateNode(edge, path))
                {
                    ++adaptivePriority[edge];
                    if (fitnessCallback)
                    {
                        fitnessCallback("A* route crosses an intermediate node for edge " +
                                        std::to_string(edge.first) + "->" +
                                        std::to_string(edge.second));
                    }
                    if (emitConflictStages && stageCallback)
                    {
                        stageCallback("routing_conflict");
                    }
                    failedEdges.push_back(edge);
                    if (!failureInfo)
                    {
                        return false;
                    }
                    // findPath has already committed this path to the board.
                    // Abort this ordering so the next prepareBoard() call can
                    // discard the illegal intermediate-node traversal.
                    failedEdges.insert(
                        failedEdges.end(),
                        std::next(orderedEdges.begin(),
                                  static_cast<std::ptrdiff_t>(edgeIndex + 1)),
                        orderedEdges.end());
                    break;
                }
                routes.insert({edge, path});
            }

            if (failureInfo &&
                (!reportInitialized ||
                 failedEdges.size() < failureInfo->failedEdges.size() ||
                 (failedEdges.size() == failureInfo->failedEdges.size() &&
                  routes.size() > failureInfo->routedEdges)))
            {
                failureInfo->failedEdges = failedEdges;
                failureInfo->routedEdges = routes.size();
                reportInitialized = true;
            }
            return failedEdges.empty();
        };
        const auto acceptsCurrentLayout = [&]() {
            if (!acceptRoutedLayout || acceptRoutedLayout())
            {
                if (failureInfo)
                {
                    failureInfo->routedLayoutRejected = false;
                }
                return true;
            }
            if (failureInfo)
            {
                failureInfo->routedLayoutRejected = true;
            }
            return false;
        };

        const RouteOrderPolicy policies[] = {
            RouteOrderPolicy::SinkPressureFirst,
            RouteOrderPolicy::GroupFanoutNearFirst,
            RouteOrderPolicy::GroupFanoutFarFirst,
            RouteOrderPolicy::LongFirst,
            RouteOrderPolicy::ShortFirst,
            RouteOrderPolicy::ReverseLayerLongFirst,
            RouteOrderPolicy::ReverseLayerShortFirst
        };
        deterministicPolicyLimit = std::clamp(deterministicPolicyLimit, 1, 7);
        for (int policyIndex = 0;
             policyIndex < deterministicPolicyLimit;
             ++policyIndex)
        {
            if (routeEdges(sortEdges(policies[policyIndex])) &&
                acceptsCurrentLayout())
            {
                return true;
            }
        }

        // Strict source-aware crossings turn routing order into a real
        // constraint: an early legal route can consume the only usable port
        // of a later gate.  Keep the historical fast orders first, then try a
        // small deterministic set of alternative orders before declaring the
        // Graphviz placement unroutable.
        auto shuffledEdges = parse.getEffectiveEdges();
        std::mt19937 routeOrderGenerator(0x4a554e45u); // "JUNE"
        shuffledRouteOrderRetries = std::max(0, shuffledRouteOrderRetries);
        for (int retry = 0; retry < shuffledRouteOrderRetries; ++retry)
        {
            std::shuffle(shuffledEdges.begin(), shuffledEdges.end(),
                         routeOrderGenerator);
            std::stable_sort(shuffledEdges.begin(), shuffledEdges.end(),
                [&adaptivePriority](const auto &left, const auto &right) {
                    return adaptivePriority[left] > adaptivePriority[right];
                });
            if (routeEdges(shuffledEdges) && acceptsCurrentLayout())
            {
                return true;
            }
        }
        return false;
    }

    bool CircuitGraph::validateJuneRandomClockRoutedLayout(
        int phaseCount,
        int maxSamePhase)
    {
        phaseCount = std::max(2, phaseCount);

        std::string routeOwnershipError;
        if (!validateJuneFanoutTrees(routes, routeOwnershipError))
        {
            if (fitnessCallback)
            {
                fitnessCallback("June random-clock graph P&R: illegal route ownership: " +
                                routeOwnershipError);
            }
            return false;
        }
        if (stageCallback)
        {
            stageCallback("routed_unphased");
        }

        if (fitnessCallback)
        {
            fitnessCallback("June random-clock graph P&R: post-route phase assignment");
        }
        if (!assignPhases(phaseCount) ||
            !validateAssignedRoutePhases(phaseCount))
        {
            return false;
        }
        if (maxSamePhase > 0)
        {
            for (const auto &route : routes)
            {
                int previousPhase = -1;
                int samePhaseRun = 0;
                for (const position &pos : route.second)
                {
                    const auto cell = chessboard.gridMap.find(pos);
                    if (cell == chessboard.gridMap.end() ||
                        cell->second.getPhase() < 1)
                    {
                        return false;
                    }
                    const int phase = cell->second.getPhase();
                    samePhaseRun = phase == previousPhase
                        ? samePhaseRun + 1
                        : 1;
                    if (samePhaseRun > maxSamePhase)
                    {
                        if (fitnessCallback)
                        {
                            fitnessCallback(
                                "June random-clock graph P&R: maximum same-phase run exceeded");
                        }
                        return false;
                    }
                    previousPhase = phase;
                }
            }
        }
        if (stageCallback)
        {
            stageCallback("phase_assigned");
        }

        std::vector<std::vector<position>> routeGeometry;
        routeGeometry.reserve(routes.size());
        for (const auto &route : routes)
        {
            routeGeometry.push_back(route.second);
        }
        Mapping mapping;
        mapping.mapping_line(routeGeometry);
        std::string crossoverError;
        if (!mapping.validate_crossovers(&crossoverError))
        {
            if (fitnessCallback)
            {
                fitnessCallback("June random-clock graph P&R: invalid cell-level crossover: " +
                                crossoverError);
            }
            return false;
        }
        return true;
    }

    bool CircuitGraph::routeAndValidateJuneRandomClock(
        int phaseCount,
        int shuffledRouteOrderRetries,
        int maxSamePhase)
    {
        phaseCount = std::max(2, phaseCount);

        if (fitnessCallback)
        {
            fitnessCallback("June random-clock graph P&R: four-direction A* routing");
        }
        const auto acceptCompleteLayout = [this, phaseCount, maxSamePhase]() {
            return validateJuneRandomClockRoutedLayout(
                phaseCount, maxSamePhase);
        };
        if (!placeAndRouteInternal(
                shuffledRouteOrderRetries,
                nullptr,
                true,
                6,
                acceptCompleteLayout))
        {
            // Preserve the last partial routing attempt for paper/debug
            // snapshots before the caller rejects or relaxes this candidate.
            if (stageCallback)
            {
                stageCallback("routing_failed");
            }
            return false;
        }
        return true;
    }

    bool CircuitGraph::routeCompactRandomClockWithExpansion(
        int phaseCount,
        int shuffledRouteOrderRetries,
        int maxExpansionRounds,
        double maxSearchCost,
        int maxSamePhase)
    {
        phaseCount = std::max(2, phaseCount);
        shuffledRouteOrderRetries = std::max(0, shuffledRouteOrderRetries);
        maxExpansionRounds = std::clamp(maxExpansionRounds, 0, 16);
        astar.setMaxSearchCost(std::max(40.0, maxSearchCost));
        adaptiveExpansionStats = {};

        if (nodeIndex_pos.empty())
        {
            return false;
        }

        const auto materializePlacement = [this]() {
            routes.clear();
            astar.reset();
            chessboard.reset();
            for (const auto &node : nodeIndex_pos)
            {
                if (!chessboard.is_placeNode(node.second))
                {
                    return false;
                }
                chessboard.placeNode(node.second);
            }
            grid_positions = nodeIndex_pos;
            return true;
        };
        if (!materializePlacement())
        {
            return false;
        }
        if (stageCallback)
        {
            stageCallback("compact_placement");
        }

        struct CapacityCut
        {
            bool row = true;
            unsigned int coordinate = 0;
            int pressure = 0;
        };
        const auto applyCut = [this](const CapacityCut &cut) {
            for (const auto &node : nodeIndex_pos)
            {
                const unsigned int coordinate =
                    cut.row ? node.second.second : node.second.first;
                if (coordinate >= cut.coordinate &&
                    coordinate == std::numeric_limits<unsigned int>::max())
                {
                    return false;
                }
            }
            for (auto &node : nodeIndex_pos)
            {
                unsigned int &coordinate =
                    cut.row ? node.second.second : node.second.first;
                if (coordinate >= cut.coordinate)
                {
                    ++coordinate;
                }
            }
            grid_positions = nodeIndex_pos;
            return true;
        };
        const auto nodeArea = [this]() {
            unsigned int minX = std::numeric_limits<unsigned int>::max();
            unsigned int minY = std::numeric_limits<unsigned int>::max();
            unsigned int maxX = 0;
            unsigned int maxY = 0;
            for (const auto &node : nodeIndex_pos)
            {
                minX = std::min(minX, node.second.first);
                minY = std::min(minY, node.second.second);
                maxX = std::max(maxX, node.second.first);
                maxY = std::max(maxY, node.second.second);
            }
            if (minX == std::numeric_limits<unsigned int>::max())
            {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return (static_cast<std::uint64_t>(maxX) - minX + 1) *
                   (static_cast<std::uint64_t>(maxY) - minY + 1);
        };
        const auto usedArea = [this]() {
            unsigned int minX = std::numeric_limits<unsigned int>::max();
            unsigned int minY = std::numeric_limits<unsigned int>::max();
            unsigned int maxX = 0;
            unsigned int maxY = 0;
            for (const auto &cell : chessboard.gridMap)
            {
                if (cell.second.get_current_weight() == 0)
                {
                    continue;
                }
                minX = std::min(minX, cell.first.first);
                minY = std::min(minY, cell.first.second);
                maxX = std::max(maxX, cell.first.first);
                maxY = std::max(maxY, cell.first.second);
            }
            if (minX == std::numeric_limits<unsigned int>::max())
            {
                return std::numeric_limits<std::uint64_t>::max();
            }
            return (static_cast<std::uint64_t>(maxX) - minX + 1) *
                   (static_cast<std::uint64_t>(maxY) - minY + 1);
        };
        const auto betterReport = [](bool routed,
                                     const RoutingFailureInfo &report,
                                     std::uint64_t area,
                                     bool currentRouted,
                                     const RoutingFailureInfo &currentReport,
                                     std::uint64_t currentArea) {
            if (routed != currentRouted)
            {
                return routed;
            }
            if (report.failedEdges.size() != currentReport.failedEdges.size())
            {
                return report.failedEdges.size() <
                       currentReport.failedEdges.size();
            }
            if (report.routedEdges != currentReport.routedEdges)
            {
                return report.routedEdges > currentReport.routedEdges;
            }
            return area < currentArea;
        };
        const auto acceptCompleteLayout = [this, phaseCount, maxSamePhase]() {
            return validateJuneRandomClockRoutedLayout(
                phaseCount, maxSamePhase);
        };
        const auto removeRedundantCapacity = [&]() {
            // Expansion is deliberately conservative.  Once a legal route and
            // phase assignment exist, sweep empty cut lines in reverse order
            // and retain a deletion only if full routing, phase closure, fanout
            // ownership, and crossover DRC remain legal.
            for (int removalRound = 0;
                 removalRound < std::max(12, maxExpansionRounds);
                 ++removalRound)
            {
                unsigned int minX = std::numeric_limits<unsigned int>::max();
                unsigned int minY = std::numeric_limits<unsigned int>::max();
                unsigned int maxX = 0;
                unsigned int maxY = 0;
                std::set<unsigned int> occupiedX;
                std::set<unsigned int> occupiedY;
                for (const auto &node : nodeIndex_pos)
                {
                    minX = std::min(minX, node.second.first);
                    minY = std::min(minY, node.second.second);
                    maxX = std::max(maxX, node.second.first);
                    maxY = std::max(maxY, node.second.second);
                    occupiedX.insert(node.second.first);
                    occupiedY.insert(node.second.second);
                }

                std::vector<CapacityCut> removableCuts;
                if (minX != std::numeric_limits<unsigned int>::max())
                {
                    for (unsigned int x = maxX; x > minX; --x)
                    {
                        if (occupiedX.count(x) == 0)
                        {
                            removableCuts.push_back({false, x, 0});
                        }
                    }
                    for (unsigned int y = maxY; y > minY; --y)
                    {
                        if (occupiedY.count(y) == 0)
                        {
                            removableCuts.push_back({true, y, 0});
                        }
                    }
                }
                if (removableCuts.empty())
                {
                    break;
                }

                bool removed = false;
                for (const CapacityCut &cut : removableCuts)
                {
                    const auto savedNodes = nodeIndex_pos;
                    const auto savedRoutes = routes;
                    const auto savedCells = chessboard.gridMap;
                    const std::uint64_t savedUsedArea = usedArea();
                    for (auto &node : nodeIndex_pos)
                    {
                        unsigned int &coordinate =
                            cut.row ? node.second.second : node.second.first;
                        if (coordinate > cut.coordinate)
                        {
                            --coordinate;
                        }
                    }
                    grid_positions = nodeIndex_pos;

                    const auto savedFitnessCallback = fitnessCallback;
                    const auto savedStageCallback = stageCallback;
                    fitnessCallback = {};
                    stageCallback = {};
                    RoutingFailureInfo compactReport;
                    const bool compactRouted = placeAndRouteInternal(
                        shuffledRouteOrderRetries,
                        &compactReport,
                        false,
                        6,
                        acceptCompleteLayout);
                    const bool compactLegal = compactRouted &&
                        usedArea() < savedUsedArea;
                    fitnessCallback = savedFitnessCallback;
                    stageCallback = savedStageCallback;

                    if (compactLegal)
                    {
                        removed = true;
                        if (cut.row)
                        {
                            ++adaptiveExpansionStats.removedRows;
                        }
                        else
                        {
                            ++adaptiveExpansionStats.removedColumns;
                        }
                        if (fitnessCallback)
                        {
                            fitnessCallback(
                                std::string("Compact-first random-clock P&R: removed redundant ") +
                                (cut.row ? "row y=" : "column x=") +
                                std::to_string(cut.coordinate));
                        }
                        if (stageCallback)
                        {
                            stageCallback(
                                std::string("layout_remove_") +
                                (cut.row ? "row_at_" : "column_at_") +
                                std::to_string(cut.coordinate));
                        }
                        break;
                    }

                    nodeIndex_pos = savedNodes;
                    grid_positions = nodeIndex_pos;
                    routes = savedRoutes;
                    chessboard.gridMap = savedCells;
                    astar.reset();
                }
                if (!removed)
                {
                    break;
                }
            }
            return true;
        };

        for (int round = 0; round <= maxExpansionRounds; ++round)
        {
            if (fitnessCallback)
            {
                fitnessCallback(
                    round == 0
                        ? "Compact-first random-clock P&R: routing the minimum-spacing seed"
                        : "Compact-first random-clock P&R: rerouting the expanded placement");
            }

            RoutingFailureInfo failure;
            RoutingFailureInfo *failureReport =
                maxExpansionRounds > 0 ? &failure : nullptr;
            if (placeAndRouteInternal(
                    shuffledRouteOrderRetries,
                    failureReport,
                    true,
                    6,
                    acceptCompleteLayout))
            {
                removeRedundantCapacity();
                return true;
            }
            if (failure.routedLayoutRejected && failure.failedEdges.empty())
            {
                // Geometry alone is insufficient: phase closure, fanout
                // ownership, or crossover validation rejected every complete
                // route ordering.  Let one sparse cut be tested using the full
                // net set as pressure instead of silently abandoning expansion.
                failure.failedEdges = parse.getEffectiveEdges();
            }
            if (round == maxExpansionRounds || failure.failedEdges.empty())
            {
                break;
            }

            // Port-aware sparse capacity pressure, mirroring the fixed-clock
            // Normal Graph repair policy.  A horizontal-dominant failed net
            // benefits from another row; a vertical-dominant net benefits
            // from another column.  The sink's assigned top/left port receives
            // the strongest vote so a blocked gate port is repaired before
            // unrelated whitespace is added.
            std::map<unsigned int, int> rowPressure;
            std::map<unsigned int, int> columnPressure;
            for (const auto &edge : failure.failedEdges)
            {
                const auto sourceIt = nodeIndex_pos.find(edge.first);
                const auto targetIt = nodeIndex_pos.find(edge.second);
                if (sourceIt == nodeIndex_pos.end() ||
                    targetIt == nodeIndex_pos.end())
                {
                    continue;
                }
                const position source = sourceIt->second;
                const position target = targetIt->second;
                const auto axisDistance = [](unsigned int left,
                                             unsigned int right) {
                    return left >= right
                        ? static_cast<std::uint64_t>(left) - right
                        : static_cast<std::uint64_t>(right) - left;
                };
                const std::uint64_t deltaX =
                    axisDistance(target.first, source.first);
                const std::uint64_t deltaY =
                    axisDistance(target.second, source.second);
                if (deltaX >= deltaY)
                {
                    rowPressure[std::max(source.second, target.second)] += 1;
                }
                if (deltaY >= deltaX)
                {
                    const unsigned int cut =
                        source.first < target.first
                            ? target.first
                            : (target.first < source.first
                                   ? source.first
                                   : (target.first ==
                                              std::numeric_limits<unsigned int>::max()
                                          ? target.first
                                          : target.first + 1));
                    columnPressure[cut] += 1;
                }

                const auto &fanins =
                    parse.getFaninsIndex(static_cast<unsigned int>(edge.second));
                int leftPortFanin = -1;
                if (fanins.size() >= 2)
                {
                    unsigned int nearestLeftX = 0;
                    unsigned int greatestX = 0;
                    bool hasNearestLeft = false;
                    bool hasGreatest = false;
                    int greatestNode = -1;
                    for (const uint64_t fanin : fanins)
                    {
                        const auto faninPos =
                            nodeIndex_pos.find(static_cast<int>(fanin));
                        if (faninPos == nodeIndex_pos.end())
                        {
                            continue;
                        }
                        const unsigned int x = faninPos->second.first;
                        if (x < target.first &&
                            (!hasNearestLeft || x > nearestLeftX))
                        {
                            hasNearestLeft = true;
                            nearestLeftX = x;
                            leftPortFanin = static_cast<int>(fanin);
                        }
                        if (!hasGreatest || x > greatestX)
                        {
                            hasGreatest = true;
                            greatestX = x;
                            greatestNode = static_cast<int>(fanin);
                        }
                    }
                    if (leftPortFanin < 0)
                    {
                        leftPortFanin = greatestNode;
                    }
                }
                if (leftPortFanin == edge.first &&
                    source.first < target.first)
                {
                    columnPressure[target.first] += 4;
                }
                else if (target.second > source.second)
                {
                    rowPressure[target.second] += 4;
                }

                const auto &fanouts =
                    parse.getFanoutsIndex(static_cast<unsigned int>(edge.first));
                bool allBelow = !fanouts.empty();
                bool allRight = !fanouts.empty();
                for (const uint64_t fanout : fanouts)
                {
                    const auto fanoutPos =
                        nodeIndex_pos.find(static_cast<int>(fanout));
                    if (fanoutPos == nodeIndex_pos.end())
                    {
                        continue;
                    }
                    allBelow = allBelow &&
                        fanoutPos->second.second > source.second;
                    allRight = allRight &&
                        fanoutPos->second.first > source.first;
                }
                if (allBelow)
                {
                    if (source.second <
                        std::numeric_limits<unsigned int>::max())
                    {
                        rowPressure[source.second + 1] += 2;
                    }
                }
                else if (allRight)
                {
                    if (source.first <
                        std::numeric_limits<unsigned int>::max())
                    {
                        columnPressure[source.first + 1] += 2;
                    }
                }
            }

            std::vector<CapacityCut> cuts;
            for (const auto &entry : rowPressure)
            {
                cuts.push_back({true, entry.first, entry.second});
            }
            for (const auto &entry : columnPressure)
            {
                cuts.push_back({false, entry.first, entry.second});
            }
            std::stable_sort(cuts.begin(), cuts.end(),
                [](const CapacityCut &left, const CapacityCut &right) {
                    if (left.pressure != right.pressure)
                    {
                        return left.pressure > right.pressure;
                    }
                    if (left.row != right.row)
                    {
                        return !left.row;
                    }
                    return left.coordinate < right.coordinate;
                });
            if (cuts.size() > 4)
            {
                cuts.resize(4);
            }
            if (cuts.empty())
            {
                break;
            }

            const auto basePositions = nodeIndex_pos;
            const auto baseRoutes = routes;
            const auto baseCells = chessboard.gridMap;
            const std::uint64_t baseArea = nodeArea();
            bool haveAcceptedCut = false;
            bool bestRouted = false;
            RoutingFailureInfo bestReport = failure;
            std::uint64_t bestArea = baseArea;
            CapacityCut bestCut;
            std::map<int, position> bestPositions;

            const auto savedFitnessCallback = fitnessCallback;
            const auto savedStageCallback = stageCallback;
            fitnessCallback = {};
            stageCallback = {};
            for (const CapacityCut &cut : cuts)
            {
                nodeIndex_pos = basePositions;
                grid_positions = nodeIndex_pos;
                if (!applyCut(cut))
                {
                    continue;
                }

                RoutingFailureInfo trialReport;
                const bool trialRouted = placeAndRouteInternal(
                    shuffledRouteOrderRetries,
                    &trialReport,
                    false,
                    6,
                    acceptCompleteLayout);
                if (!trialRouted && trialReport.routedLayoutRejected)
                {
                    continue;
                }
                const std::uint64_t trialArea = nodeArea();
                if (!haveAcceptedCut ||
                    betterReport(trialRouted,
                                 trialReport,
                                 trialArea,
                                 bestRouted,
                                 bestReport,
                                 bestArea))
                {
                    haveAcceptedCut = true;
                    bestRouted = trialRouted;
                    bestReport = trialReport;
                    bestArea = trialArea;
                    bestCut = cut;
                    bestPositions = nodeIndex_pos;
                }
                if (trialRouted)
                {
                    break;
                }
            }
            fitnessCallback = savedFitnessCallback;
            stageCallback = savedStageCallback;
            nodeIndex_pos = basePositions;
            grid_positions = nodeIndex_pos;
            routes = baseRoutes;
            chessboard.gridMap = baseCells;
            astar.reset();

            const bool strictlyImproved =
                haveAcceptedCut &&
                betterReport(bestRouted,
                             bestReport,
                             bestArea,
                             false,
                             failure,
                             baseArea);
            if (!strictlyImproved)
            {
                if (fitnessCallback)
                {
                    fitnessCallback(
                        "Compact-first random-clock P&R: no single row/column cut improved routing; preserving the compact seed");
                }
                break;
            }

            nodeIndex_pos = std::move(bestPositions);
            grid_positions = nodeIndex_pos;
            ++adaptiveExpansionStats.acceptedRounds;
            if (bestCut.row)
            {
                ++adaptiveExpansionStats.insertedRows;
            }
            else
            {
                ++adaptiveExpansionStats.insertedColumns;
            }
            if (fitnessCallback)
            {
                fitnessCallback(
                    std::string("Compact-first random-clock P&R: inserted one ") +
                    (bestCut.row ? "row before y=" : "column before x=") +
                    std::to_string(bestCut.coordinate) +
                    " from failed-edge pressure");
            }
            if (!materializePlacement())
            {
                return false;
            }
            if (stageCallback)
            {
                stageCallback(
                    std::string("layout_insert_") +
                    (bestCut.row ? "row_at_" : "column_at_") +
                    std::to_string(bestCut.coordinate));
            }
        }

        if (stageCallback)
        {
            stageCallback("routing_failed");
        }
        return false;
    }

    bool CircuitGraph::placeAndRouteJuneRandomClock(int phaseCount,
                                                     double graphvizGridSize,
                                                     int shuffledRouteOrderRetries,
                                                     int maxSamePhase)
    {
        phaseCount = std::max(2, phaseCount);
        if (graphvizGridSize <= 0.0)
        {
            graphvizGridSize = 40.0;
        }

        if (fitnessCallback)
        {
            fitnessCallback("June random-clock graph P&R: Graphviz DOT placement");
        }
        processAndGenerateGraph(false, true, true, true);
        sortNodesByYThenXCoordinate(graphvizGridSize);
        if (nodeIndex_pos.empty())
        {
            return false;
        }
        if (stageCallback)
        {
            stageCallback("quantized_placement");
        }
        return routeAndValidateJuneRandomClock(
            phaseCount, shuffledRouteOrderRetries, maxSamePhase);
    }

    bool CircuitGraph::placeAndRouteJuneRandomClockAnisotropic(
        int phaseCount,
        double graphvizGridSizeX,
        double graphvizGridSizeY,
        int shuffledRouteOrderRetries,
        int maxSamePhase)
    {
        phaseCount = std::max(2, phaseCount);
        graphvizGridSizeX = graphvizGridSizeX > 0.0
            ? graphvizGridSizeX : 40.0;
        graphvizGridSizeY = graphvizGridSizeY > 0.0
            ? graphvizGridSizeY : graphvizGridSizeX;

        if (fitnessCallback)
        {
            fitnessCallback(
                "June random-clock graph P&R: anisotropic Graphviz placement");
        }
        processAndGenerateGraph(false, true, true, true);
        sortNodesByYThenXCoordinate(graphvizGridSizeX, graphvizGridSizeY);
        if (nodeIndex_pos.empty())
        {
            return false;
        }
        if (stageCallback)
        {
            stageCallback("quantized_placement");
        }
        return routeAndValidateJuneRandomClock(
            phaseCount, shuffledRouteOrderRetries, maxSamePhase);
    }

    bool CircuitGraph::placeAndRouteCompactLayeredClock(
        int phaseCount,
        unsigned int xSpacing,
        unsigned int ySpacing,
        int shuffledRouteOrderRetries)
    {
        phaseCount = std::max(2, phaseCount);
        xSpacing = std::max(2u, xSpacing);
        ySpacing = std::max(2u, ySpacing);

        if (fitnessCallback)
        {
            fitnessCallback(
                "Compact layered graph P&R: Graphviz order with regularized coordinates");
        }
        processAndGenerateGraph(false, true, true, true);
        sortNodesByLayeredGrid(xSpacing, ySpacing);
        if (nodeIndex_pos.empty())
        {
            return false;
        }
        return routeAndValidateJuneRandomClock(
            phaseCount, shuffledRouteOrderRetries);
    }

    bool CircuitGraph::placeAndRoutePhaseAware(int phaseCount,
                                                int maxSamePhase,
                                                double maxSearchCost,
                                                int maxRoutingAttempts,
                                                bool enableFlexiblePhasePass)
    {
        enum class RouteOrderPolicy
        {
            GroupFanoutFarFirst,
            GroupFanoutNearFirst,
            LongFirst,
            ShortFirst,
            ReverseLayerLongFirst,
            ReverseLayerShortFirst
        };

        phaseCount = std::max(2, phaseCount);
        maxSamePhase = std::max(1, maxSamePhase);

        const auto edgeDistance = [this](const std::pair<int, int> &edge) {
            const position start = nodeIndex_pos.at(edge.first);
            const position end = nodeIndex_pos.at(edge.second);
            return std::abs(static_cast<int>(end.first) - static_cast<int>(start.first)) +
                   std::abs(static_cast<int>(end.second) - static_cast<int>(start.second));
        };
        const auto isMultiFanout = [this](int nodeIndex) {
            return parse.getFanoutsIndex(nodeIndex).size() > 1;
        };
        double effectiveSearchCost = maxSearchCost;
        const auto effectiveEdges = parse.getEffectiveEdges();
        for (const auto &edge : effectiveEdges)
        {
            effectiveSearchCost = std::max(
                effectiveSearchCost,
                static_cast<double>(edgeDistance(edge) + phaseCount * 4 + 24));
        }
        std::map<std::pair<int, int>, int> adaptivePriority;
        const auto sortEdges = [&](RouteOrderPolicy policy) {
            auto edges = effectiveEdges;
            std::stable_sort(edges.begin(), edges.end(), [&](const auto &left, const auto &right) {
                const int leftPriority = adaptivePriority[left];
                const int rightPriority = adaptivePriority[right];
                if (leftPriority != rightPriority)
                {
                    return leftPriority > rightPriority;
                }
                const int leftLayer = parse.getVertexLayer(left.first);
                const int rightLayer = parse.getVertexLayer(right.first);
                if (leftLayer != rightLayer)
                {
                    const bool reverseLayer =
                        policy == RouteOrderPolicy::ReverseLayerLongFirst ||
                        policy == RouteOrderPolicy::ReverseLayerShortFirst;
                    return reverseLayer ? leftLayer > rightLayer : leftLayer < rightLayer;
                }
                const int leftDistance = edgeDistance(left);
                const int rightDistance = edgeDistance(right);
                if ((policy == RouteOrderPolicy::LongFirst ||
                     policy == RouteOrderPolicy::ReverseLayerLongFirst) &&
                    leftDistance != rightDistance)
                {
                    return leftDistance > rightDistance;
                }
                if ((policy == RouteOrderPolicy::ShortFirst ||
                     policy == RouteOrderPolicy::ReverseLayerShortFirst) &&
                    leftDistance != rightDistance)
                {
                    return leftDistance < rightDistance;
                }
                if (left.first != right.first)
                {
                    const bool leftFanout = isMultiFanout(left.first);
                    const bool rightFanout = isMultiFanout(right.first);
                    if (leftFanout != rightFanout)
                    {
                        return leftFanout;
                    }
                    if (leftDistance != rightDistance)
                    {
                        return leftDistance > rightDistance;
                    }
                    return left < right;
                }
                if (leftDistance != rightDistance)
                {
                    return policy == RouteOrderPolicy::GroupFanoutNearFirst
                        ? leftDistance < rightDistance : leftDistance > rightDistance;
                }
                return left.second < right.second;
            });
            return edges;
        };
        std::set<position> placedNodePositions;
        for (const auto &node : nodeIndex_pos)
        {
            placedNodePositions.insert(node.second);
        }
        const auto crossesIntermediateNode = [&placedNodePositions](
                                                     const std::pair<int, int> &,
                                                     const std::vector<position> &path) {
            if (path.size() <= 2)
            {
                return false;
            }
            return std::any_of(std::next(path.begin()), std::prev(path.end()),
                               [&placedNodePositions](const position &pos) {
                                   return placedNodePositions.count(pos) != 0;
                               });
        };

        const RouteOrderPolicy policies[] = {
            RouteOrderPolicy::GroupFanoutFarFirst,
            RouteOrderPolicy::GroupFanoutNearFirst,
            RouteOrderPolicy::LongFirst,
            RouteOrderPolicy::ShortFirst,
            RouteOrderPolicy::ReverseLayerLongFirst,
            RouteOrderPolicy::ReverseLayerShortFirst
        };
        // Repeat the six deterministic policies after promoting the edge that
        // blocked the previous pass, then use bounded seeded order variation.
        // The former fallback branched phase at every visited cell; on
        // congested MAJ circuits that multiplied the state space without
        // changing the geometric deadlock.  Launch offsets retain phase
        // diversity while every adaptive reorder pass stays predictable.
        maxRoutingAttempts = std::clamp(maxRoutingAttempts, 1, 36);
        const int routingAttempts = effectiveEdges.size() > 300
            ? std::min(6, maxRoutingAttempts)
            : maxRoutingAttempts;
        for (int policyIndex = 0; policyIndex < routingAttempts; ++policyIndex)
        {
            const RouteOrderPolicy policy = policies[policyIndex % 6];
            routes.clear();
            chessboard.reset();
            PhaseAwareAstar router(chessboard,
                                   phaseCount,
                                   maxSamePhase,
                                   effectiveSearchCost,
                                   enableFlexiblePhasePass && policyIndex >= 6
                                       ? -1 : policyIndex);
            bool placed = true;
            for (const auto &node : nodeIndex_pos)
            {
                if (!chessboard.is_placeNode(node.second))
                {
                    placed = false;
                    break;
                }
                chessboard.placeNode(node.second);
            }
            if (!placed)
            {
                continue;
            }

            bool routed = true;
            auto orderedEdges = sortEdges(policy);
            if (policyIndex >= 12)
            {
                // The first two passes are fully reproducible heuristics.  A
                // third bounded pass explores deterministic pseudo-random tie
                // orders; failed edges remain promoted to the front.  This is
                // substantially cheaper than branching phase at every cell and
                // breaks greedy routing deadlocks on wider MAJ networks.
                std::mt19937 generator(
                    0x47524150u + static_cast<unsigned int>(policyIndex * 131) +
                    static_cast<unsigned int>(effectiveEdges.size()));
                std::shuffle(orderedEdges.begin(), orderedEdges.end(), generator);
                std::stable_sort(orderedEdges.begin(), orderedEdges.end(),
                    [&adaptivePriority](const auto &left, const auto &right) {
                        return adaptivePriority[left] > adaptivePriority[right];
                    });
            }
            for (const auto &edge : orderedEdges)
            {
                const position start = nodeIndex_pos.at(edge.first);
                const position end = nodeIndex_pos.at(edge.second);
                const int preferredStartPhase =
                    (parse.getVertexLayer(edge.first) + policyIndex) % phaseCount + 1;
                auto result = router.findPath(start,
                                              end,
                                              preferredStartPhase,
                                              isMultiFanout(edge.first));
                if (!result.has_value() || crossesIntermediateNode(edge, result->positions))
                {
                    ++adaptivePriority[edge];
                    if (fitnessCallback)
                    {
                        fitnessCallback("phase-aware route failed at edge " +
                            std::to_string(edge.first) + "->" + std::to_string(edge.second) +
                            " after " + std::to_string(routes.size()) +
                            " routes (policy " + std::to_string(policyIndex) + "): " +
                            (result.has_value() ? "intermediate-node conflict" : router.lastError()));
                    }
                    routed = false;
                    break;
                }
                routes[{static_cast<unsigned int>(edge.first),
                        static_cast<unsigned int>(edge.second)}] = std::move(result->positions);
            }
            if (!routed || !validateAssignedRoutePhases(phaseCount))
            {
                continue;
            }

            std::string routeOwnershipError;
            if (!validateJuneFanoutTrees(routes, routeOwnershipError))
            {
                if (fitnessCallback)
                {
                    fitnessCallback("phase-aware route ownership failed: " +
                                    routeOwnershipError);
                }
                continue;
            }

            bool repeatLimitOk = true;
            for (const auto &route : routes)
            {
                int previous = -1;
                int run = 1;
                for (const position &pos : route.second)
                {
                    const auto cell = chessboard.gridMap.find(pos);
                    if (cell == chessboard.gridMap.end())
                    {
                        repeatLimitOk = false;
                        break;
                    }
                    const int phase = cell->second.getPhase();
                    run = phase == previous ? run + 1 : 1;
                    if (run > maxSamePhase)
                    {
                        repeatLimitOk = false;
                        break;
                    }
                    previous = phase;
                }
                if (!repeatLimitOk)
                {
                    break;
                }
            }
            if (repeatLimitOk)
            {
                return true;
            }
        }
        return false;
    }

    int CircuitGraph::compactPhaseAware(int phaseCount,
                                         int maxSamePhase,
                                         double maxSearchCost,
                                         int maxRounds,
                                         int maxElapsedMilliseconds,
                                         int maxEvaluatedCuts)
    {
        struct Snapshot
        {
            std::map<int, position> nodes;
            std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routedEdges;
            std::unordered_map<position, GridCell, PositionHash> cells;
        };
        const auto takeSnapshot = [this]() {
            return Snapshot{nodeIndex_pos, routes, chessboard.gridMap};
        };
        const auto restore = [this](const Snapshot &snapshot) {
            nodeIndex_pos = snapshot.nodes;
            grid_positions = snapshot.nodes;
            routes = snapshot.routedEdges;
            chessboard.gridMap = snapshot.cells;
        };
        const auto score = [this]() {
            bool initialized = false;
            unsigned int minX = 0, maxX = 0, minY = 0, maxY = 0;
            int routeLength = 0;
            for (const auto &cell : chessboard.gridMap)
            {
                if (cell.second.get_current_weight() == 0)
                {
                    continue;
                }
                if (!initialized)
                {
                    minX = maxX = cell.first.first;
                    minY = maxY = cell.first.second;
                    initialized = true;
                }
                minX = std::min(minX, cell.first.first);
                maxX = std::max(maxX, cell.first.first);
                minY = std::min(minY, cell.first.second);
                maxY = std::max(maxY, cell.first.second);
            }
            for (const auto &route : routes)
            {
                routeLength += static_cast<int>(route.second.size());
            }
            if (!initialized)
            {
                return std::make_tuple(std::numeric_limits<int>::max(),
                                       std::numeric_limits<int>::max(),
                                       std::numeric_limits<int>::max());
            }
            const int width = static_cast<int>(maxX - minX + 1);
            const int height = static_cast<int>(maxY - minY + 1);
            return std::make_tuple(width * height, std::max(width, height), routeLength);
        };
        const auto collisionFree = [](const std::map<int, position> &nodes) {
            std::set<position> occupied;
            for (const auto &node : nodes)
            {
                if (!occupied.insert(node.second).second)
                {
                    return false;
                }
            }
            return true;
        };

        int acceptedCuts = 0;
        int evaluatedCuts = 0;
        maxRounds = std::max(0, maxRounds);
        maxElapsedMilliseconds = std::max(0, maxElapsedMilliseconds);
        maxEvaluatedCuts = std::max(0, maxEvaluatedCuts);
        const auto started = std::chrono::steady_clock::now();
        const auto budgetExpired = [&]() {
            if (maxElapsedMilliseconds == 0)
            {
                return true;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            return elapsed.count() >= maxElapsedMilliseconds;
        };
        for (int round = 0; round < maxRounds; ++round)
        {
            const Snapshot base = takeSnapshot();
            const auto baseScore = score();
            bool accepted = false;

            for (int axis = 0; axis < 2 && !accepted; ++axis)
            {
                std::set<unsigned int> coordinateSet;
                for (const auto &node : base.nodes)
                {
                    coordinateSet.insert(axis == 0 ? node.second.first : node.second.second);
                }
                if (coordinateSet.size() < 2)
                {
                    continue;
                }
                std::vector<unsigned int> cuts;
                const unsigned int low = *coordinateSet.begin();
                const unsigned int high = *coordinateSet.rbegin();
                for (unsigned int cut = low; cut < high; ++cut)
                {
                    cuts.push_back(cut);
                }
                const double center = (static_cast<double>(low) + high) / 2.0;
                std::stable_sort(cuts.begin(), cuts.end(), [center](unsigned int left, unsigned int right) {
                    return std::abs(static_cast<double>(left) - center) <
                           std::abs(static_cast<double>(right) - center);
                });
                if (cuts.size() > 12)
                {
                    cuts.resize(12);
                }

                for (const unsigned int cut : cuts)
                {
                    if (budgetExpired() || evaluatedCuts >= maxEvaluatedCuts)
                    {
                        restore(base);
                        return acceptedCuts;
                    }
                    ++evaluatedCuts;
                    restore(base);
                    for (auto &node : nodeIndex_pos)
                    {
                        unsigned int &coordinate = axis == 0 ? node.second.first : node.second.second;
                        if (coordinate > cut)
                        {
                            --coordinate;
                        }
                    }
                    if (!collisionFree(nodeIndex_pos))
                    {
                        continue;
                    }
                    // Compaction is a local refinement, not another exhaustive
                    // placement search.  Deterministic phase offsets keep every
                    // cut bounded; an unsuccessful cut is simply rejected.
                    const bool smallGraph = parse.getEffectiveEdges().size() <= 24;
                    if (!placeAndRoutePhaseAware(phaseCount,
                                                 maxSamePhase,
                                                 maxSearchCost,
                                                 smallGraph ? 12 : 6,
                                                 smallGraph))
                    {
                        continue;
                    }
                    if (score() < baseScore)
                    {
                        ++acceptedCuts;
                        accepted = true;
                        break;
                    }
                }
            }
            if (!accepted)
            {
                restore(base);
                break;
            }
        }
        return acceptedCuts;
    }

    int CircuitGraph::compactClockPhaseCycles(int phaseCount,
                                              int maxRounds)
    {
        struct Snapshot
        {
            std::map<int, position> nodes;
            std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routedEdges;
            std::unordered_map<position, GridCell, PositionHash> cells;
        };
        const auto takeSnapshot = [this]() {
            return Snapshot{nodeIndex_pos, routes, chessboard.gridMap};
        };
        const auto restore = [this](const Snapshot &snapshot) {
            nodeIndex_pos = snapshot.nodes;
            grid_positions = snapshot.nodes;
            routes = snapshot.routedEdges;
            chessboard.gridMap = snapshot.cells;
        };
        const auto occupiedScore = [this]() {
            bool initialized = false;
            unsigned int minX = 0, maxX = 0, minY = 0, maxY = 0;
            for (const auto &cell : chessboard.gridMap)
            {
                if (cell.second.get_current_weight() == 0)
                {
                    continue;
                }
                if (!initialized)
                {
                    minX = maxX = cell.first.first;
                    minY = maxY = cell.first.second;
                    initialized = true;
                }
                minX = std::min(minX, cell.first.first);
                maxX = std::max(maxX, cell.first.first);
                minY = std::min(minY, cell.first.second);
                maxY = std::max(maxY, cell.first.second);
            }
            if (!initialized)
            {
                return std::make_tuple(std::numeric_limits<int>::max(),
                                       std::numeric_limits<int>::max());
            }
            const int width = static_cast<int>(maxX - minX + 1);
            const int height = static_cast<int>(maxY - minY + 1);
            return std::make_tuple(width * height, std::max(width, height));
        };
        const auto fourConnected = [](const std::vector<position> &path) {
            if (path.size() < 2)
            {
                return false;
            }
            std::set<position> unique;
            for (std::size_t index = 0; index < path.size(); ++index)
            {
                if (!unique.insert(path[index]).second)
                {
                    return false;
                }
                if (index == 0)
                {
                    continue;
                }
                const position &previous = path[index - 1];
                const position &current = path[index];
                const unsigned int distance =
                    (previous.first > current.first ? previous.first - current.first
                                                    : current.first - previous.first) +
                    (previous.second > current.second ? previous.second - current.second
                                                      : current.second - previous.second);
                if (distance != 1)
                {
                    return false;
                }
            }
            return true;
        };
        const auto collisionFree = [](const std::map<int, position> &nodes) {
            std::set<position> occupied;
            for (const auto &node : nodes)
            {
                if (!occupied.insert(node.second).second)
                {
                    return false;
                }
            }
            return true;
        };
        const auto rebuildAndValidate = [this, phaseCount, &fourConnected]() {
            std::set<position> nodeCells;
            chessboard.gridMap.clear();
            for (const auto &node : nodeIndex_pos)
            {
                if (!nodeCells.insert(node.second).second ||
                    !chessboard.is_placeNode(node.second))
                {
                    return false;
                }
                chessboard.placeNode(node.second);
            }

            std::map<position, std::set<unsigned int>> routeSources;
            for (const auto &route : routes)
            {
                if (!fourConnected(route.second) ||
                    route.second.front() != nodeIndex_pos.at(static_cast<int>(route.first.first)) ||
                    route.second.back() != nodeIndex_pos.at(static_cast<int>(route.first.second)))
                {
                    return false;
                }
                for (const position &pos : route.second)
                {
                    routeSources[pos].insert(route.first.first);
                }
            }
            for (const auto &usage : routeSources)
            {
                if (nodeCells.count(usage.first) != 0)
                {
                    continue;
                }
                if (usage.second.size() > 2)
                {
                    return false;
                }
                for (std::size_t count = 0; count < usage.second.size(); ++count)
                {
                    if (!chessboard.is_addWire(usage.first))
                    {
                        return false;
                    }
                    chessboard.addWire(usage.first);
                }
            }

            std::string ownershipError;
            if (!validateJuneFanoutTrees(routes, ownershipError) ||
                !assignPhases(phaseCount) ||
                !validateAssignedRoutePhases(phaseCount) ||
                !hasAcceptableAssignedRoutePhases(phaseCount))
            {
                return false;
            }
            std::vector<std::vector<position>> routeGeometry;
            routeGeometry.reserve(routes.size());
            for (const auto &route : routes)
            {
                routeGeometry.push_back(route.second);
            }
            Mapping mapping;
            mapping.mapping_line(routeGeometry);
            std::string crossoverError;
            return mapping.validate_crossovers(&crossoverError);
        };

        phaseCount = std::max(2, phaseCount);
        maxRounds = std::max(0, maxRounds);
        int acceptedCycles = 0;
        for (int round = 0; round < maxRounds; ++round)
        {
            const Snapshot base = takeSnapshot();
            const auto baseScore = occupiedScore();
            bool accepted = false;
            for (int axis = 0; axis < 2 && !accepted; ++axis)
            {
                unsigned int low = std::numeric_limits<unsigned int>::max();
                unsigned int high = 0;
                for (const auto &cell : base.cells)
                {
                    if (cell.second.get_current_weight() == 0)
                    {
                        continue;
                    }
                    const unsigned int coordinate = axis == 0
                        ? cell.first.first : cell.first.second;
                    low = std::min(low, coordinate);
                    high = std::max(high, coordinate);
                }
                if (low == std::numeric_limits<unsigned int>::max() ||
                    high < low + static_cast<unsigned int>(phaseCount) + 1)
                {
                    continue;
                }

                std::vector<unsigned int> bandStarts;
                for (unsigned int start = low + 1;
                     start + static_cast<unsigned int>(phaseCount) <= high;
                     ++start)
                {
                    bool containsNode = false;
                    for (const auto &node : base.nodes)
                    {
                        const unsigned int coordinate = axis == 0
                            ? node.second.first : node.second.second;
                        if (coordinate >= start &&
                            coordinate < start + static_cast<unsigned int>(phaseCount))
                        {
                            containsNode = true;
                            break;
                        }
                    }
                    if (!containsNode)
                    {
                        bandStarts.push_back(start);
                    }
                }
                const double center = (static_cast<double>(low) + high) / 2.0;
                std::stable_sort(bandStarts.begin(), bandStarts.end(),
                    [center](unsigned int left, unsigned int right) {
                        return std::abs(static_cast<double>(left) - center) <
                               std::abs(static_cast<double>(right) - center);
                    });
                if (bandStarts.size() > 6)
                {
                    bandStarts.resize(6);
                }

                for (const unsigned int start : bandStarts)
                {
                    restore(base);
                    const unsigned int end = start + static_cast<unsigned int>(phaseCount);
                    for (auto &node : nodeIndex_pos)
                    {
                        unsigned int &coordinate = axis == 0
                            ? node.second.first : node.second.second;
                        if (coordinate >= end)
                        {
                            coordinate -= static_cast<unsigned int>(phaseCount);
                        }
                    }

                    bool validGeometry = true;
                    for (auto &route : routes)
                    {
                        std::vector<position> compacted;
                        compacted.reserve(route.second.size());
                        for (position pos : route.second)
                        {
                            unsigned int &coordinate = axis == 0 ? pos.first : pos.second;
                            if (coordinate >= start && coordinate < end)
                            {
                                continue;
                            }
                            if (coordinate >= end)
                            {
                                coordinate -= static_cast<unsigned int>(phaseCount);
                            }
                            if (compacted.empty() || compacted.back() != pos)
                            {
                                compacted.push_back(pos);
                            }
                        }
                        if (!fourConnected(compacted))
                        {
                            validGeometry = false;
                            break;
                        }
                        route.second = std::move(compacted);
                    }
                    grid_positions = nodeIndex_pos;
                    if (validGeometry && rebuildAndValidate() &&
                        occupiedScore() < baseScore)
                    {
                        ++acceptedCycles;
                        accepted = true;
                        break;
                    }

                    // A complete clock cycle may also be removed when the old
                    // wires bend inside the band. Keep the phase-equivalent
                    // node shift, then reroute transactionally instead of
                    // requiring the pre-compaction geometry to survive.
                    restore(base);
                    for (auto &node : nodeIndex_pos)
                    {
                        unsigned int &coordinate = axis == 0
                            ? node.second.first : node.second.second;
                        if (coordinate >= end)
                        {
                            coordinate -= static_cast<unsigned int>(phaseCount);
                        }
                    }
                    grid_positions = nodeIndex_pos;
                    if (!collisionFree(nodeIndex_pos) ||
                        !routeAndValidateJuneRandomClock(phaseCount, 6) ||
                        !(occupiedScore() < baseScore))
                    {
                        continue;
                    }
                    ++acceptedCycles;
                    accepted = true;
                    break;
                }
            }
            if (!accepted)
            {
                restore(base);
                break;
            }
        }
        return acceptedCycles;
    }
    // 计算层与层之间除了起始节点和最终节点之间的交叉节点，把有交叉的层分到同一组
    std::map<unsigned int, std::vector<std::vector<position>>> CircuitGraph::reclassifyLayers(
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>> &classifiedRoutes,
        std::map<unsigned int, std::vector<unsigned int>> &groupMapping)
    {
        // 为每一层计算内部节点集合（忽略起始和结束节点）
        std::map<unsigned int, std::unordered_set<position, PositionHash>> layerInteriorNodes;
        for (const auto &layerPair : classifiedRoutes)
        {
            unsigned int layer = layerPair.first;
            const auto &paths = layerPair.second;
            std::unordered_set<position, PositionHash> interiorSet;
            for (const auto &pathPair : paths)
            {
                const auto &nodeVec = pathPair.second;
                if (nodeVec.size() > 2)
                { // 至少有起点、中间节点、终点
                    // 将除首尾之外的所有节点加入集合
                    for (size_t i = 1; i < nodeVec.size() - 1; ++i)
                    {
                        interiorSet.insert(nodeVec[i]);
                    }
                }
            }
            layerInteriorNodes[layer] = interiorSet;
        }

        // 按照层号升序遍历，并根据相邻层内部节点是否交叉决定是否归入同一组
        std::map<unsigned int, unsigned int> layerGroup; // key: 原始层号, value: 分组号
        unsigned int currentGroup = 0;
        bool firstLayer = true;
        unsigned int prevLayer = 0;
        for (const auto &layerPair : classifiedRoutes)
        {
            unsigned int layer = layerPair.first;
            if (firstLayer)
            {
                layerGroup[layer] = currentGroup;
                firstLayer = false;
            }
            else
            {
                bool shareInterior = false;
                const auto &currSet = layerInteriorNodes[layer];
                const auto &prevSet = layerInteriorNodes[prevLayer];
                // 判断当前层与上一层是否有内部节点交集
                for (const auto &node : currSet)
                {
                    if (prevSet.find(node) != prevSet.end())
                    {
                        shareInterior = true;
                        break;
                    }
                }
                if (shareInterior)
                {
                    // 与上一层交叉则归为同一组
                    layerGroup[layer] = layerGroup[prevLayer];
                }
                else
                {
                    // 否则新起一个组
                    currentGroup++;
                    layerGroup[layer] = currentGroup;
                }
            }
            prevLayer = layer;
        }

        // 构造 groupMapping 容器，将原始层映射到对应的分组
        for (const auto &p : layerGroup)
        {
            unsigned int layer = p.first;
            unsigned int group = p.second;
            groupMapping[group].push_back(layer);
        }

        // 根据每层所属分组构造新的容器，将同组的层路径放到一起
        std::map<unsigned int, std::vector<std::vector<position>>> classifiedRoutes_New;
        for (const auto &layerPair : classifiedRoutes)
        {
            unsigned int layer = layerPair.first;
            unsigned int group = layerGroup[layer];
            // 将当前层的所有路径放入对应组中
            for (const auto &pathPair : layerPair.second)
            {
                classifiedRoutes_New[group].push_back(pathPair.second);
            }
        }

        return classifiedRoutes_New;
    }

    // 打印 groupMapping
    void CircuitGraph::printGroupMapping(const std::map<unsigned int, std::vector<unsigned int>> &groupMapping)
    {
        for (const auto &pair : groupMapping)
        {
            std::cout << "Group " << pair.first << ": ";
            for (size_t i = 0; i < pair.second.size(); ++i)
            {
                std::cout << pair.second[i];
                if (i < pair.second.size() - 1)
                    std::cout << ", ";
            }
            std::cout << std::endl;
        }
    }

    void CircuitGraph::printClassifiedRoutes(
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>> &classifiedRoutes)
    {
        for (const auto &layerPair : classifiedRoutes)
        {
            unsigned int layer = layerPair.first;
            std::cout << "Layer " << layer << ":\n";
            const auto &paths = layerPair.second;
            for (const auto &pathPair : paths)
            {
                const auto &startEnd = pathPair.first;
                std::cout << "  Path (Start: " << startEnd.first << ", End: " << startEnd.second << ") -> Nodes: [";
                const auto &nodes = pathPair.second;
                for (size_t i = 0; i < nodes.size(); ++i)
                {
                    std::cout << "(" << nodes[i].first << "," << nodes[i].second << ")";
                    if (i < nodes.size() - 1)
                    {
                        std::cout << ", ";
                    }
                }
                std::cout << "]\n";
            }
            std::cout << std::endl;
        }
    }

    bool CircuitGraph::assignPhases(int phaseCount)
    {
        // 使用 map 来存储分类的路径，按层级分类
        std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>> classifiedRoutes;
        // 存放新层对应的旧层
        std::map<unsigned int, std::vector<unsigned int>> groupMapping;
        // 将 routes 按照层级分类
        for (const auto &route : routes)
        {
            unsigned int layer = parse.getVertexLayer(route.first.first);
            classifiedRoutes[layer][route.first] = route.second;
        }

        auto classifiedRoutes_New = reclassifyLayers(classifiedRoutes, groupMapping);

        bool is_first_layer = true;

        // 遍历所有层
        for (auto &[layer_id, routes] : classifiedRoutes_New)
        {
            std::vector<Path> paths;
            std::vector<int> start_phases; // 存储当前层每条路径的起始相位

            // 遍历当前层的所有路径
            for (auto &path_positions : routes)
            {
                std::vector<std::pair<int, int>> decodedPath;

                // 构建路径
                for (auto pos : path_positions)
                {
                    decodedPath.push_back({static_cast<int>(pos.first), static_cast<int>(pos.second)});
                }

                paths.push_back(Path{decodedPath});

                int start_phase = -1;
                if (is_first_layer)
                {
                    start_phase = -1;
                }
                else
                {
                    auto start_pos = path_positions[0];
                    start_phase = chessboard.gridMap[start_pos].getPhase();
                }
                start_phases.push_back(start_phase);
            }
            // 调用相位优化函数
            bool success = false;
            try
            {
                success = phaseOptimize(layer_id, paths, start_phases, phaseCount);
            }
            catch (const std::exception &)
            {
                success = false;
            }
            if (!success)
            {
                return assignPhasesFallback(phaseCount);
            }

            is_first_layer = false;
        }

        if (validateAssignedRoutePhases(phaseCount) && hasAcceptableAssignedRoutePhases(phaseCount))
        {
            return true;
        }
        return assignPhasesFallback(phaseCount);
    }

    bool CircuitGraph::phaseOptimize(int current_layer, std::vector<fcngraph::Path> &paths, std::vector<int> &start_phases, int phaseCount, int recursion_count)
    {
        PhaseSolver solver(paths, start_phases, phaseCount);
        std::vector<std::vector<int>> optimized_phases = solver.solve();
        std::string message = "Layer " + std::to_string(current_layer) + " optimized at recursion level " + std::to_string(recursion_count);
        if (fitnessCallback)
        {
            fitnessCallback(message);
        }

        // std::cout << "layer " << current_layer  <<std::endl;
        // optimizer.printSolution();

        // 确保两个vector大小相同，才进行处理
        if (paths.size() != optimized_phases.size())
        {
            std::string message = "The number of paths and the number of phase sets do not match!";
            if (fitnessCallback)
            {
                fitnessCallback(message);
            }
            return false;
        }

        std::set<position> recordSetedPhasePos;

        for (size_t i = 0; i < paths.size(); ++i)
        {
            const Path &path = paths[i];
            const std::vector<int> &phases = optimized_phases[i];

            std::string message = " path " + std::to_string(i) + " start from (" + std::to_string(path.grids[0].first) + "," + std::to_string(path.grids[0].second) + ")";
            if (fitnessCallback)
            {
                fitnessCallback(message);
            }

            if (path.grids.size() != phases.size())
            {
                std::string message = "Mismatch in grid and phase sizes for path " + std::to_string(i);
                if (fitnessCallback)
                {
                    fitnessCallback(message);
                }
                continue;
            }

            for (size_t j = 0; j < path.grids.size(); ++j)
            {
                const std::pair<int, int> &grid = path.grids[j];
                position pos{static_cast<unsigned int>(grid.first), static_cast<unsigned int>(grid.second)};
                int phase = phases[j];

                if (chessboard.gridMap.find(pos) != chessboard.gridMap.end())
                {
                    auto tempPhase = chessboard.gridMap[pos].getPhase();
                    if (tempPhase == -1)
                    {
                        chessboard.gridMap[pos].setPhase(phase);
                        recordSetedPhasePos.insert(pos);
                    }
                    else if (tempPhase == phase)
                    {
                        continue;
                    }
                    else
                    {
                        std::string message = "Conflict in phase assignment for path " + std::to_string(i) + " at position (" + std::to_string(pos.first) + "," + std::to_string(pos.second) + ")";
                        if (fitnessCallback)
                        {
                            fitnessCallback(message);
                        }
                        for (auto &recorded_pos : recordSetedPhasePos)
                        {
                            chessboard.gridMap[recorded_pos].setPhase(-1);
                        }
                        return false;
                    }
                }
                else
                {
                    std::string message = "BIG ERROR!!! Position (" + std::to_string(pos.first) + "," + std::to_string(pos.second) + ") not found in gridMap for path " + std::to_string(i);
                    if (fitnessCallback)
                    {
                        fitnessCallback(message);
                    }
                    return false;
                }
            }
        }

        return true; // 如果所有路径都成功优化，返回 true
    }

    bool CircuitGraph::assignPhasesFallback(int phaseCount)
    {
        phaseCount = std::max(2, phaseCount);

        for (auto &cell : chessboard.gridMap)
        {
            if (cell.second.getPhase() >= 1)
            {
                cell.second.setPhase(-1);
            }
        }

        std::vector<std::pair<std::pair<unsigned int, unsigned int>, std::vector<position>>> orderedRoutes(routes.begin(), routes.end());
        std::stable_sort(orderedRoutes.begin(), orderedRoutes.end(), [this](const auto &lhs, const auto &rhs) {
            const int lhsLayer = parse.getVertexLayer(lhs.first.first);
            const int rhsLayer = parse.getVertexLayer(rhs.first.first);
            if (lhsLayer != rhsLayer)
            {
                return lhsLayer < rhsLayer;
            }
            return lhs.second.size() > rhs.second.size();
        });

        const auto getPhaseAt = [this](const position &pos) -> int {
            auto cell = chessboard.gridMap.find(pos);
            if (cell == chessboard.gridMap.end())
            {
                return -1;
            }
            return cell->second.getPhase();
        };

        const auto setPhaseAt = [this](const position &pos, int phase) -> bool {
            auto cell = chessboard.gridMap.find(pos);
            if (cell == chessboard.gridMap.end())
            {
                return false;
            }
            int existingPhase = cell->second.getPhase();
            if (existingPhase >= 1 && existingPhase != phase)
            {
                return false;
            }
            cell->second.setPhase(phase);
            return true;
        };

        for (const auto &route : orderedRoutes)
        {
            if (route.second.empty())
            {
                continue;
            }

            std::vector<int> fixedPhases;
            fixedPhases.reserve(route.second.size());
            bool hasFixedPhase = false;
            for (const position &pos : route.second)
            {
                const int phase = getPhaseAt(pos);
                fixedPhases.push_back(phase);
                if (phase >= 1)
                {
                    hasFixedPhase = true;
                }
            }

            const int preferredStartPhase = hasFixedPhase
                ? -1
                : phaseAfter(1, parse.getVertexLayer(route.first.first), phaseCount);

            std::vector<int> phases;
            try
            {
                phases = solveForwardPhasePath(fixedPhases, phaseCount, preferredStartPhase);
            }
            catch (const std::exception &)
            {
                return assignRouteConstraintPhases(phaseCount);
            }

            for (std::size_t i = 0; i < route.second.size(); ++i)
            {
                if (!setPhaseAt(route.second[i], phases[i]))
                {
                    return assignRouteConstraintPhases(phaseCount);
                }
            }
        }

        if (validateAssignedRoutePhases(phaseCount) && hasAcceptableAssignedRoutePhases(phaseCount))
        {
            return true;
        }
        if (assignRouteConstraintPhases(phaseCount))
        {
            return true;
        }
        return validateAssignedRoutePhases(phaseCount);
    }

    bool CircuitGraph::assignRouteConstraintPhases(int phaseCount)
    {
        phaseCount = std::max(2, phaseCount);

        struct ConstraintEdge
        {
            int from = 0;
            int to = 0;
        };

        struct PhaseScore
        {
            int invalid = 0;
            int maxRun = 1;
            int waits = 0;
        };

        std::map<position, int> positionIndex;
        std::vector<position> positions;
        const auto getPositionIndex = [&positionIndex, &positions](const position &pos) {
            auto iter = positionIndex.find(pos);
            if (iter != positionIndex.end())
            {
                return iter->second;
            }
            const int index = static_cast<int>(positions.size());
            positionIndex[pos] = index;
            positions.push_back(pos);
            return index;
        };

        for (auto &cell : chessboard.gridMap)
        {
            if (cell.second.get_current_weight() > 0)
            {
                getPositionIndex(cell.first);
            }
        }

        std::vector<ConstraintEdge> constraintEdges;
        std::vector<std::vector<int>> routePositionIndexes;
        for (const auto &route : routes)
        {
            const auto &path = route.second;
            std::vector<int> routeIndexes;
            routeIndexes.reserve(path.size());
            for (std::size_t i = 0; i < path.size(); ++i)
            {
                routeIndexes.push_back(getPositionIndex(path[i]));
                if (i > 0)
                {
                    constraintEdges.push_back({
                        routeIndexes[i - 1],
                        routeIndexes[i]
                    });
                }
            }
            routePositionIndexes.push_back(std::move(routeIndexes));
        }

        if (positions.empty())
        {
            return false;
        }

        const auto transitionCost = [phaseCount](int fromPhase, int toPhase) {
            if (toPhase == phaseAfter(fromPhase, 1, phaseCount))
            {
                return 0;
            }
            if (toPhase == fromPhase)
            {
                return 1;
            }
            return 1000000;
        };

        const auto scorePhases = [&constraintEdges, &routePositionIndexes, &transitionCost](const std::vector<int> &phases) {
            PhaseScore score;
            for (const auto &edge : constraintEdges)
            {
                const int cost = transitionCost(phases[edge.from], phases[edge.to]);
                if (cost >= 1000000)
                {
                    ++score.invalid;
                }
                else if (cost > 0)
                {
                    ++score.waits;
                }
            }
            for (const auto &path : routePositionIndexes)
            {
                int currentRun = 1;
                for (std::size_t i = 1; i < path.size(); ++i)
                {
                    if (phases[path[i]] == phases[path[i - 1]])
                    {
                        ++currentRun;
                        score.maxRun = std::max(score.maxRun, currentRun);
                    }
                    else
                    {
                        currentRun = 1;
                    }
                }
            }
            return score;
        };

        const auto isBetterScore = [](const PhaseScore &candidate, const PhaseScore &current) {
            return std::tie(candidate.invalid, candidate.maxRun, candidate.waits)
                 < std::tie(current.invalid, current.maxRun, current.waits);
        };

        std::vector<std::vector<int>> incidentEdges(positions.size());
        for (std::size_t edgeIndex = 0; edgeIndex < constraintEdges.size(); ++edgeIndex)
        {
            const auto &edge = constraintEdges[edgeIndex];
            incidentEdges[edge.from].push_back(static_cast<int>(edgeIndex));
            incidentEdges[edge.to].push_back(static_cast<int>(edgeIndex));
        }

        const auto localCost = [&constraintEdges, &transitionCost](const std::vector<int> &phases, int edgeIndex) {
            const auto &edge = constraintEdges[static_cast<std::size_t>(edgeIndex)];
            return transitionCost(phases[edge.from], phases[edge.to]);
        };

        const auto improve = [&](std::vector<int> phases) {
            const std::size_t maxPasses = std::max<std::size_t>(32, std::min<std::size_t>(512, phases.size() * 4));
            for (std::size_t pass = 0; pass < maxPasses; ++pass)
            {
                bool changed = false;
                for (std::size_t index = 0; index < positions.size(); ++index)
                {
                    int currentPhase = phases[index];
                    int bestPhase = currentPhase;
                    int bestCost = 0;
                    for (int edgeIndex : incidentEdges[index])
                    {
                        bestCost += localCost(phases, edgeIndex);
                    }

                    const int preferredPhase = static_cast<int>(positions[index].second % static_cast<unsigned int>(phaseCount)) + 1;
                    for (int phase = 1; phase <= phaseCount; ++phase)
                    {
                        phases[index] = phase;
                        int cost = 0;
                        for (int edgeIndex : incidentEdges[index])
                        {
                            cost += localCost(phases, edgeIndex);
                        }
                        if (cost < bestCost || (cost == bestCost && phase == preferredPhase && bestPhase != preferredPhase))
                        {
                            bestCost = cost;
                            bestPhase = phase;
                        }
                    }

                    phases[index] = bestPhase;
                    if (bestPhase != currentPhase)
                    {
                        changed = true;
                    }
                }

                if (!changed)
                {
                    break;
                }
            }
            return phases;
        };

        std::vector<std::vector<int>> seeds;
        for (int phase = 1; phase <= phaseCount; ++phase)
        {
            seeds.push_back(std::vector<int>(positions.size(), phase));
        }
        for (int offset = 0; offset < phaseCount; ++offset)
        {
            std::vector<int> ySeed(positions.size());
            std::vector<int> diagonalSeed(positions.size());
            for (std::size_t index = 0; index < positions.size(); ++index)
            {
                const int x = static_cast<int>(positions[index].first);
                const int y = static_cast<int>(positions[index].second);
                ySeed[index] = ((y + offset) % phaseCount) + 1;
                diagonalSeed[index] = ((x + y + offset) % phaseCount) + 1;
            }
            seeds.push_back(std::move(ySeed));
            seeds.push_back(std::move(diagonalSeed));
        }

        std::vector<int> voteSeed(positions.size(), 0);
        std::vector<std::vector<int>> phaseVotes(positions.size(), std::vector<int>(phaseCount + 1, 0));
        for (const auto &route : routes)
        {
            const auto &path = route.second;
            for (std::size_t i = 0; i < path.size(); ++i)
            {
                const int index = positionIndex[path[i]];
                const int phase = phaseAfter(1, static_cast<int>(i), phaseCount);
                ++phaseVotes[static_cast<std::size_t>(index)][phase];
            }
        }
        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            int bestPhase = static_cast<int>(positions[index].second % static_cast<unsigned int>(phaseCount)) + 1;
            int bestVotes = -1;
            for (int phase = 1; phase <= phaseCount; ++phase)
            {
                if (phaseVotes[index][phase] > bestVotes)
                {
                    bestVotes = phaseVotes[index][phase];
                    bestPhase = phase;
                }
            }
            voteSeed[index] = bestPhase;
        }
        seeds.push_back(std::move(voteSeed));

        std::vector<int> bestPhases;
        PhaseScore bestScore{
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max(),
            std::numeric_limits<int>::max()
        };
        for (const auto &seed : seeds)
        {
            auto candidate = improve(seed);
            const auto candidateScore = scorePhases(candidate);
            if (bestPhases.empty() || isBetterScore(candidateScore, bestScore))
            {
                bestScore = candidateScore;
                bestPhases = std::move(candidate);
            }
        }

        if (bestPhases.empty() || bestScore.invalid > 0)
        {
            return false;
        }

        std::set<int> usedPhases(bestPhases.begin(), bestPhases.end());
        if (usedPhases.size() < 2 && positions.size() > 1)
        {
            return false;
        }

        for (auto &cell : chessboard.gridMap)
        {
            if (cell.second.getPhase() >= 1)
            {
                cell.second.setPhase(-1);
            }
        }

        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            auto cell = chessboard.gridMap.find(positions[index]);
            if (cell == chessboard.gridMap.end())
            {
                return false;
            }
            cell->second.setPhase(bestPhases[index]);
        }

        return validateAssignedRoutePhases(phaseCount);
    }

    bool CircuitGraph::validateAssignedRoutePhases(int phaseCount) const
    {
        phaseCount = std::max(2, phaseCount);

        for (const auto &route : routes)
        {
            const auto &path = route.second;
            if (path.empty())
            {
                continue;
            }

            for (std::size_t i = 1; i < path.size(); ++i)
            {
                auto prevCell = chessboard.gridMap.find(path[i - 1]);
                auto currCell = chessboard.gridMap.find(path[i]);
                if (prevCell == chessboard.gridMap.end() || currCell == chessboard.gridMap.end())
                {
                    return false;
                }

                const int prevPhase = prevCell->second.getPhase();
                const int currPhase = currCell->second.getPhase();
                if (prevPhase < 1 || currPhase < 1)
                {
                    return false;
                }
                if (!isForwardPhaseStep(prevPhase, currPhase, phaseCount))
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool CircuitGraph::hasAcceptableAssignedRoutePhases(int phaseCount) const
    {
        phaseCount = std::max(2, phaseCount);
        const int maxRunLimit = std::max(phaseCount + 2, phaseCount * 2 + 2);

        int repeatedAdjacent = 0;
        int totalAdjacent = 0;
        int maxRun = 1;

        for (const auto &route : routes)
        {
            int previousPhase = -1;
            int currentRun = 1;
            for (const position &pos : route.second)
            {
                auto cell = chessboard.gridMap.find(pos);
                if (cell == chessboard.gridMap.end() || cell->second.getPhase() < 1)
                {
                    return false;
                }

                const int phase = cell->second.getPhase();
                if (previousPhase >= 1)
                {
                    ++totalAdjacent;
                    if (phase == previousPhase)
                    {
                        ++repeatedAdjacent;
                        ++currentRun;
                        maxRun = std::max(maxRun, currentRun);
                    }
                    else
                    {
                        currentRun = 1;
                    }
                }
                previousPhase = phase;
            }
        }

        if (maxRun > maxRunLimit)
        {
            return false;
        }
        if (totalAdjacent == 0)
        {
            return true;
        }
        return repeatedAdjacent * 10 <= totalAdjacent * 3;
    }

    void CircuitGraph::printLaTex(const std::string &outputPath,
                                  bool use2ddStyle)
    {
        const std::string filename = outputPath.empty()
            ? parse.get_moduleName() + ".tex"
            : outputPath;
        std::ofstream os(filename);
        if (!os.is_open())
        {
            std::cerr << "Error opening file!" << std::endl;
            return;
        }

        os << R"(\documentclass[tikz]{standalone}
            %graphics
            \usepackage{pgfmath}
            \usetikzlibrary{calc,arrows.meta}
            \definecolor{ifcnink}{HTML}{23344A}
            \definecolor{ifcnaccent}{HTML}{2457A6}
            \definecolor{ifcnguide}{HTML}{EAF0F5}
            \definecolor{ifcnedge}{HTML}{A7B3C3}
            \definecolor{ifcninput}{HTML}{DCEAF7}
            \definecolor{ifcnoutput}{HTML}{F8E8C9}
            \definecolor{ifcnmajority}{HTML}{F2D58A}
            \definecolor{ifcnand}{HTML}{F2C9CB}
            \definecolor{ifcnor}{HTML}{E8DDF3}
            \definecolor{ifcnnot}{HTML}{DDF1E3}
            \definecolor{ifcnfanout}{HTML}{CDEBDD}
            \definecolor{ifcnwire}{HTML}{E7EBEF}

            \begin{document}
            \begin{tikzpicture}[)"
           << (use2ddStyle ? R"(
            scale=0.5,transform shape,
            guide/.style={draw=ifcnguide,line width=0.45pt},
            logicadj/.style={draw=ifcnedge,line width=0.65pt},
            logiclong/.style={draw=ifcnedge!70,line width=0.65pt,dashed},
            route/.style={->, >={Stealth[]},line width=0.85pt,draw=ifcnaccent!78},
            gate/.style={circle,draw=ifcnink,line width=0.8pt,minimum size=0.76cm,
                         inner sep=0pt,font=\bfseries},
            ninput/.style={gate,fill=ifcninput},
            noutput/.style={gate,fill=ifcnoutput},
            nmaj/.style={gate,fill=ifcnmajority},
            nand/.style={gate,fill=ifcnand},
            nor/.style={gate,fill=ifcnor},
            nnot/.style={gate,fill=ifcnnot},
            nfanout/.style={gate,fill=ifcnfanout},
            nwire/.style={gate,fill=ifcnwire},
            nother/.style={gate,fill=white}
            )"
                              : R"(
            scale=0.5,transform shape,
            c1/.style={rectangle, fill, lightgray!50, minimum size=1cm},
            c2/.style={rectangle, fill, lightgray, minimum size=1cm},
            c3/.style={rectangle, fill, gray, minimum size=1cm},
            c4/.style={rectangle, fill, darkgray!90, minimum size=1cm},
            route/.style={->, >={Stealth[]},line width=0.8pt, blue!50},
            v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm}
            )")
           << R"(]
            )"
           << std::endl;

        unsigned int maxLayoutY = 0;
        unsigned int minLayoutX = std::numeric_limits<unsigned int>::max();
        unsigned int maxLayoutX = 0;
        for (const auto &cell : chessboard.getGridMap())
        {
            if (cell.second.get_current_weight() == 0)
            {
                continue;
            }
            maxLayoutY = std::max(maxLayoutY, cell.first.second);
            minLayoutX = std::min(minLayoutX, cell.first.first);
            maxLayoutX = std::max(maxLayoutX, cell.first.first);
        }
        for (const auto &node : nodeIndex_pos)
        {
            maxLayoutY = std::max(maxLayoutY, node.second.second);
            minLayoutX = std::min(minLayoutX, node.second.first);
            maxLayoutX = std::max(maxLayoutX, node.second.first);
        }
        if (minLayoutX == std::numeric_limits<unsigned int>::max())
        {
            minLayoutX = 0;
        }
        const auto drawY = [maxLayoutY](unsigned int y) {
            // TikZ uses an upward Y axis; this is a renderer-only conversion
            // from iFCN's top-left layout coordinates.
            return maxLayoutY - y;
        };

        if (use2ddStyle)
        {
            std::set<unsigned int> layerRows;
            for (const auto &node : nodeIndex_pos)
            {
                layerRows.insert(node.second.second);
            }
            const double guideLeft = static_cast<double>(minLayoutX) - 0.65;
            const double guideRight = static_cast<double>(maxLayoutX) + 0.65;
            for (const unsigned int row : layerRows)
            {
                os << "\\draw[guide] (" << guideLeft << "," << drawY(row)
                   << ") -- (" << guideRight << "," << drawY(row) << ");"
                   << std::endl;
            }

        }
        else
        {
            for (auto it = chessboard.getGridMap().begin(); it != chessboard.getGridMap().end(); ++it)
            {
                auto pos = it->first;
                auto grid = it->second;
                int phase = grid.getPhase();
                auto x = pos.first;
                auto y = drawY(pos.second);
                switch (phase)
                {
                case 1:
                    os << "\\node[c1] at (" << x << "," << y << ") {};" << std::endl;
                    break;
                case 2:
                    os << "\\node[c2] at (" << x << "," << y << ") {};" << std::endl;
                    break;
                case 3:
                    os << "\\node[c3] at (" << x << "," << y << ") {};" << std::endl;
                    break;
                case 4:
                    os << "\\node[c4] at (" << x << "," << y << ") {};" << std::endl;
                    break;
                default:
                    break;
                }
            }
        }

        os << R"(%nodes and edges)" << std::endl;
        for (auto &index_pos : nodeIndex_pos)
        {
            auto node = index_pos.first;
            auto x = index_pos.second.first;
            auto y = drawY(index_pos.second.second);
            auto nodeType = parse.getNodeType(node);
            std::string nodeName = escapeLatexText(parse.getNodeName(node));
            if (use2ddStyle)
            {
                std::string style = "nother";
                if (nodeType == "input")
                    style = "ninput";
                else if (nodeType == "output")
                    style = "noutput";
                else if (nodeType == "maj")
                    style = "nmaj";
                else if (nodeType == "and")
                    style = "nand";
                else if (nodeType == "or")
                    style = "nor";
                else if (nodeType == "not")
                    style = "nnot";
                else if (nodeType == "fanout")
                    style = "nfanout";
                else if (nodeType == "wire")
                    style = "nwire";
                os << "\\node[" << style << "] (" << node << ") at ("
                   << x << "," << y << ") {" << node << "};" << std::endl;
                continue;
            }
            if (nodeType == "input")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << nodeName << "};" << std::endl;
            }
            else if (nodeType == "output")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << nodeName << "};" << std::endl;
            }
            else if (nodeType == "maj")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "M" << "};" << std::endl;
            }
            else if (nodeType == "and")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "\\&" << "};" << std::endl;
            }
            else if (nodeType == "or")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "\\textbar" << "};" << std::endl;
            }
            else if (nodeType == "not")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "$\\neg$" << "};" << std::endl;
            }
            else if (nodeType == "wire")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "w" << "};" << std::endl;
            }
            else if (nodeType == "fanout")
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "F" << "};" << std::endl;
            }
            else
            {
                os << R"(\node()" << node << ") [v] at (" << x << "," << y << ") {" << "" << "};" << std::endl;
            }
        }

        os << std::endl;
        if (use2ddStyle && routes.empty())
        {
            for (const auto &edge : parse.getEffectiveEdges())
            {
                if (nodeIndex_pos.find(static_cast<int>(edge.first)) == nodeIndex_pos.end() ||
                    nodeIndex_pos.find(static_cast<int>(edge.second)) == nodeIndex_pos.end())
                {
                    continue;
                }
                const int layerSpan = std::abs(
                    static_cast<int>(parse.getVertexLayer(edge.second)) -
                    static_cast<int>(parse.getVertexLayer(edge.first)));
                os << "\\draw[" << (layerSpan > 1 ? "logiclong" : "logicadj")
                   << "] (" << edge.first << ") -- (" << edge.second << ");"
                   << std::endl;
            }
        }
        for (auto &route : routes)
        {
            auto node_pair = route.first;
            auto path = route.second;
            if (path.empty())
                continue;
            auto node_1 = node_pair.first;
            auto node_2 = node_pair.second;
            os << R"(\draw[route] )" << "(" << node_1 << ") -- ";

            for (size_t i = 1; i < path.size() - 1; ++i)
            {
                auto pos = path[i];
                auto x = pos.first;
                auto y = drawY(pos.second);
                os << "(" << x << "," << y << ") -- ";
            }
            os << "(" << node_2 << ");" << std::endl;
        }

        os << std::endl;

        os << R"(\end{tikzpicture}
    \end{document})"
           << std::endl;
    }
}
