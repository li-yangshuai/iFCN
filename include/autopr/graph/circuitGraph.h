#pragma once
#include <graphviz/gvc.h>
#include "parse.h"
#include "autopr/grid/grid.h"
#include "autopr/algorithms/astar.h"
#include "autopr/algorithms/phaseSolver.h"
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include "autopr/grid/gridCell.h"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility> // For std::pair
#include <functional> // For std::hash

// // 定义一个自定义哈希函数
// namespace std {
//     template <>
//     struct hash<std::pair<int, int>> {
//         std::size_t operator()(const std::pair<int, int>& p) const noexcept {
//             return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
//         }
//     };
// }

namespace fcngraph {

struct Path;

class CircuitGraph {
public:
    CircuitGraph( Parse& parse,std::string fileName, GridChessboard& chessboard, Astar& astar)
        : parse(parse), fileName(fileName), chessboard(chessboard), astar(astar) {
        gvc = gvContext();
    }

    //回调函数
    void setFitnessCallback(const std::function<void(std::string)> &callback);
    
    //生成图
    void processAndGenerateGraph(bool printSVG = false, bool showCircuitLabel = false, bool isBox = false, bool isOGD = false);

    //图->网格坐标处理
    void sortNodesByYThenXCoordinate(double grid_size = 400); 
    void sortNodesByLayeredGrid(unsigned int xSpacing = 4,
                                unsigned int ySpacing = 4,
                                unsigned int xPadding = 4,
                                unsigned int yPadding = 4);

    //布局布线
    bool placeAndRoute();

    std::map<unsigned int, std::vector<std::vector<position>>> reclassifyLayers(const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>>& classifiedRoutes, std::map<unsigned int, std::vector<unsigned int>>& groupMapping);
    void printGroupMapping(const std::map<unsigned int, std::vector<unsigned int>>& groupMapping);
    void printClassifiedRoutes(
        const std::map<unsigned int, std::map<std::pair<unsigned int, unsigned int>, std::vector<position>>>& classifiedRoutes);
    // 调用SA分配相位
    bool assignPhases(int phaseCount = 4);

    bool phaseOptimize(int current_layer, std::vector<fcngraph::Path>& paths, std::vector<int>& start_phases, int phaseCount = 4, int recursion_count = 0); 
    bool assignPhasesFallback(int phaseCount = 4);

    // 打印latex结果
    void printLaTex();
    
    std::map<int, position> nodeIndex_pos;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;
private:
    Parse& parse;
    std::string fileName;
    GridChessboard& chessboard;
    Astar& astar;
    std::map<int, Agnode_t*> node_map;
    GVC_t* gvc;
    //由dot算法生成的node位置
    std::vector<std::pair<int, std::pair<double, double>>> node_positions;
    //由dot算法生成的node位置转换为网格坐标
    std::map<int, position> grid_positions;

    //按照y坐标从大到小，x坐标从小到大排序
    std::vector<std::pair<int, position>> sorted_grid_positions;
        //回调打印的信息用；
    std::function<void(std::string)> fitnessCallback;

};


/* SA 实现相位分配 */


}
 
