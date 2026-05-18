#pragma once

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "MapChessboard.hpp"

namespace iFCN_Lab {

class MapPhaseAStar {
public:
    MapPhaseAStar(MapChessboard& chessboard, int phaseCycle = 4, int padding = 2, int maxSamePhase = 0)
        : board(chessboard), phaseCycle(phaseCycle), padding(padding), maxSamePhase(maxSamePhase) {}

    std::vector<std::pair<int,int>> route(int srcIndex, int dstIndex);
    std::vector<std::pair<int,int>> route_with_dirs(
        int srcIndex,
        int dstIndex,
        int start_dx,
        int start_dy,
        int end_dx,
        int end_dy,
        bool use_start,
        bool use_end
    );

    void reset() {
        finishedRoutes.clear();
    }

private:
    struct State {
        int x;
        int y;
        int phase;
        int runLen;
        int prevDx;
        int prevDy;

        bool operator==(const State& other) const {
            return x == other.x && y == other.y && phase == other.phase && runLen == other.runLen &&
                   prevDx == other.prevDx && prevDy == other.prevDy;
        }
    };

    struct StateHash {
        std::size_t operator()(const State& s) const {
            std::size_t h1 = std::hash<int>{}(s.x);
            std::size_t h2 = std::hash<int>{}(s.y);
            std::size_t h3 = std::hash<int>{}(s.phase);
            std::size_t h4 = std::hash<int>{}(s.runLen);
            std::size_t h5 = std::hash<int>{}(s.prevDx);
            std::size_t h6 = std::hash<int>{}(s.prevDy);
            return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5);
        }
    };

    struct CoordHash {
        std::size_t operator()(const std::pair<int,int>& p) const {
            std::size_t h1 = std::hash<int>{}(p.first);
            std::size_t h2 = std::hash<int>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    MapChessboard& board;
    int phaseCycle;
    int padding;
    int maxSamePhase;

    std::unordered_map<std::pair<int,int>, std::vector<std::pair<int,int>>, CoordHash> finishedRoutes;

    int heuristic(const std::pair<int,int>& a, const std::pair<int,int>& b) const {
        return std::abs(a.first - b.first) + std::abs(a.second - b.second);
    }

    int getPhase(const std::pair<int,int>& coord) const {
        return board.getPhase(coord);
    }

    bool phaseCompatible(const std::pair<int,int>& coord, int expected) const {
        int p = getPhase(coord);
        return p < 0 || p == expected;
    }

    std::vector<std::pair<int,int>> routeInternal(
        int srcIndex,
        int dstIndex,
        bool use_start,
        int start_dx,
        int start_dy,
        bool use_end,
        int end_dx,
        int end_dy
    );
};

} // namespace iFCN_Lab
