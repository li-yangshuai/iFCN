#pragma once
#include "autopr/grid/grid.h"
// #include"parse.h"

namespace fcngraph{

class Astar{

public:
    Astar(GridChessboard &_chessboard, bool isRegularClockScheme = true, double maxSearchCost = 80.0):
        chessboard(_chessboard),
        isRegularClockScheme(isRegularClockScheme),
        maxSearchCost(maxSearchCost),
        is_pathReused(false){}
    std::vector<position> findPath(const position& startPos, const position& goalPos, bool isOneFanout = false);
    void setMaxSearchCost(double cost) { maxSearchCost = cost; }
    void setAllowInterSourceWireOverlap(bool allow) { allowInterSourceWireOverlap = allow; }
    void setOccupiedWirePenalty(double penalty) { occupiedWirePenalty = penalty; }
    void setSearchBounds(const position& minimum, const position& maximum) {
        searchMinimum = minimum;
        searchMaximum = maximum;
        hasSearchBounds = minimum.first <= maximum.first &&
                          minimum.second <= maximum.second;
    }
    void clearSearchBounds() { hasSearchBounds = false; }

    inline void reset(){
        inDirections.clear();
        outDirections.clear();
        finishRoutes.clear();
        wireOwnership.clear();
        reusedSuccessor.clear();
        is_pathReused = false;
    }

    // void findMultiPaths(unsigned int startMorton, unsigned int goalMorton_1, unsigned int goalMorton_2);
    // std::map<std::pair<unsigned int, unsigned int>, std::vector<position>> routes;    //存储每个节点的路径，start_node_pos---路径坐标容器
private:
     //A* 曼哈顿距离，启发式g计算
    double heuristic(const position& a, const position& b);
    //获取当前节点的可通行区域
    std::vector<position> getNeighbors(
        const position& pos,
        const std::unordered_map<position, position, PositionHash>& cameFrom);

    //回溯获取路径
    std::vector<position> reconstructPath(const std::unordered_map<position, position, PositionHash>& cameFrom, const position& current);
    //检查node的入度和出度
    bool drcInDegreeCheck(const position& current_neighbor);
    bool isNodeCell(const position& pos) const;
    bool isInsideSearchBounds(const position& pos) const {
        return !hasSearchBounds ||
               (pos.first >= searchMinimum.first &&
                pos.first <= searchMaximum.first &&
                pos.second >= searchMinimum.second &&
                pos.second <= searchMaximum.second);
    }
    void recordRouteOwnership(const std::vector<position>& path);

private:
    GridChessboard &chessboard;
    bool isRegularClockScheme;
    double maxSearchCost;
    bool allowInterSourceWireOverlap{true};
    double occupiedWirePenalty{-1.0};
    position startPos;
    position goalPos;
    std::multimap<position, position> inDirections;           //存储每个节点的入度方向, node-pos
    std::map<position, position> outDirections;               //存储每个节点的出度方向, node-pos

    bool is_pathReused;
    bool hasSearchBounds{false};
    position searchMinimum{0, 0};
    position searchMaximum{0, 0};
    std::vector<position> reusedPath;
    std::unordered_map<position, position, PositionHash> reusedSuccessor;
    std::map<position, std::vector<position>> finishRoutes;  //起点唯一的路径

    enum class WireOrientation
    {
        Horizontal,
        Vertical,
        Bend
    };
    std::map<position, std::map<position, std::set<WireOrientation>>> wireOwnership;
};


};
