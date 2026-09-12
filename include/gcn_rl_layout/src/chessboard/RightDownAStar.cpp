#include "RightDownAStar.h"

namespace iFCN_Lab {

void RightDownAStar::prepareActivePortReservations(
    const std::pair<int,int>& start,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& firstStep,
    const std::pair<int,int>& preGoal)
{
    activeReservedPorts.clear();
    activeReservedPorts.reserve(board.nodeIndexToCoordMap.size() * 3);
    for (const auto& [nodeIndex, coord] : board.nodeIndexToCoordMap) {
        (void)nodeIndex;
        activeReservedPorts.insert({coord.first, coord.second + 1});
        activeReservedPorts.insert({coord.first - 1, coord.second});
        activeReservedPorts.insert({coord.first, coord.second - 1});
    }
    activeReservedPorts.erase(start);
    activeReservedPorts.erase(goal);
    activeReservedPorts.erase(firstStep);
    activeReservedPorts.erase(preGoal);
}

bool RightDownAStar::canUseActiveRouteCell(
    const std::pair<int,int>& coord) const
{
    return activeReservedPorts.count(coord) == 0 && board.canPlaceWire(coord);
}

bool RightDownAStar::appendStraightSegment(
    std::vector<std::pair<int,int>>& path,
    const std::pair<int,int>& target,
    bool allowOccupiedTarget) const
{
    if (path.empty()) return false;

    auto current = path.back();
    if (target.first < current.first || target.second < current.second) {
        return false;
    }
    if (target.first != current.first && target.second != current.second) {
        return false;
    }

    int dx = (target.first > current.first) ? 1 : 0;
    int dy = (target.second > current.second) ? 1 : 0;

    while (current != target) {
        current = {current.first + dx, current.second + dy};
        bool isTarget = (current == target);
        if (!(allowOccupiedTarget && isTarget) && !canUseActiveRouteCell(current)) {
            return false;
        }
        path.push_back(current);
    }
    return true;
}

bool RightDownAStar::tryDirectMonotoneRoute(
    const std::pair<int,int>& start,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& firstStep,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    path.clear();
    path.push_back(start);

    if (firstStep == goal) {
        path.push_back(goal);
        return true;
    }

    path.push_back(firstStep);

    if (preGoal == firstStep) {
        path.push_back(goal);
        return true;
    }

    const std::pair<int,int> bends[] = {
        {firstStep.first, preGoal.second},
        {preGoal.first, firstStep.second},
    };

    for (const auto& bend : bends) {
        std::vector<std::pair<int,int>> candidate = path;
        if (bend != candidate.back()) {
            if (!appendStraightSegment(candidate, bend, false)) {
                continue;
            }
        }
        if (!appendStraightSegment(candidate, preGoal, true)) {
            continue;
        }
        candidate.push_back(goal);
        path.swap(candidate);
        return true;
    }

    return false;
}

bool RightDownAStar::trySampledMonotoneRoute(
    const std::pair<int,int>& start,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& firstStep,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    path.clear();
    if (firstStep == goal) {
        path = {start, goal};
        return true;
    }
    if (preGoal == firstStep) {
        path = {start, firstStep, goal};
        return true;
    }
    if (preGoal.first < firstStep.first || preGoal.second < firstStep.second) {
        return false;
    }

    std::vector<int> sampledX;
    std::vector<int> sampledY;
    constexpr int sampleSegments = 8;
    for (int sample = 0; sample <= sampleSegments; ++sample) {
        sampledX.push_back(
            firstStep.first +
            (preGoal.first - firstStep.first) * sample / sampleSegments
        );
        sampledY.push_back(
            firstStep.second +
            (preGoal.second - firstStep.second) * sample / sampleSegments
        );
    }
    std::sort(sampledX.begin(), sampledX.end());
    sampledX.erase(std::unique(sampledX.begin(), sampledX.end()), sampledX.end());
    std::sort(sampledY.begin(), sampledY.end());
    sampledY.erase(std::unique(sampledY.begin(), sampledY.end()), sampledY.end());

    uint64_t bestCongestion = std::numeric_limits<uint64_t>::max();
    std::vector<std::pair<int,int>> bestPath;
    auto consider = [&](std::vector<std::pair<int,int>> candidate) {
        if (candidate.empty() || candidate.back() != preGoal) {
            return;
        }
        candidate.push_back(goal);
        uint64_t congestion = 0;
        for (size_t index = 1; index + 1 < candidate.size(); ++index) {
            congestion += static_cast<uint64_t>(std::max(
                0,
                MAX_CELL_CAPACITY -
                    board.getGridCellCapacityAtCoord(candidate[index])
            ));
        }
        if (congestion < bestCongestion) {
            bestCongestion = congestion;
            bestPath.swap(candidate);
        }
    };

    // right-down-right candidates
    for (int pivotX : sampledX) {
        std::vector<std::pair<int,int>> candidate = {start, firstStep};
        const std::pair<int,int> firstBend = {pivotX, firstStep.second};
        const std::pair<int,int> secondBend = {pivotX, preGoal.second};
        if ((firstBend == candidate.back() ||
             appendStraightSegment(candidate, firstBend, false)) &&
            (secondBend == candidate.back() ||
             appendStraightSegment(candidate, secondBend, false)) &&
            (preGoal == candidate.back() ||
             appendStraightSegment(candidate, preGoal, true))) {
            consider(std::move(candidate));
        }
    }

    // down-right-down candidates
    for (int pivotY : sampledY) {
        std::vector<std::pair<int,int>> candidate = {start, firstStep};
        const std::pair<int,int> firstBend = {firstStep.first, pivotY};
        const std::pair<int,int> secondBend = {preGoal.first, pivotY};
        if ((firstBend == candidate.back() ||
             appendStraightSegment(candidate, firstBend, false)) &&
            (secondBend == candidate.back() ||
             appendStraightSegment(candidate, secondBend, false)) &&
            (preGoal == candidate.back() ||
             appendStraightSegment(candidate, preGoal, true))) {
            consider(std::move(candidate));
        }
    }

    if (bestPath.empty()) {
        return false;
    }
    path.swap(bestPath);
    return true;
}

bool RightDownAStar::tryDynamicProgrammingRoute(
    const std::pair<int,int>& start,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& firstStep,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    path.clear();

    if (goal.first < start.first || goal.second < start.second) {
        return false;
    }

    if (preGoal == start) {
        path = {start, goal};
        return true;
    }

    if (preGoal.first < firstStep.first || preGoal.second < firstStep.second) {
        return false;
    }

    const int width = preGoal.first - firstStep.first + 1;
    const int height = preGoal.second - firstStep.second + 1;
    if (width <= 0 || height <= 0) {
        return false;
    }

    // Every monotone path has the same geometric length, but not the same
    // congestion.  Retain a one-byte parent grid and only two rolling cost
    // rows: this avoids the old full distance matrix while preferring empty
    // tracks over cells that already carry a wire.  Greedily accepting the
    // first L-shaped path used to stack two wires into full-capacity walls.
    std::vector<uint8_t> parent(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    const uint32_t infinity = std::numeric_limits<uint32_t>::max() / 4;
    std::vector<uint32_t> previousCost(static_cast<size_t>(width), infinity);
    std::vector<uint32_t> currentCost(static_cast<size_t>(width), infinity);

    auto index_of = [width](int x, int y) -> size_t {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    };

    for (int y = 0; y < height; ++y) {
        std::fill(currentCost.begin(), currentCost.end(), infinity);
        for (int x = 0; x < width; ++x) {
            const std::pair<int,int> coord = {firstStep.first + x, firstStep.second + y};
            const bool isFirst = (coord == firstStep);
            const bool isPreGoal = (coord == preGoal);
            if (!isFirst && !isPreGoal && !canUseActiveRouteCell(coord)) {
                continue;
            }

            const size_t idx = index_of(x, y);
            if (isFirst) {
                parent[idx] = 3;  // search root
                currentCost[static_cast<size_t>(x)] = 0;
                continue;
            }

            const uint32_t fromUp = (
                y > 0 ? previousCost[static_cast<size_t>(x)] : infinity
            );
            const uint32_t fromLeft = (
                x > 0 ? currentCost[static_cast<size_t>(x - 1)] : infinity
            );
            if (fromUp == infinity && fromLeft == infinity) {
                continue;
            }

            const int remainingCapacity = board.getGridCellCapacityAtCoord(coord);
            const uint32_t congestionPenalty = static_cast<uint32_t>(
                std::max(0, MAX_CELL_CAPACITY - remainingCapacity)
            );
            const bool preferUpOnTie = (
                (coord.first * 31 + coord.second * 17 +
                 firstStep.first * 13 + preGoal.first) & 1
            ) == 0;
            const bool chooseUp = (
                fromUp < fromLeft ||
                (fromUp == fromLeft && preferUpOnTie)
            );
            const uint32_t predecessorCost = chooseUp ? fromUp : fromLeft;
            parent[idx] = chooseUp ? 1 : 2;
            currentCost[static_cast<size_t>(x)] = predecessorCost + congestionPenalty;
        }
        previousCost.swap(currentCost);
    }

    const int goal_x = preGoal.first - firstStep.first;
    const int goal_y = preGoal.second - firstStep.second;
    const size_t goal_idx = index_of(goal_x, goal_y);
    if (parent[goal_idx] == 0) {
        return false;
    }

    std::vector<std::pair<int,int>> middle_path;
    int x = goal_x;
    int y = goal_y;
    while (true) {
        middle_path.emplace_back(firstStep.first + x, firstStep.second + y);
        if (x == 0 && y == 0) {
            break;
        }

        const uint8_t p = parent[index_of(x, y)];
        if (p == 1) {
            --y;
        } else if (p == 2) {
            --x;
        } else {
            return false;
        }
    }
    std::reverse(middle_path.begin(), middle_path.end());

    path.reserve(middle_path.size() + 2);
    path.push_back(start);
    for (const auto& coord : middle_path) {
        path.push_back(coord);
    }
    if (path.back() != goal) {
        path.push_back(goal);
    }
    return true;
}

bool RightDownAStar::tryDirectMonotoneRouteFromAnchor(
    const std::pair<int,int>& anchor,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    path.clear();

    if (goal.first < anchor.first || goal.second < anchor.second) {
        return false;
    }

    if (preGoal == anchor) {
        path = {anchor, goal};
        return true;
    }

    if (preGoal.first < anchor.first || preGoal.second < anchor.second) {
        return false;
    }

    path.push_back(anchor);
    const std::pair<int,int> bends[] = {
        {anchor.first, preGoal.second},
        {preGoal.first, anchor.second},
    };

    for (const auto& bend : bends) {
        std::vector<std::pair<int,int>> candidate = path;
        if (bend != candidate.back()) {
            if (!appendStraightSegment(candidate, bend, false)) {
                continue;
            }
        }
        if (!appendStraightSegment(candidate, preGoal, true)) {
            continue;
        }
        candidate.push_back(goal);
        path.swap(candidate);
        return true;
    }

    return false;
}

bool RightDownAStar::tryDynamicProgrammingRouteFromAnchor(
    const std::pair<int,int>& anchor,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    path.clear();

    if (goal.first < anchor.first || goal.second < anchor.second) {
        return false;
    }

    if (preGoal == anchor) {
        path = {anchor, goal};
        return true;
    }

    if (preGoal.first < anchor.first || preGoal.second < anchor.second) {
        return false;
    }

    const int width = preGoal.first - anchor.first + 1;
    const int height = preGoal.second - anchor.second + 1;
    if (width <= 0 || height <= 0) {
        return false;
    }

    std::vector<uint8_t> parent(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    const uint32_t infinity = std::numeric_limits<uint32_t>::max() / 4;
    std::vector<uint32_t> previousCost(static_cast<size_t>(width), infinity);
    std::vector<uint32_t> currentCost(static_cast<size_t>(width), infinity);

    auto index_of = [width](int x, int y) -> size_t {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    };

    for (int y = 0; y < height; ++y) {
        std::fill(currentCost.begin(), currentCost.end(), infinity);
        for (int x = 0; x < width; ++x) {
            const std::pair<int,int> coord = {anchor.first + x, anchor.second + y};
            const bool isAnchor = (coord == anchor);
            const bool isPreGoal = (coord == preGoal);
            if (!isAnchor && !isPreGoal && !canUseActiveRouteCell(coord)) {
                continue;
            }

            const size_t idx = index_of(x, y);
            if (isAnchor) {
                parent[idx] = 3;
                currentCost[static_cast<size_t>(x)] = 0;
                continue;
            }

            const uint32_t fromUp = (
                y > 0 ? previousCost[static_cast<size_t>(x)] : infinity
            );
            const uint32_t fromLeft = (
                x > 0 ? currentCost[static_cast<size_t>(x - 1)] : infinity
            );
            if (fromUp == infinity && fromLeft == infinity) {
                continue;
            }

            const int remainingCapacity = board.getGridCellCapacityAtCoord(coord);
            const uint32_t congestionPenalty = static_cast<uint32_t>(
                std::max(0, MAX_CELL_CAPACITY - remainingCapacity)
            );
            const bool preferUpOnTie = (
                (coord.first * 31 + coord.second * 17 +
                 anchor.first * 13 + preGoal.first) & 1
            ) == 0;
            const bool chooseUp = (
                fromUp < fromLeft ||
                (fromUp == fromLeft && preferUpOnTie)
            );
            const uint32_t predecessorCost = chooseUp ? fromUp : fromLeft;
            parent[idx] = chooseUp ? 1 : 2;
            currentCost[static_cast<size_t>(x)] = predecessorCost + congestionPenalty;
        }
        previousCost.swap(currentCost);
    }

    const int goal_x = preGoal.first - anchor.first;
    const int goal_y = preGoal.second - anchor.second;
    const size_t goal_idx = index_of(goal_x, goal_y);
    if (parent[goal_idx] == 0) {
        return false;
    }

    std::vector<std::pair<int,int>> middle_path;
    int x = goal_x;
    int y = goal_y;
    while (true) {
        middle_path.emplace_back(anchor.first + x, anchor.second + y);
        if (x == 0 && y == 0) {
            break;
        }

        const uint8_t p = parent[index_of(x, y)];
        if (p == 1) {
            --y;
        } else if (p == 2) {
            --x;
        } else {
            return false;
        }
    }
    std::reverse(middle_path.begin(), middle_path.end());

    path.reserve(middle_path.size() + 1);
    for (const auto& coord : middle_path) {
        path.push_back(coord);
    }
    if (path.back() != goal) {
        path.push_back(goal);
    }
    return true;
}

bool RightDownAStar::trySharedPrefixRoute(
    const std::pair<int,int>& start,
    const std::pair<int,int>& goal,
    const std::pair<int,int>& preGoal,
    std::vector<std::pair<int,int>>& path) const
{
    auto it = finishedRoutes.find(start);
    if (it == finishedRoutes.end()) {
        return false;
    }

    const auto& existingRoutes = it->second;
    for (const auto& existing : existingRoutes) {
        if (existing.size() < 2) {
            continue;
        }

        for (size_t branchIdx = existing.size() - 1; branchIdx > 0; --branchIdx) {
            if (branchIdx + 1 == existing.size()) {
                continue;  // do not branch from the old sink node.
            }

            const auto& branch = existing[branchIdx];
            if (branch.first > preGoal.first || branch.second > preGoal.second) {
                continue;
            }

            std::vector<std::pair<int,int>> branchPath;
            std::vector<std::pair<int,int>> directBranchPath;
            const bool hasDirectBranch = tryDirectMonotoneRouteFromAnchor(
                branch, goal, preGoal, directBranchPath
            );
            bool directBranchIsEmpty = hasDirectBranch;
            if (hasDirectBranch) {
                for (size_t pathIndex = 1; pathIndex + 1 < directBranchPath.size(); ++pathIndex) {
                    if (board.getGridCellCapacityAtCoord(directBranchPath[pathIndex]) <
                        MAX_CELL_CAPACITY) {
                        directBranchIsEmpty = false;
                        break;
                    }
                }
            }
            if (directBranchIsEmpty) {
                branchPath.swap(directBranchPath);
            } else if (!tryDynamicProgrammingRouteFromAnchor(
                           branch, goal, preGoal, branchPath)) {
                if (!hasDirectBranch) {
                    continue;
                }
                branchPath.swap(directBranchPath);
            }

            path.assign(existing.begin(), existing.begin() + static_cast<long>(branchIdx) + 1);
            path.insert(path.end(), branchPath.begin() + 1, branchPath.end());
            return true;
        }
    }

    return false;
}

std::vector<std::pair<int,int>> RightDownAStar::commitPath(
    const std::vector<std::pair<int,int>>& path)
{
    if (path.empty()) return {};

    std::pair<int,int> start = path.front();

    size_t sharedPrefixEnd = 0;
    auto it = finishedRoutes.find(start);
    if (it != finishedRoutes.end()) {
        for (const auto& oldPath : it->second) {
            size_t localSharedPrefixEnd = 0;
            size_t minLen = std::min(oldPath.size(), path.size());
            for (size_t i = 1; i < minLen; ++i) {
                if (oldPath[i] == path[i]) {
                    localSharedPrefixEnd = i;
                } else {
                    break;
                }
            }
            sharedPrefixEnd = std::max(sharedPrefixEnd, localSharedPrefixEnd);
        }
    }

    for (size_t i = sharedPrefixEnd + 1; i + 1 < path.size(); ++i) {
        board.placeWire(path[i]);
    }

    finishedRoutes[start].push_back(path);
    return path;
}

std::vector<std::pair<int,int>> RightDownAStar::route(
    int srcIndex, int dstIndex, const std::pair<int,int>& fanin_dir)
{
    return routeWithDirs(srcIndex, dstIndex, {0, 1}, fanin_dir);
}

std::vector<std::pair<int,int>> RightDownAStar::routeWithDirs(
    int srcIndex,
    int dstIndex,
    const std::pair<int,int>& fanout_dir,
    const std::pair<int,int>& fanin_dir)
{
    std::pair<int,int> start = board.getPlacedNodeCoord(srcIndex);
    std::pair<int,int> goal  = board.getPlacedNodeCoord(dstIndex);

    // --- 起点即终点 ---
    if (start == goal) return {start};

    if (goal.first < start.first || goal.second < start.second) {
        return {};
    }

    if (fanout_dir != std::pair<int,int>{1, 0} &&
        fanout_dir != std::pair<int,int>{0, 1}) {
        return {};
    }
    if (fanin_dir != std::pair<int,int>{-1, 0} &&
        fanin_dir != std::pair<int,int>{0, -1}) {
        return {};
    }

    // --- 只能右/下，若目标在左/上，直接失败 ---
    // if (goal.first < start.first || goal.second < start.second) return {};

    // --- 搜索边界矩形（右下象限） ---
    int minX = start.first, maxX = goal.first;
    int minY = start.second, maxY = goal.second;

    //检查终点的扇入方向是否被堵住
    std::pair<int,int> preGoal = { goal.first + fanin_dir.first, goal.second + fanin_dir.second };
    const bool preGoalAvailable = (preGoal == start || board.canPlaceWire(preGoal));

    // The selected sink port is part of the routed net.  In particular, a
    // shared-prefix fanout must not reuse a different gate node as the final
    // wire tile before the real sink.  Doing so silently connects the sink to
    // that gate's cell template instead of to the intended source net.
    if (!preGoalAvailable) {
        return {};
    }

    // Select the source port explicitly.  Horizontal same-row propagation is
    // a legal right/down route when the sink is strictly to the right.
    std::pair<int,int> firstStep = {
        start.first + fanout_dir.first,
        start.second + fanout_dir.second,
    };
    activeReservedPorts.clear();
    const bool firstStepAvailable = (
        firstStep == goal || board.canPlaceWire(firstStep)
    );

    // Adjacent nodes must satisfy both selected endpoint ports.
    if (firstStep == goal && preGoal != start) {
        return {};
    }

    if (firstStepAvailable) {
        std::vector<std::pair<int,int>> directPath;
        if (tryDirectMonotoneRoute(start, goal, firstStep, preGoal, directPath)) {
            return commitPath(directPath);
        }

        std::vector<std::pair<int,int>> dpPath;
        if (tryDynamicProgrammingRoute(start, goal, firstStep, preGoal, dpPath)) {
            return commitPath(dpPath);
        }
    }

    if (firstStepAvailable) {
        std::vector<std::pair<int,int>> sharedPrefixPath;
        if (trySharedPrefixRoute(start, goal, preGoal, sharedPrefixPath)) {
            return commitPath(sharedPrefixPath);
        }
    }

    return {};
}


// ------------------------ 右/下邻居 + 边界限制 ------------------------
std::vector<std::pair<int,int>> RightDownAStar::getNeighbors(
    const std::pair<int,int>& pos,
    int /*minX*/, int maxX, int /*minY*/, int maxY)
{
    std::vector<std::pair<int,int>> nbs;
    nbs.reserve(2);

    if (pos.second + 1 <= maxY) // 下
        nbs.emplace_back(pos.first, pos.second + 1);

    if (pos.first + 1 <= maxX) // 右
        nbs.emplace_back(pos.first + 1, pos.second);

    return nbs;
}

} // namespace iFCN_Lab
