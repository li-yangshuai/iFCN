#pragma once
#include <utility>
#include "morton2D.h"
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

class Astar;
class Individual;
// class CircuitGraph;

class MortonChessboard{

public:
    friend class Astar; 
    friend class Individual;
    // friend class CircuitGraph;

    MortonChessboard(CLOCK_SCHEME  _clockType, position _northWest = {0,0}, position _southEast = {1024,1024} ): 
        chessboard_nw(_northWest),
        chessboard_se(_southEast),
        patternData(nullptr), 
        patternWidth(0), 
        patternHeight(0),
        clockScheme(nullptr)
    {
        set_clockType(_clockType);
    }

    MortonChessboard(){}

    
    // 选择时钟方案
    void set_clockType(CLOCK_SCHEME  _clockType);

    //输入二维坐标，返回morton码
    inline static unsigned int calculateMortonCode(unsigned int x, unsigned int y) {
        return libmorton::m2D_e_LUT_ET<unsigned int, uint_fast16_t>(static_cast<uint_fast16_t>(x), static_cast<uint_fast16_t>(y));
    }

    //morton码解码
    inline static std::pair<unsigned int, unsigned int> decodeMortonCode(const unsigned int morton) {
        uint_fast32_t x, y;
        libmorton::m2D_d_sLUT<uint_fast64_t, uint_fast32_t>(morton, x, y);
        return std::pair<unsigned int, unsigned int>(static_cast<unsigned int>(x), static_cast<unsigned int>(y));
    }

    inline unsigned int randomMortonPos(){
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> disX(chessboard_nw.first, chessboard_se.first);
        std::uniform_int_distribution<> disY(chessboard_nw.second, chessboard_se.second);
        unsigned int x = disX(gen);
        unsigned int y = disY(gen);
        return calculateMortonCode(x, y);
    }

    std::unordered_map<unsigned int, GridCell>& getGridMap() {
        return gridMap;
    }

    uint8_t getCoorPos_Phase(unsigned int x, unsigned int y){
       return getPatternAt(x,y).value;
    }
    std::unordered_map<unsigned int, GridCell> gridMap; 

public:
    /***************************规律时钟方案*************************/
    // 放置节点的方法,(x,y)表示的是要放置的位置
    inline bool is_placeNode(unsigned int mortonCode) {
        auto& cell = gridMap[mortonCode];
        return cell.can_put_node();
    }

    inline void placeNode(unsigned int mortonCode){
        auto& cell = gridMap[mortonCode];
        cell.put_node();
    }

    // 删除节点的方法
    inline void removeNode(unsigned int mortonCode) {
        auto& cell = gridMap[mortonCode];
        cell.remove_node();
    }

    inline bool is_addWire(unsigned mortonCode){
        auto& cell = gridMap[mortonCode];
        return cell.can_put_wire();
    }

    inline void addWire(unsigned mortonCode) {
        auto& cell = gridMap[mortonCode];
        cell.put_wire();
    }

    // 删除线的方法
    inline void removeWire(unsigned mortonCode){
        auto& cell = gridMap[mortonCode];
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
    inline void addNodeCell(unsigned int mortonCode){
        GridCell cell;
        cell.put_node();
        if(gridMap.find(mortonCode) == gridMap.end()){
            gridMap[mortonCode] = cell;
        }
    }

    inline void addWireCell(unsigned int mortonCode){
        GridCell cell;
        cell.put_wire();
        if(gridMap.find(mortonCode) == gridMap.end()){
            gridMap[mortonCode] = cell;
        }
    }

    inline void removeNodeCell(unsigned int mortonCode){
        if(gridMap.find(mortonCode) != gridMap.end()){
            gridMap.erase(mortonCode);
        }
    }

    //因为可以放置两根线，所以需要判断是否还有线
    inline void removeWireCell(unsigned int mortonCode){
        if(gridMap.find(mortonCode) != gridMap.end()){
            GridCell& cell = gridMap[mortonCode];
            cell.remove_wire();
            if(cell.get_current_weight() == 0){
                gridMap.erase(mortonCode);
            }
        }
    }

    inline bool is_placeWire(unsigned int mortonCode){
        if(gridMap.find(mortonCode) == gridMap.end()){
            return true;
        }
        return gridMap[mortonCode].can_put_wire();
    }

    /***************************不规则时钟方案***********************/

    //根据morton码获取可通行区域的morton码,用于初始化容器directionMap
    std::vector<unsigned int> getPosssibleDirection(unsigned int morton, bool regularClockScheme = true);
    
private:
    //获取时钟方案可以布线的方向
    inline const Pattern& getPatternAt(unsigned int x, unsigned int y) const {
        unsigned int patternX = x % patternWidth;
        unsigned int patternY = y % patternHeight;
        return patternData[patternY * patternWidth + patternX];
    }



    //根据时钟方案，从私有成员gridMap中获取当前morton码的所有可布线区域的morton
    inline std::vector<unsigned int> getDirectionsForMortonCode(unsigned int mortonCode) const {
        auto result = directionMap.find(mortonCode);
        return (result != directionMap.end()) ? result->second : std::vector<unsigned int>{};
    }

private:

    //chessborad 定位坐标：左上角和右下角
    position chessboard_nw, chessboard_se;
    std::unique_ptr<ClockScheme> clockScheme; //时钟设定
    int patternWidth, patternHeight;     //该时钟类型的宽、高
    const Pattern* patternData;          //获取pattern时钟方案的的数据
    //当前位置的morton码和下一步可以布线的morton集合
    std::unordered_map <unsigned int, std::vector<unsigned int>> directionMap;

    //使用morton码作为key的GridCell映射





};


};