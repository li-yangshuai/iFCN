#include "circuitGraph.h"
#include "autopr/algorithms/phaseSolver.h"
#include <set>

namespace fcngraph
{

    void CircuitGraph::setFitnessCallback(const std::function<void(std::string)> &callback)
    {
        fitnessCallback = callback;
    }

    void CircuitGraph::processAndGenerateGraph(bool printSVG, bool showCircuitLabel, bool isBox, bool isOGD)
    {

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
        agclose(A);
    }

    void CircuitGraph::sortNodesByYThenXCoordinate(double grid_size)
    {
        grid_positions.clear();
        nodeIndex_pos.clear();
        sorted_grid_positions.clear();

        if (grid_size <= 0.0)
        {
            grid_size = 30.0;
        }

        // 预处理，将节点坐标映射到网格坐标
        for (const auto &v : node_positions)
        {
            int node = v.first;
            double x = v.second.first;
            double y = v.second.second;
            int grid_x = static_cast<int>(std::round(x / grid_size)) + 20;
            int grid_y = static_cast<int>(std::round(y / grid_size)) + 20;
            position candidate{static_cast<unsigned int>(std::max(0, grid_x)),
                               static_cast<unsigned int>(std::max(0, grid_y))};
            while (std::find_if(grid_positions.begin(), grid_positions.end(),
                                [&candidate](const auto &entry) { return entry.second == candidate; }) != grid_positions.end())
            {
                ++candidate.first;
            }
            grid_positions[node] = candidate;
            nodeIndex_pos[node] = candidate;
        }


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
                                              unsigned int yPadding)
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
            std::sort(nodes.begin(), nodes.end(), [&graphvizXByNode](int lhs, int rhs) {
                const double lx = graphvizXByNode.count(lhs) ? graphvizXByNode[lhs] : static_cast<double>(lhs);
                const double rx = graphvizXByNode.count(rhs) ? graphvizXByNode[rhs] : static_cast<double>(rhs);
                if (lx == rx)
                {
                    return lhs < rhs;
                }
                return lx < rx;
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

    bool CircuitGraph::placeAndRoute()
    {
        routes.clear();
        astar.reset();

        for (auto &node_pos : nodeIndex_pos)
        {
            if (!chessboard.is_placeNode(node_pos.second))
            {
                return false;
            }
            chessboard.placeNode(node_pos.second);
        }

        auto edges = parse.getEffectiveEdges();
        std::stable_sort(edges.begin(), edges.end(), [this](const auto &lhs, const auto &rhs) {
            const auto lhsStart = nodeIndex_pos[lhs.first];
            const auto lhsEnd = nodeIndex_pos[lhs.second];
            const auto rhsStart = nodeIndex_pos[rhs.first];
            const auto rhsEnd = nodeIndex_pos[rhs.second];
            const int lhsDistance = std::abs(static_cast<int>(lhsEnd.first) - static_cast<int>(lhsStart.first))
                                  + std::abs(static_cast<int>(lhsEnd.second) - static_cast<int>(lhsStart.second));
            const int rhsDistance = std::abs(static_cast<int>(rhsEnd.first) - static_cast<int>(rhsStart.first))
                                  + std::abs(static_cast<int>(rhsEnd.second) - static_cast<int>(rhsStart.second));
            if (lhs.first == rhs.first)
            {
                return lhsDistance > rhsDistance;
            }
            if (parse.getVertexLayer(lhs.first) == parse.getVertexLayer(rhs.first))
            {
                return lhsDistance > rhsDistance;
            }
            return parse.getVertexLayer(lhs.first) < parse.getVertexLayer(rhs.first);
        });
        for (auto &edge : edges)
        {
            auto start = nodeIndex_pos[edge.first];
            auto end = nodeIndex_pos[edge.second];
            bool isOneFanout = false;
            if (parse.getm_vertexType(edge.first) == typeid(AndNode) ||
                parse.getm_vertexType(edge.first) == typeid(OrNode) ||
                parse.getm_vertexType(edge.first) == typeid(MajNode))
            {
                isOneFanout = true;
            }
            auto path = astar.findPath(start, end, isOneFanout);
            if (!path.empty())
            {
                routes.insert({edge, path});
            }
            else
            {
                return false;
            }
        }
        return true;
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

        return true; // 所有层都成功分配相位
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

        auto phaseAfter = [phaseCount](int startPhase, int steps) {
            int phase = (startPhase - 1 + steps) % phaseCount;
            if (phase < 0)
            {
                phase += phaseCount;
            }
            return phase + 1;
        };

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

        for (const auto &route : orderedRoutes)
        {
            if (route.second.empty())
            {
                continue;
            }

            int anchorPhase = chessboard.gridMap[route.second.front()].getPhase();
            if (anchorPhase < 1)
            {
                anchorPhase = phaseAfter(1, parse.getVertexLayer(route.first.first));
                chessboard.gridMap[route.second.front()].setPhase(anchorPhase);
            }
            std::size_t anchorIndex = 0;

            for (std::size_t i = 1; i < route.second.size(); ++i)
            {
                const auto &pos = route.second[i];
                const int existingPhase = chessboard.gridMap[pos].getPhase();
                const int idealPhase = phaseAfter(anchorPhase, static_cast<int>(i - anchorIndex));
                if (existingPhase < 1)
                {
                    chessboard.gridMap[pos].setPhase(idealPhase);
                }
                else if (existingPhase != idealPhase)
                {
                    anchorPhase = existingPhase;
                    anchorIndex = i;
                }
            }
        }

        return true;
    }

    void CircuitGraph::printLaTex()
    {
        std::string filename = parse.get_moduleName() + ".tex";
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

            \begin{document}
            \begin{tikzpicture}[
            scale=0.5,transform shape,
            c1/.style={rectangle, fill, lightgray!50, minimum size=1cm},
            c2/.style={rectangle, fill, lightgray, minimum size=1cm},
            c3/.style={rectangle, fill, gray, minimum size=1cm},
            c4/.style={rectangle, fill, darkgray!90, minimum size=1cm},
            route/.style={->, >={Stealth[]},line width=0.8pt, blue!50},
            v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm},
            ]
            )"
           << std::endl;

        for (auto it = chessboard.getGridMap().begin(); it != chessboard.getGridMap().end(); ++it)
        {
            auto pos = it->first;
            auto grid = it->second;
            int phase = grid.getPhase();
            auto x = pos.first;
            auto y = pos.second;
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

        os << R"(%nodes and edges)" << std::endl;
        for (auto &index_pos : nodeIndex_pos)
        {
            auto node = index_pos.first;
            auto x = index_pos.second.first;
            auto y = index_pos.second.second;
            auto nodeType = parse.getNodeType(node);
            std::string nodeName = parse.getNodeName(node);
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
                auto y = pos.second;
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
