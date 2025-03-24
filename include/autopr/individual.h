#pragma once
#include "mortonGrid.h"
#include "parse.h"
#include "astar.h"

namespace fcngraph {

template<typename T>
bool is_unique(std::vector<T> const& vec) {
    auto tmp = vec;
    std::sort(tmp.begin(), tmp.end());
    auto it = std::unique(tmp.begin(), tmp.end());
    return it == tmp.end();
}

class Individual {
public:

    Individual(Parse &_parse, MortonChessboard &_chessboard, Astar &_astar) : 
        parse(_parse), 
        chessboard(_chessboard), 
        astar(_astar),
        fitness(.0), 
        is_placed(false),
        is_routed(false), 
        is_synced(false)
    {
        randomlyPlaceNodes();
    }

    Individual(Parse &_parse, MortonChessboard &_chessboard, Astar &_astar, std::map<unsigned int, unsigned int> _nodeindex_morton) : 
        parse(_parse), 
        chessboard(_chessboard), 
        astar(_astar),
        fitness(.0), 
        is_placed(false),
        is_routed(false), 
        is_synced(false)
    {
        nodeindex_morton = _nodeindex_morton;
    }

    Individual(const Individual& other) : 
        parse(other.parse),          // 引用原对象的parse
        chessboard(other.chessboard), // 引用原对象的chessboard
        astar(other.astar),           // 引用原对象的astar
        fitness(other.fitness),       // 拷贝fitness
        is_placed(other.is_placed),   // 拷贝is_placed状态
        is_routed(other.is_routed),   // 拷贝is_routed状态
        is_synced(other.is_synced),   // 拷贝is_synced状态
        nodeindex_morton(other.nodeindex_morton), // 仅拷贝nodeindex_morton数据
        routes(other.routes),         // 拷贝routes
        layerRoutesLength(other.layerRoutesLength)
    {

    }

    Individual& operator=(const Individual& other) {
        if (this != &other) {
            fitness = other.fitness;
            is_placed = other.is_placed;
            is_routed = other.is_routed;
            is_synced = other.is_synced;
            nodeindex_morton = other.nodeindex_morton;
            layerRoutesLength = other.layerRoutesLength;
            routes = other.routes;
        }
        return *this;
    }

    bool operator<(const Individual &rhs) const noexcept {
        return this->fitness < rhs.fitness; 
    }


    inline long double getfitness() const noexcept {
        return fitness;
    }

    // 重新放置节点，刷新容器
    inline void newPlacement(std::map<unsigned int, unsigned int> _nodeindex_morton) {
        reset();
        assert(!_nodeindex_morton.empty());
        nodeindex_morton = _nodeindex_morton;
        for(auto&v : nodeindex_morton){
            chessboard.placeNode(v.second);
        }
    }

    inline auto& getNodeindex_morton() {
        return nodeindex_morton;
    }

    inline auto& getRoutes() {
        return routes;
    }

    inline bool isPr_synced(){
        return is_synced;
    }

   

public: 

    void randomlyPlaceNodes();
    void sameLevelRouting(); 
    void diffLevelRouting();
    
    void reset() {
        fitness = .0;
        is_placed = false;
        is_routed = false;
        is_synced = false;
        chessboard.reset();
        astar.reset();
        routes.clear();
        layer_length.clear();
        nodeindex_morton.clear();
        layerRoutesLength.clear();
    }

    void infoReset(){
        fitness = .0;
        is_placed = false;
        is_routed = false;
        is_synced = false;
        chessboard.reset();
        astar.reset();
        routes.clear();
        layer_length.clear();
        layerRoutesLength.clear();
    }
    
    void add_placement_value_to();
    void add_routing_value_to();
    void add_synchronization_value_to();
    void add_area_value_to();
    void computeFitness();

    bool is_placed;
    bool is_routed;
    bool is_synced;
    long double fitness;
    std::map<unsigned int, unsigned int> nodeindex_morton; 
    std::vector<unsigned int> layer_length;
    std::map<std::pair<unsigned int, unsigned int>, std::vector<unsigned int>> routes;
private:
    Parse &parse;
    MortonChessboard &chessboard;
    Astar &astar;

    std::multimap<uint8_t, unsigned int> layerRoutesLength;

};

}
