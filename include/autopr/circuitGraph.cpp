#include "circuitGraph.h"
#include "phaseSolver.h"

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
                    // agsafeset(node, const_cast<char*>("label"), const_cast<char*>(""), const_cast<char*>(""));
                    continue;
                }
            }
        }

        // 设置边属性
        for (Agedge_t *edge = agfstedge(A, agfstnode(A)); edge; edge = agnxtedge(A, edge, agfstnode(A)))
        {
            agsafeset(edge, const_cast<char *>("splines"), const_cast<char *>("polyline"), const_cast<char *>(""));
            // 控制布局大小
            //  agsafeset(edge, const_cast<char*>("minlen"), const_cast<char*>("2"), const_cast<char*>(""));
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
        // 预处理，将节点坐标映射到网格坐标
        for (const auto &v : node_positions)
        {
            int node = v.first;
            double x = v.second.first;
            double y = v.second.second;
            int grid_x = static_cast<int>(x / 40) + 20; // grid_size会导致大规模电路节点间距过大，太大会导致节点位置重复
            int grid_y = static_cast<int>(y / 40) + 20;
            grid_positions[node] = std::make_pair(grid_x, grid_y);
            nodeIndex_morton[node] = MortonChessboard::calculateMortonCode(grid_x, grid_y);
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

    bool CircuitGraph::placeAndRoute()
    {
        for (auto &node_morton : nodeIndex_morton)
        {
            chessboard.placeNode(node_morton.second);
        }

        auto edges = parse.getEffectiveEdges();
        for (auto &edge : edges)
        {
            auto start = nodeIndex_morton[edge.first];
            auto end = nodeIndex_morton[edge.second];
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
    std::map<unsigned int, std::vector<std::vector<unsigned int>>> CircuitGraph::reclassifyLayers(
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>>> &classifiedRoutes,
        std::map<unsigned int, std::vector<unsigned int>> &groupMapping)
    {
        // 为每一层计算内部节点集合（忽略起始和结束节点）
        std::map<unsigned int, std::unordered_set<unsigned int>> layerInteriorNodes;
        for (const auto &layerPair : classifiedRoutes)
        {
            unsigned int layer = layerPair.first;
            const auto &paths = layerPair.second;
            std::unordered_set<unsigned int> interiorSet;
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
        std::map<unsigned int, std::vector<std::vector<unsigned int>>> classifiedRoutes_New;
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
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>>> &classifiedRoutes)
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
                    std::cout << nodes[i];
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

    bool CircuitGraph::assignPhases()
    {
        // 使用 map 来存储分类的路径，按层级分类
        std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>>> classifiedRoutes;
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

            auto a1 = classifiedRoutes[groupMapping[layer_id][0]].size();
            // 遍历当前层的所有路径
            for (auto &morton_positions : routes)
            {
                std::vector<std::pair<int, int>> decodedPath;

                // 解码 Mortons 并构建路径
                for (auto morton_pos : morton_positions)
                {
                    decodedPath.push_back(MortonChessboard::decodeMortonCode(morton_pos));
                }

                paths.push_back(Path{decodedPath});

                // 如果是第一层，初始化起始相位为1
                if (is_first_layer)
                {
                    if (start_phases.size() < a1)
                    {
                        start_phases.push_back(1);
                        chessboard.gridMap[morton_positions[0]].setPhase(1); // 初始化输入节点相位为1
                    }
                }
                else
                {
                    if (start_phases.size() < a1)
                    {
                        auto start_morton = morton_positions[0];
                        start_phases.push_back(chessboard.gridMap[start_morton].getPhase());
                    }
                }
            }
            // 调用相位优化函数
            bool success = phaseOptimize(layer_id, paths, start_phases);
            if (!success)
            {
                return false;
            }

            is_first_layer = false;
        }

        return true; // 所有层都成功分配相位
    }

    bool CircuitGraph::phaseOptimize(int current_layer, std::vector<fcngraph::Path> &paths, std::vector<int> &start_phases, int recursion_count)
    {
        // 递归次数限制
        const int max_recursion_count = 100;

        PhaseSolver solver(paths, start_phases);
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

        std::set<int> recordSetedPhaseMortonPos;

        for (size_t i = 0; i < paths.size(); ++i)
        {
            const Path &path = paths[i];
            const std::vector<int> &phases = optimized_phases[i];

            std::string message = " path " + std::to_string(i) + " start from (" + std::to_string(path.grids[0].first) + std::to_string(path.grids[0].second) + ") with morton" + std::to_string(MortonChessboard::calculateMortonCode(path.grids[0].first, path.grids[0].second));
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

            for (size_t j = 1; j < path.grids.size(); ++j)
            {
                const std::pair<int, int> &grid = path.grids[j];
                auto pos = MortonChessboard::calculateMortonCode(grid.first, grid.second);
                int phase = phases[j];

                if (chessboard.gridMap.find(pos) != chessboard.gridMap.end())
                {
                    auto tempPhase = chessboard.gridMap[pos].getPhase();
                    if (tempPhase == -1)
                    {
                        chessboard.gridMap[pos].setPhase(phase);
                        recordSetedPhaseMortonPos.insert(pos);
                    }
                    else if (tempPhase == phase)
                    {
                        continue;
                    }
                    else
                    {
                        std::string message = "Conflict in phase assignment for path " + std::to_string(i) + " at position " + std::to_string(pos) + " at position " + std::to_string(MortonChessboard::decodeMortonCode(pos).first) + ", " + std::to_string(MortonChessboard::decodeMortonCode(pos).second);
                        if (fitnessCallback)
                        {
                            fitnessCallback(message);
                        }
                        if (recursion_count >= max_recursion_count)
                        {
                            std::string message = " reached maximum recursion limit of " + std::to_string(max_recursion_count) + ". Exiting with failure.";
                            if (fitnessCallback)
                            {
                                fitnessCallback(message);
                            }
                            continue;
                        }
                        else
                        {
                            for (auto &pos : recordSetedPhaseMortonPos)
                            {
                                chessboard.gridMap[pos].setPhase(-1);
                            }

                            // 递归调用，但递归次数递增
                            return phaseOptimize(current_layer, paths, start_phases, recursion_count + 1);
                        }
                    }
                }
                else
                {
                    std::string message = "BIG ERROR!!! Position " + std::to_string(pos) + " not found in gridMap for path " + std::to_string(i);
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
            auto x_y = MortonChessboard::decodeMortonCode(pos);
            auto x = x_y.first;
            auto y = x_y.second;
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
        for (auto &index_morton : nodeIndex_morton)
        {
            auto node = index_morton.first;
            auto x_y = MortonChessboard::decodeMortonCode(index_morton.second);
            auto x = x_y.first;
            auto y = x_y.second;
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
                auto x_y = MortonChessboard::decodeMortonCode(pos);
                auto x = x_y.first;
                auto y = x_y.second;
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
