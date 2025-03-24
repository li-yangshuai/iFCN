#pragma once
#include "patterns.h"
#include <algorithm>
#include <optional>

namespace fcngraph{

using position = std::pair<unsigned int, unsigned int>;
using morton_node = unsigned int;

static const uint8_t MAX_CELL_WEIGHT{6};
static const uint8_t WIRE_WEIGHT{3};
static const uint8_t NODE_WEIGHT{5};


class GridCell {
public:
    GridCell(){}
    GridCell(int _phase): phase(_phase){}

    inline bool can_put_node() const {
        if(current_weight == 0)
            return true;
        return false;
    }

    inline void put_node(){
        current_weight += NODE_WEIGHT;
    }

    inline void remove_node(){
        current_weight -= NODE_WEIGHT;
    }

    inline bool can_put_wire() const {
        if((MAX_CELL_WEIGHT - current_weight) >= WIRE_WEIGHT)
            return true;
        return false;
    }

    inline void put_wire(){
        current_weight += WIRE_WEIGHT;
        wire_count++;
    }

    inline void remove_wire(){
        current_weight -= WIRE_WEIGHT;
        wire_count--;
    }

    inline void reset(){
        current_weight = 0;
        wire_count = 0;
        phase.reset();
    }

    inline uint8_t get_current_weight() const {
        return current_weight;
    } 

    inline void setPhase(int _phase){
        phase = _phase;
    }

    inline int getPhase() const {
        if (phase.has_value()) {
            return phase.value();
        } else {
            return -1;
        }
    }

private:
    uint8_t current_weight{0};
    uint8_t wire_count{0};
    std::optional<int> phase;          //0-3 四种相位

    
};



}; // namespace fcngraph
