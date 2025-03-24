#pragma once
#include"mortonGrid.h"
// #include"parse.h"

namespace fcngraph{

class Astar{

public:
    Astar(MortonChessboard &_chessboard, bool isRegularClockScheme = true): chessboard(_chessboard), isRegularClockScheme(isRegularClockScheme), is_pathReused(false){}
    std::vector<unsigned int> findPath(unsigned int startMorton, unsigned int goalMorton, bool isOneFanout = false);

    inline void reset(){
        inDirections.clear();
        outDirections.clear();
        finishRoutes.clear();
        is_pathReused = false;
    }

    // void findMultiPaths(unsigned int startMorton, unsigned int goalMorton_1, unsigned int goalMorton_2);
    // std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> routes;    //存储每个节点的路径，start_node_pos_morton---路径morton容器
private:
     //A* 曼哈顿距离，启发式g计算
    double heuristic(const unsigned int a, const unsigned int b);
    //获取当前节点的可通行区域
    std::vector<unsigned int> getNeighbors(unsigned int mortonCode);

    //回溯获取路径
    std::vector<unsigned int> reconstructPath(const std::unordered_map<unsigned int, unsigned int>& cameFrom, unsigned int current);
    //检查node的入度和出度
    bool drcInDegreeCheck(unsigned int current_neighbor);

private:
    MortonChessboard &chessboard;
    bool isRegularClockScheme;
    unsigned int startMortonPos;
    unsigned int goalMortonPos;
    std::multimap<unsigned int, unsigned int> inDirections;           //存储每个节点的入度方向, nodemorton-mortonpos
    std::map<unsigned int, unsigned int> outDirections;               //存储每个节点的出度方向, nodemorton-mortonpos

    bool is_pathReused;
    std::vector<unsigned int> reusedPath;
    std::map<unsigned int, std::vector<unsigned int>> finishRoutes;  //起点唯一的路径
};


};