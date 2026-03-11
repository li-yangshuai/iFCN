#pragma once
#include "autopr/grid/grid.h"
// #include"parse.h"

namespace fcngraph{

class Astar{

public:
    Astar(GridChessboard &_chessboard, bool isRegularClockScheme = true): chessboard(_chessboard), isRegularClockScheme(isRegularClockScheme), is_pathReused(false){}
    std::vector<position> findPath(const position& startPos, const position& goalPos, bool isOneFanout = false);

    inline void reset(){
        inDirections.clear();
        outDirections.clear();
        finishRoutes.clear();
        is_pathReused = false;
    }

    // void findMultiPaths(unsigned int startMorton, unsigned int goalMorton_1, unsigned int goalMorton_2);
    // std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;    //存储每个节点的路径，start_node_pos---路径坐标容器
private:
     //A* 曼哈顿距离，启发式g计算
    double heuristic(const position& a, const position& b);
    //获取当前节点的可通行区域
    std::vector<position> getNeighbors(const position& pos);

    //回溯获取路径
    std::vector<position> reconstructPath(const std::unordered_map<position, position, PositionHash>& cameFrom, const position& current);
    //检查node的入度和出度
    bool drcInDegreeCheck(const position& current_neighbor);

private:
    GridChessboard &chessboard;
    bool isRegularClockScheme;
    position startPos;
    position goalPos;
    std::multimap<position, position> inDirections;           //存储每个节点的入度方向, node-pos
    std::map<position, position> outDirections;               //存储每个节点的出度方向, node-pos

    bool is_pathReused;
    std::vector<position> reusedPath;
    std::map<position, std::vector<position>> finishRoutes;  //起点唯一的路径
};


};
