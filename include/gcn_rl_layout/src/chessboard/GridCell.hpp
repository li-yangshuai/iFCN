// GridCell.hpp
#pragma once
#include <optional>
#include <cstdint>
#include "AbstractNode.hpp" // 或你的 AbstractNode.hpp，根据你文件结构

namespace iFCN_Lab {

constexpr int MAX_CELL_CAPACITY      = 5;
constexpr int WIRE_CELL_CAPACITY     = 2;
constexpr int INPUT_NODE_CAPACITY    = 5;
constexpr int LOGIC_NODE_CAPACITY    = 5;


class GridCell {
public:
    GridCell(int clockPhase = -1)
        : clockPhase(clockPhase), capacity(MAX_CELL_CAPACITY), nodeIndex(std::nullopt), nodeType(std::nullopt) {}

    // 判断指定 NodeType 能否放置
    bool canPlaceNode(NodeType ntype) const {
        int need_cap = requiredCapacity(ntype);
        return capacity >= need_cap;
    }

    // 放置节点（索引+类型），自动记录类型
    void placeNode(int idx, NodeType ntype) {
        nodeIndex = idx;
        nodeType = ntype;
        capacity -= requiredCapacity(ntype);
    }

    // 放置线路
    bool canPlaceWire() {
        if(capacity < WIRE_CELL_CAPACITY) 
            return false;
        return true;
    }

    // 放置线路
    void placeWire() {
        capacity -= WIRE_CELL_CAPACITY;
    }

    void removeNode() {
        if (nodeType.has_value()) {
            capacity += requiredCapacity(nodeType.value());
            nodeIndex.reset();
            nodeType.reset();
        }
    }

    void removeWire() {
        capacity += WIRE_CELL_CAPACITY;
    }


    int getPhase() const { 
        return clockPhase; 
    }

    void setPhase(int phase) { 
        clockPhase = phase; 
    }

    std::optional<int> getNodeIndex() const { 
        return nodeIndex; 
    }

    std::optional<NodeType> getNodeType() const { 
        return nodeType; 
    }

    auto getCapacity() const { 
        return capacity; 
    }


    bool isEmpty() const {
        return capacity == MAX_CELL_CAPACITY;
    }


    void reset() {
        capacity = MAX_CELL_CAPACITY;
        nodeIndex.reset();
        nodeType.reset();
    }

    void reset(bool resetPhase) {
        capacity = MAX_CELL_CAPACITY;
        nodeIndex.reset();
        nodeType.reset();
        if (resetPhase) {
            clockPhase = -1;
        }
    }

private:
    int clockPhase;
    int capacity;

    std::optional<int> nodeIndex;
    std::optional<NodeType> nodeType;  // 新增：记录该cell放置的节点类型

    static int requiredCapacity(NodeType ntype) {
        switch(ntype) {
            // case NodeType::Input:  return INPUT_NODE_CAPACITY;
            // case NodeType::Redundancy: return INPUT_NODE_CAPACITY;
            default:               return LOGIC_NODE_CAPACITY;
        }
    }
};

inline std::string NodeTypeToString(NodeType t) {
    switch (t) {
        case NodeType::Input: return "input";
        case NodeType::Output: return "output";
        case NodeType::And: return "and";
        case NodeType::Or: return "or";
        case NodeType::Maj: return "maj";
        case NodeType::Not: return "not";
        case NodeType::Redundancy: return "redundancy";
        case NodeType::Fanout: return "fanout";
        // ... 补全你的类型
        default: return "Unknown";
    }
}




} // namespace iFCN_Lab
