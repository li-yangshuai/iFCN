#pragma once
#include <utility>
#include <functional>
#include "patterns.h"
#include "gridCell.h"
// #include "circuitGraph.h"
#include <unordered_map>
#include <random>
#include <unordered_set>
#include <map>
#include <queue>
#include <cmath>
#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <stack>

namespace fcngraph{

using position = std::pair<unsigned int, unsigned int>;

struct PositionHash {
    size_t operator()(const position& pos) const noexcept {
        return std::hash<unsigned int>()(pos.first) ^ (std::hash<unsigned int>()(pos.second) << 1);
    }
};

class Astar;
class Individual;
// class CircuitGraph;

class GridChessboard{

public:
    friend class Astar; 
    friend class Individual;
    // friend class CircuitGraph;

    GridChessboard(CLOCK_SCHEME  _clockType, position _northWest = {0,0}, position _southEast = {1024,1024} ): 
        chessboard_nw(_northWest),
        chessboard_se(_southEast),
        patternData(nullptr), 
        patternWidth(0), 
        patternHeight(0),
        clockScheme(nullptr)
    {
        set_clockType(_clockType);
    }

    GridChessboard(){}

    
    // 选择时钟方案
    void set_clockType(CLOCK_SCHEME  _clockType);

    inline position randomPosition(){
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> disX(chessboard_nw.first, chessboard_se.first);
        std::uniform_int_distribution<> disY(chessboard_nw.second, chessboard_se.second);
        return {static_cast<unsigned int>(disX(gen)), static_cast<unsigned int>(disY(gen))};
    }

    std::unordered_map<position, GridCell, PositionHash>& getGridMap() {
        return gridMap;
    }

    uint8_t getCoorPos_Phase(unsigned int x, unsigned int y){
       return getPatternAt(x,y).value;
    }
    std::unordered_map<position, GridCell, PositionHash> gridMap; 

public:
    /***************************规律时钟方案*************************/
    // 放置节点的方法,(x,y)表示的是要放置的位置
    inline bool is_placeNode(const position& pos) {
        auto& cell = gridMap[pos];
        return cell.can_put_node();
    }

    inline void placeNode(const position& pos){
        auto& cell = gridMap[pos];
        cell.put_node();
    }

    // 删除节点的方法
    inline void removeNode(const position& pos) {
        auto& cell = gridMap[pos];
        cell.remove_node();
    }

    inline bool is_addWire(const position& pos){
        auto& cell = gridMap[pos];
        return cell.can_put_wire();
    }

    inline void addWire(const position& pos) {
        auto& cell = gridMap[pos];
        cell.put_wire();
    }

    // 删除线的方法
    inline void removeWire(const position& pos){
        auto& cell = gridMap[pos];
        return cell.remove_wire();
    }   

    // 重置
    inline void reset(){
        for(auto &v: gridMap){
            v.second.reset();
        }
    }
    /***************************规律时钟方案*************************/

public:
    /***************************不规则时钟方案**********************/
    inline void addNodeCell(const position& pos){
        GridCell cell;
        cell.put_node();
        if(gridMap.find(pos) == gridMap.end()){
            gridMap[pos] = cell;
        }
    }

    inline void addWireCell(const position& pos){
        GridCell cell;
        cell.put_wire();
        if(gridMap.find(pos) == gridMap.end()){
            gridMap[pos] = cell;
        }
    }

    inline void removeNodeCell(const position& pos){
        if(gridMap.find(pos) != gridMap.end()){
            gridMap.erase(pos);
        }
    }

    //因为可以放置两根线，所以需要判断是否还有线
    inline void removeWireCell(const position& pos){
        if(gridMap.find(pos) != gridMap.end()){
            GridCell& cell = gridMap[pos];
            cell.remove_wire();
            if(cell.get_current_weight() == 0){
                gridMap.erase(pos);
            }
        }
    }

    inline bool is_placeWire(const position& pos){
        if(gridMap.find(pos) == gridMap.end()){
            return true;
        }
        return gridMap[pos].can_put_wire();
    }

    /***************************不规则时钟方案***********************/

    //根据坐标获取可通行区域的坐标,用于初始化容器directionMap
    std::vector<position> getPosssibleDirection(const position& pos, bool regularClockScheme = true);
    
private:
    //获取时钟方案可以布线的方向
    inline const Pattern& getPatternAt(unsigned int x, unsigned int y) const {
        unsigned int patternX = x % patternWidth;
        unsigned int patternY = y % patternHeight;
        return patternData[patternY * patternWidth + patternX];
    }



    //根据时钟方案，从私有成员gridMap中获取当前坐标的所有可布线区域
    inline std::vector<position> getDirectionsForPosition(const position& pos) const {
        auto result = directionMap.find(pos);
        return (result != directionMap.end()) ? result->second : std::vector<position>{};
    }

private:

    //chessborad 定位坐标：左上角和右下角
    position chessboard_nw, chessboard_se;
    std::unique_ptr<ClockScheme> clockScheme; //时钟设定
    int patternWidth, patternHeight;     //该时钟类型的宽、高
    const Pattern* patternData;          //获取pattern时钟方案的的数据
    //当前位置坐标和下一步可以布线的坐标集合
    std::unordered_map<position, std::vector<position>, PositionHash> directionMap;





};


};
