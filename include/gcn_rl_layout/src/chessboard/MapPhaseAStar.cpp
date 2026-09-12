#include "MapPhaseAStar.h"

#include <algorithm>
#include <cmath>

namespace iFCN_Lab {

namespace {
constexpr int kStepCost = 1;
constexpr int kHoldPhasePenalty = 1;
constexpr int kTurnPenalty = 3;
} // namespace

std::vector<std::pair<int,int>> MapPhaseAStar::route(int srcIndex, int dstIndex) {
    return routeInternal(srcIndex, dstIndex, false, 0, 0, false, 0, 0);
}

std::vector<std::pair<int,int>> MapPhaseAStar::route_with_dirs(
    int srcIndex,
    int dstIndex,
    int start_dx,
    int start_dy,
    int end_dx,
    int end_dy,
    bool use_start,
    bool use_end
) {
    return routeInternal(srcIndex, dstIndex, use_start, start_dx, start_dy, use_end, end_dx, end_dy);
}

std::vector<std::pair<int,int>> MapPhaseAStar::routeInternal(
    int srcIndex,
    int dstIndex,
    bool use_start,
    int start_dx,
    int start_dy,
    bool use_end,
    int end_dx,
    int end_dy
) {
    if (phaseCycle != 3 && phaseCycle != 4) {
        return {};
    }

    std::pair<int,int> start = board.getPlacedNodeCoord(srcIndex);
    std::pair<int,int> goal = board.getPlacedNodeCoord(dstIndex);
    if (start == goal) {
        return {start};
    }

    auto [minX, minY, maxX, maxY] = board.findLayoutBoard();
    if (maxX < minX || maxY < minY) {
        return {};
    }
    int boundMinX = minX - padding;
    int boundMinY = minY - padding;
    int boundMaxX = maxX + padding;
    int boundMaxY = maxY + padding;

    int startPhase = getPhase(start);
    std::vector<int> startPhases;
    if (startPhase < 0) {
        for (int p = 0; p < phaseCycle; ++p) {
            startPhases.push_back(p);
        }
    } else {
        startPhases.push_back(startPhase);
    }

    std::unordered_map<State, State, StateHash> cameFrom;
    std::unordered_map<State, int, StateHash> gScore;
    std::unordered_map<State, int, StateHash> fScore;

    auto cmp = [&](const State& a, const State& b) {
        return fScore[a] > fScore[b];
    };
    std::priority_queue<State, std::vector<State>, decltype(cmp)> openSet(cmp);

    for (int p : startPhases) {
        State s{start.first, start.second, p, 1, 0, 0};
        gScore[s] = 0;
        fScore[s] = heuristic(start, goal);
        openSet.push(s);
    }

    std::unordered_set<State, StateHash> closed;
    int expandedStates = 0;
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};

    while (!openSet.empty()) {
        State current = openSet.top();
        openSet.pop();
        if (closed.count(current)) {
            continue;
        }
        if (!phaseCompatible({current.x, current.y}, current.phase)) {
            continue;
        }
        closed.insert(current);
        ++expandedStates;
        if (expansionLimit > 0 && expandedStates > expansionLimit) {
            return {};
        }

        std::pair<int,int> curCoord{current.x, current.y};
        if (curCoord == goal) {
            std::vector<State> statePath;
            State walker = current;
            statePath.push_back(walker);
            while (cameFrom.count(walker)) {
                walker = cameFrom[walker];
                statePath.push_back(walker);
            }
            std::reverse(statePath.begin(), statePath.end());

            std::unordered_set<std::pair<int,int>, pair_hash> seenCoords;
            bool uniquePath = true;
            for (const auto& st : statePath) {
                std::pair<int,int> coord{st.x, st.y};
                if (seenCoords.count(coord)) {
                    uniquePath = false;
                    break;
                }
                seenCoords.insert(coord);
            }
            if (!uniquePath) {
                continue;
            }

            std::vector<std::pair<int,int>> path;
            path.reserve(statePath.size());
            for (const auto& st : statePath) {
                std::pair<int,int> coord{st.x, st.y};
                if (getPhase(coord) < 0) {
                    board.setPhase(coord, st.phase);
                }
                path.push_back(coord);
            }
            for (size_t i = 1; i + 1 < path.size(); ++i) {
                board.placeWire(path[i]);
            }
            board.savePath({srcIndex, dstIndex}, path);
            return path;
        }

        std::pair<int,int> prevCoord{0, 0};
        bool hasPrev = false;
        auto prevIt = cameFrom.find(current);
        if (prevIt != cameFrom.end()) {
            prevCoord = {prevIt->second.x, prevIt->second.y};
            hasPrev = true;
        }

        for (int dir = 0; dir < 4; ++dir) {
            int nx = current.x + dx[dir];
            int ny = current.y + dy[dir];
            if (nx < boundMinX || nx > boundMaxX || ny < boundMinY || ny > boundMaxY) {
                continue;
            }
            std::pair<int,int> nextCoord{nx, ny};
            if (hasPrev && nextCoord == prevCoord) {
                continue;
            }
            if (use_start && current.x == start.first && current.y == start.second) {
                if (nx - current.x != start_dx || ny - current.y != start_dy) {
                    continue;
                }
            }
            bool canSame = (maxSamePhase <= 0) || (current.runLen < maxSamePhase);
            int advPhase = (current.phase + 1) % phaseCycle;
            bool sameOk = canSame && phaseCompatible(nextCoord, current.phase);
            bool advOk = phaseCompatible(nextCoord, advPhase);
            if (!sameOk && !advOk) {
                continue;
            }
            if (nextCoord == goal && use_end) {
                if (current.x - goal.first != end_dx || current.y - goal.second != end_dy) {
                    continue;
                }
            }
            if (nextCoord != goal && !board.canPlaceWire(nextCoord)) {
                continue;
            }

            auto tryPushState = [&](const State& next, int transitionCost) {
                const bool hasPrevDirection = current.prevDx != 0 || current.prevDy != 0;
                const int turnCost = (
                    hasPrevDirection &&
                    (current.prevDx != next.prevDx || current.prevDy != next.prevDy)
                ) ? kTurnPenalty : 0;
                int tentative = gScore[current] + transitionCost + turnCost;
                if (gScore.count(next) && tentative >= gScore[next]) {
                    return;
                }
                cameFrom[next] = current;
                gScore[next] = tentative;
                fScore[next] = tentative + heuristic(nextCoord, goal);
                openSet.push(next);
            };

            if (advOk) {
                State nextAdvance{nx, ny, advPhase, 1, dx[dir], dy[dir]};
                tryPushState(nextAdvance, kStepCost);
            }
            if (sameOk) {
                State nextSame{nx, ny, current.phase, current.runLen + 1, dx[dir], dy[dir]};
                int sameCost = kStepCost;
                if (advOk) {
                    // When both transitions are legal, prefer advancing the phase
                    // to avoid long same-phase runs.
                    sameCost += kHoldPhasePenalty;
                }
                tryPushState(nextSame, sameCost);
            }
        }
    }

    return {};
}

} // namespace iFCN_Lab
