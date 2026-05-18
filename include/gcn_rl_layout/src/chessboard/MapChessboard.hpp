#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#include <boost/functional/hash.hpp>
#include "AbstractNode.hpp"
#include "GridCell.hpp"
#include "CZMFunction.hpp"
#include "Parse.h"


// std::vector<int> 
namespace iFCN_Lab
{

struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

class MapChessboard
{
public:
    MapChessboard(){}
    MapChessboard(std::shared_ptr<ClockStrategy> clockScheme) : strategy(clockScheme) {}

private:
    static constexpr int kPhaseBlockSize = 4;

    static inline int floorDiv4(int v) {
        if (v >= 0) return v / kPhaseBlockSize;
        return -(((-v) + (kPhaseBlockSize - 1)) / kPhaseBlockSize);
    }

    static inline std::pair<int, int> blockOrigin4x4(const std::pair<int, int>& coord) {
        return {floorDiv4(coord.first) * kPhaseBlockSize, floorDiv4(coord.second) * kPhaseBlockSize};
    }

    static inline void requireAlignedBlockOrigin4x4(const std::pair<int, int>& origin) {
        if (origin.first % kPhaseBlockSize != 0 || origin.second % kPhaseBlockSize != 0) {
            throw std::invalid_argument("4x4 block origin must align to multiples of 4.");
        }
    }

    // Packed 4x4 code: 32-bit = 4 bytes (row0..row3), each byte is 4 phases in 2-bit fields (col0 in LSB).
    static inline int unpackPackedPhaseAt(uint32_t packed, int x_in_block, int y_in_block) {
        const uint8_t row = static_cast<uint8_t>((packed >> (8 * (3 - y_in_block))) & 0xFFu);
        return static_cast<int>((row >> (2 * x_in_block)) & 0x3u);
    }

    GridCell& ensureCell(const std::pair<int, int>& coord){
        auto [it, _inserted] = gridMap.try_emplace(coord, GridCell());
        return it->second;
    }

    void clearStoredPhases() {
        for (auto& [coord, cell] : gridMap) {
            (void)coord;
            cell.setPhase(-1);
        }
    }

public:
    bool isPhaseEnabled() const {
        return phaseEnabled;
    }

    void setPhaseEnabled(bool enabled) {
        phaseEnabled = enabled;
        if (!phaseEnabled) {
            clearStoredPhases();
            clearPackedPhaseBlocks4x4();
        }
    }

    // ----------------- 4x4 Packed Phase Template -----------------
    // Packed 4x4 code: 32-bit = 4 bytes (row0..row3), each byte is 4 phases in 2-bit fields (col0 in LSB).
    // The board will treat this as a template: if a cell has no explicit phase stored, getPhase() falls back
    // to the packed template of its containing 4x4 block.
    void setPackedPhaseBlock4x4(const std::pair<int, int>& blockOrigin, uint32_t packedCode) {
        requireAlignedBlockOrigin4x4(blockOrigin);
        packedPhaseBlocks4x4[blockOrigin] = packedCode;
    }

    std::optional<uint32_t> getPackedPhaseBlock4x4(const std::pair<int, int>& blockOrigin) const {
        requireAlignedBlockOrigin4x4(blockOrigin);
        auto it = packedPhaseBlocks4x4.find(blockOrigin);
        if (it == packedPhaseBlocks4x4.end()) return std::nullopt;
        return it->second;
    }

    bool erasePackedPhaseBlock4x4(const std::pair<int, int>& blockOrigin) {
        requireAlignedBlockOrigin4x4(blockOrigin);
        return packedPhaseBlocks4x4.erase(blockOrigin) > 0;
    }

    void clearPackedPhaseBlocks4x4() {
        packedPhaseBlocks4x4.clear();
    }

    // Row-major block codes: (origin.x + bx*4, origin.y + by*4), bx in [0,blocksX), by in [0,blocksY)
    void setPackedPhaseBlocksGrid4x4(
        const std::pair<int, int>& origin,
        int blocksX,
        int blocksY,
        const std::vector<uint32_t>& packedCodes
    ) {
        requireAlignedBlockOrigin4x4(origin);
        if (blocksX <= 0 || blocksY <= 0) {
            throw std::invalid_argument("blocksX/blocksY must be positive.");
        }
        const size_t expected = static_cast<size_t>(blocksX) * static_cast<size_t>(blocksY);
        if (packedCodes.size() != expected) {
            throw std::invalid_argument("packedCodes size mismatch for setPackedPhaseBlocksGrid4x4.");
        }
        size_t idx = 0;
        for (int by = 0; by < blocksY; ++by) {
            for (int bx = 0; bx < blocksX; ++bx) {
                const std::pair<int, int> block = {origin.first + bx * kPhaseBlockSize, origin.second + by * kPhaseBlockSize};
                packedPhaseBlocks4x4[block] = packedCodes[idx++];
            }
        }
    }

    // Encode the current phase map into a packed 4x4 code.
    // If a cell phase is unassigned (<0), fill it with the TDD phase if useTDDForUnassigned is true.
    uint32_t encodePackedPhaseBlock4x4(
        const std::pair<int, int>& blockOrigin,
        bool useTDDForUnassigned = true
    ) const {
        requireAlignedBlockOrigin4x4(blockOrigin);
        uint32_t packed = 0;
        for (int y = 0; y < kPhaseBlockSize; ++y) {
            uint8_t row = 0;
            for (int x = 0; x < kPhaseBlockSize; ++x) {
                const std::pair<int, int> coord = {blockOrigin.first + x, blockOrigin.second + y};
                int phase = getPhase(coord);
                if (phase < 0) {
                    if (!useTDDForUnassigned) {
                        throw std::runtime_error("Cannot encode packed 4x4 block: phase is unassigned.");
                    }
                    phase = getTDDPhaseAtCoord(coord);
                }
                if (phase < 0 || phase > 3) {
                    throw std::runtime_error("Phase out of range (0..3) while encoding packed 4x4 block.");
                }
                row |= static_cast<uint8_t>((phase & 0x3) << (2 * x));
            }
            packed |= (static_cast<uint32_t>(row) << (8 * (3 - y)));
        }
        return packed;
    }

    std::vector<uint32_t> encodePackedPhaseBlocksGrid4x4(
        const std::pair<int, int>& origin,
        int blocksX,
        int blocksY,
        bool useTDDForUnassigned = true
    ) const {
        requireAlignedBlockOrigin4x4(origin);
        if (blocksX <= 0 || blocksY <= 0) {
            throw std::invalid_argument("blocksX/blocksY must be positive.");
        }
        std::vector<uint32_t> packed;
        packed.reserve(static_cast<size_t>(blocksX) * static_cast<size_t>(blocksY));
        for (int by = 0; by < blocksY; ++by) {
            for (int bx = 0; bx < blocksX; ++bx) {
                const std::pair<int, int> block = {origin.first + bx * kPhaseBlockSize, origin.second + by * kPhaseBlockSize};
                packed.push_back(encodePackedPhaseBlock4x4(block, useTDDForUnassigned));
            }
        }
        return packed;
    }

    // 放置节点/线路
    bool canPlaceNode(const std::pair<int, int>& coord, NodeType nodeType){
        return ensureCell(coord).canPlaceNode(nodeType);
    }

    void placeNode(int nodeIndex, const std::pair<int, int>& coord, NodeType nodeType){
        ensureCell(coord).placeNode(nodeIndex, nodeType);
        //注意这里会刷新NodeIndex的坐标，注意是刷新，如果之前这个index放过位置，以最新的位置来记录
        nodeIndexToCoordMap[nodeIndex] = coord;
    }

    bool canPlaceWire(const std::pair<int, int>& coord){
        return ensureCell(coord).canPlaceWire();
    }

    void placeWire(const std::pair<int, int>& coord){
        ensureCell(coord).placeWire();
    }

    void removeNode(int nodeIndex){
        auto it = nodeIndexToCoordMap.find(nodeIndex);
        if (it != nodeIndexToCoordMap.end()) {
            auto coord = it->second;
            if(gridMap.find(coord) != gridMap.end()){
                gridMap[coord].removeNode();
            }
            nodeIndexToCoordMap.erase(it);
        }
    }

    void removeWire(const std::pair<int, int>& coord){
        if(gridMap.find(coord) != gridMap.end()){
            gridMap[coord].removeWire();
        }
    }

    void savePath(const std::pair<int,int>& nodePair, const std::vector<std::pair<int,int>>& path){
        nodePairRoutes[nodePair] = path;
    }


    void reset(){
        reset_with_phase(true);
    }

    void reset_with_phase(bool resetPhase){
        nodeIndexToCoordMap.clear();
        nodePairRoutes.clear();

        if (resetPhase || !phaseEnabled) {
            gridMap.clear();
            clearPackedPhaseBlocks4x4();
            return;
        }

        // 保留已赋值相位，清空布线/节点占用信息
        std::unordered_map<std::pair<int, int>, GridCell, pair_hash> phaseOnly;
        phaseOnly.reserve(gridMap.size());
        for (const auto& [coord, cell] : gridMap) {
            int phase = cell.getPhase();
            if (phase >= 0) {
                phaseOnly.emplace(coord, GridCell(phase));
            }
        }
        gridMap.swap(phaseOnly);
    }

    //(0,0)是0,(1,0)是1,(2,0)是2,(3,0)是3
    int getTDDPhaseAtCoord(const std::pair<int, int>& coord) const {
        int x = coord.first;
        int y = coord.second;
        int v = (x + y) % 4;
        if (v < 0) v += 4;
        return v;
    }

    int getPhase(const std::pair<int, int>& coord) const {
        if (!phaseEnabled) {
            return -1;
        }
        auto it = gridMap.find(coord);
        if (it != gridMap.end()) {
            int stored = it->second.getPhase();
            if (stored >= 0) {
                return stored;
            }
        }
        if (!packedPhaseBlocks4x4.empty()) {
            const std::pair<int, int> origin = blockOrigin4x4(coord);
            auto bit = packedPhaseBlocks4x4.find(origin);
            if (bit != packedPhaseBlocks4x4.end()) {
                const int lx = coord.first - origin.first;
                const int ly = coord.second - origin.second;
                if (lx >= 0 && lx < kPhaseBlockSize && ly >= 0 && ly < kPhaseBlockSize) {
                    return unpackPackedPhaseAt(bit->second, lx, ly);
                }
            }
        }
        return -1;
    }

    bool hasPhaseConflict(const std::pair<int, int>& coord, int phase) const {
        if (!phaseEnabled) {
            (void)coord;
            (void)phase;
            return false;
        }
        int existing = getPhase(coord);
        return existing >= 0 && existing != phase;
    }

    bool trySetPhase(const std::pair<int, int>& coord, int phase) {
        if (!phaseEnabled) {
            (void)coord;
            (void)phase;
            return true;
        }
        if (hasPhaseConflict(coord, phase)) {
            return false;
        }
        ensureCell(coord).setPhase(phase);
        return true;
    }

    void setPhase(const std::pair<int, int>& coord, int phase) {
        if (!phaseEnabled) {
            (void)coord;
            (void)phase;
            return;
        }
        if (!trySetPhase(coord, phase)) {
            throw std::runtime_error(
                "Phase conflict at coord (" + std::to_string(coord.first) + "," + std::to_string(coord.second) +
                "): existing=" + std::to_string(getPhase(coord)) + ", new=" + std::to_string(phase)
            );
        }
    }

    int getGridCellCapacityAtCoord(const std::pair<int, int>& coord) const {
        auto it = gridMap.find(coord);
        if (it != gridMap.end()) {
            return it->second.getCapacity();
        } else {
            return MAX_CELL_CAPACITY; // 默认容量
        }
    }

    //输入nodeIndex，返回坐标
    std::pair<int, int> getPlacedNodeCoord(int nodeIndex) const {
        auto it = nodeIndexToCoordMap.find(nodeIndex);
        if (it != nodeIndexToCoordMap.end()) {
            return it->second;
        } else {
            throw std::runtime_error("Node index not found.");
        }
    }

    //输入nodeIndex，返回节点类型
    NodeType getPlaceNodeType(int nodeIndex) const {
        auto it = nodeIndexToCoordMap.find(nodeIndex);
        if (it != nodeIndexToCoordMap.end()) {
            auto coord = it->second;
            if(gridMap.find(coord) != gridMap.end()){
                auto nt = gridMap.at(coord).getNodeType();
                if(nt.has_value()){
                    return nt.value();
                }else{
                    throw std::runtime_error("No node placed at the given index.");
                }
            }else{
                throw std::runtime_error("No node placed at the given index.");
            }
        } else {
            throw std::runtime_error("Node index not found.");
        }
    }   


    std::tuple<int, int, int, int> findLayoutBoard() const{
        int minX = std::numeric_limits<int>::max();
        int minY = std::numeric_limits<int>::max();
        int maxX = std::numeric_limits<int>::min();
        int maxY = std::numeric_limits<int>::min();
        bool found = false;
        //这里不能计算放置node的，还有放置wire的，应该获取使用的gridCell
        for(const auto& [coord, cell] : gridMap) {
            if (!cell.isEmpty()) {
                found = true;
                minX = std::min(minX, coord.first);
                minY = std::min(minY, coord.second);
                maxX = std::max(maxX, coord.first);
                maxY = std::max(maxY, coord.second);
            }
        }
        if (!found) {
            return std::make_tuple(0, 0, -1, -1);
        }
        return std::make_tuple(minX, minY, maxX, maxY);
    }

    std::pair<int,int> computeLayoutArea() const{
        auto [minX, minY, maxX, maxY] = findLayoutBoard();
        if (maxX < minX || maxY < minY) return {-1,-1};
        return {maxX - minX + 1,  maxY - minY + 1};
    }

    static void outputTexFile(const MapChessboard& mapChessboard, const std::string fileName, const std::string& dir) {
        std::string filename = dir + "/"+fileName+ ".tex";

        std::ofstream texFile(filename);
        if (!texFile.is_open())
            throw std::runtime_error("Unable to open file: " + filename);

        texFile << "\\documentclass[tikz]{standalone}\n"
                << "\\usetikzlibrary{calc,arrows.meta}\n"
                << "\\newcommand{\\phasecell}[1]{\\vbox to 1cm{\\vfil\\hbox{\\hspace{1pt}\\scriptsize #1}\\vspace{1pt}}}\n"
                << "\\begin{document}\n"
                << "\\begin{tikzpicture}[\n"
                << "scale=0.5,transform shape,\n"
                << "cell/.style={rectangle, minimum size=1cm, inner sep=0pt, text width=1cm, align=left},\n"
                << "c-1/.style={cell, fill=white, text=black},\n"
                << "c0/.style={cell, fill=lightgray!50, text=black},\n"
                << "c1/.style={cell, fill=lightgray, text=black},\n"
                << "c2/.style={cell, fill=gray, text=black},\n"
                << "c3/.style={cell, fill=darkgray!90, text=white},\n"
                << "v/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.7cm},\n"
                << "vi/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.5cm,text=red},\n"
                << "vo/.style={circle, draw, fill=white, line width = 0.8pt, minimum size=0.5cm,text=blue},\n"
                << "route/.style={->, >={Stealth[]},line width=0.8pt, blue!50}\n"
                << "]\n";


        //只获取放置node的版图，对版图的坐标进行平移
        auto [minX, minY, maxX, maxY] = mapChessboard.findLayoutBoard();

        if(maxX < minX || maxY < minY) {
            texFile << "\\end{tikzpicture}\n\\end{document}\n";
            texFile.close();
            std::cout << "No nodes placed, skipping TikZ output." << std::endl;
            return;
        }

        int gridHeight = maxY - minY + 1;

        //下面遍历的时候，只遍历[minX, minY]到[maxX, maxY]的区域，且所有的x和y坐标都要减去 minX和minY
        int offsetX = minX, offsetY = minY;

        // 绘制网格
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                int val = mapChessboard.getPhase({x, y});
                if (val < 0) {
                    val = mapChessboard.getTDDPhaseAtCoord({x, y});
                }
                const int drawX = x - offsetX;
                const int drawY = gridHeight - (y - offsetY) - 1;
                texFile << "\\node[c" << val << "] at (" << drawX << "," << drawY << "){\\phasecell{" << val << "}};\n";
            }
        }

        // 绘制node
        for (const auto& [nodeIndex, coord] : mapChessboard.nodeIndexToCoordMap) {
            int x = coord.first - offsetX, y = coord.second - offsetY;
            int nodeId = nodeIndex;
            // if(mapChessboard.getPlaceNodeType(nodeIndex) == NodeType::Input) {
            //     texFile << "\\node[vi] (" << nodeId << ") at (" << x << "," << gridHeight- y - 1 << "){" << nodeIndex << "};\n";
            // } else if(mapChessboard.getPlaceNodeType(nodeIndex) == NodeType::Output) {
            //     texFile << "\\node[vo] (" << nodeId << ") at (" << x << "," << gridHeight - y - 1 << "){" << nodeIndex << "};\n";
            // } else{
                texFile << "\\node[v] ("  << nodeId << ") at (" << x << "," << gridHeight - y - 1 << "){" << nodeIndex << "};\n";
            // }
        }

            //绘制布线 \draw[route](a)--(1,2)--(1,3)--(b)，不带头和尾
        for (const auto& [nodePair, path] : mapChessboard.nodePairRoutes) {
            const auto& [startNode, endNode] = nodePair;
            if(path.empty()) continue;
            texFile << "\\draw[route](" << startNode << ")--";
            for (size_t i = 1; i < path.size() - 1; ++i) {
                auto [x, y] = path[i];
                x -= offsetX; // 平移坐标
                y -= offsetY; // 平移坐标
                texFile << "(" << x << "," << gridHeight - y - 1 << ")--";
            }
            texFile << "(" << endNode << ");\n";
        }

        texFile << "\\end{tikzpicture}\n\\end{document}\n";
        texFile.close();
        // std::cout << "Circuit: " + fileName + " LaTeX file generated: " << filename << std::endl;
    }



public:

    std::unordered_map<std::pair<int, int>, GridCell, pair_hash> gridMap;  //存储棋盘格上的小格子
    std::map<int, std::pair<int, int>> nodeIndexToCoordMap; //存储节点索引到坐标的映射
    std::unordered_map<std::pair<int, int>, std::vector<std::pair<int, int>>, pair_hash> nodePairRoutes;
    std::shared_ptr<ClockStrategy> strategy;
    bool phaseEnabled = true;

private:
    // Key: 4x4 block origin coordinate (x,y), both aligned to multiples of 4.
    // Value: packed 32-bit phase code (row0..row3 bytes).
    std::unordered_map<std::pair<int, int>, uint32_t, pair_hash> packedPhaseBlocks4x4;

};

} // namespace iFCN_Lab
