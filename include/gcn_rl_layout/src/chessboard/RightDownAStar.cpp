#include "RightDownAStar.h"

namespace iFCN_Lab {

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
        if (!(allowOccupiedTarget && isTarget) && !board.canPlaceWire(current)) {
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

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(static_cast<size_t>(width) * static_cast<size_t>(height), inf);
    std::vector<uint8_t> parent(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    auto index_of = [width](int x, int y) -> size_t {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::pair<int,int> coord = {firstStep.first + x, firstStep.second + y};
            const bool isFirst = (coord == firstStep);
            const bool isPreGoal = (coord == preGoal);
            if (!isFirst && !isPreGoal && !board.canPlaceWire(coord)) {
                continue;
            }

            const size_t idx = index_of(x, y);
            if (isFirst) {
                dist[idx] = 0.0;
                continue;
            }

            if (y > 0) {
                const size_t up_idx = index_of(x, y - 1);
                const double candidate = dist[up_idx] + 1.0;
                if (candidate < dist[idx]) {
                    dist[idx] = candidate;
                    parent[idx] = 1;  // from up: move down
                }
            }

            if (x > 0) {
                const size_t left_idx = index_of(x - 1, y);
                const double candidate = dist[left_idx] + 1.1;
                if (candidate < dist[idx]) {
                    dist[idx] = candidate;
                    parent[idx] = 2;  // from left: move right
                }
            }
        }
    }

    const int goal_x = preGoal.first - firstStep.first;
    const int goal_y = preGoal.second - firstStep.second;
    const size_t goal_idx = index_of(goal_x, goal_y);
    if (!std::isfinite(dist[goal_idx])) {
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

    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(static_cast<size_t>(width) * static_cast<size_t>(height), inf);
    std::vector<uint8_t> parent(static_cast<size_t>(width) * static_cast<size_t>(height), 0);

    auto index_of = [width](int x, int y) -> size_t {
        return static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::pair<int,int> coord = {anchor.first + x, anchor.second + y};
            const bool isAnchor = (coord == anchor);
            const bool isPreGoal = (coord == preGoal);
            if (!isAnchor && !isPreGoal && !board.canPlaceWire(coord)) {
                continue;
            }

            const size_t idx = index_of(x, y);
            if (isAnchor) {
                dist[idx] = 0.0;
                continue;
            }

            if (y > 0) {
                const size_t up_idx = index_of(x, y - 1);
                const double candidate = dist[up_idx] + 1.0;
                if (candidate < dist[idx]) {
                    dist[idx] = candidate;
                    parent[idx] = 1;
                }
            }

            if (x > 0) {
                const size_t left_idx = index_of(x - 1, y);
                const double candidate = dist[left_idx] + 1.1;
                if (candidate < dist[idx]) {
                    dist[idx] = candidate;
                    parent[idx] = 2;
                }
            }
        }
    }

    const int goal_x = preGoal.first - anchor.first;
    const int goal_y = preGoal.second - anchor.second;
    const size_t goal_idx = index_of(goal_x, goal_y);
    if (!std::isfinite(dist[goal_idx])) {
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
            if (!tryDirectMonotoneRouteFromAnchor(branch, goal, preGoal, branchPath) &&
                !tryDynamicProgrammingRouteFromAnchor(branch, goal, preGoal, branchPath)) {
                continue;
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

    if (fanout_dir != std::make_pair(1, 0) &&
        fanout_dir != std::make_pair(0, 1)) {
        return {};
    }
    if (fanin_dir != std::make_pair(-1, 0) &&
        fanin_dir != std::make_pair(0, -1)) {
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

    // --- 起点严格从指定的扇出端口离开 ---
    std::pair<int,int> firstStep = {
        start.first + fanout_dir.first,
        start.second + fanout_dir.second
    };
    const bool firstStepAvailable = (firstStep == goal || board.canPlaceWire(firstStep));

    // 相邻节点也必须同时满足两端指定的端口，不能绕过方向约束。
    if (firstStep == goal && preGoal != start) {
        return {};
    }

    if (preGoalAvailable && firstStepAvailable) {
        std::vector<std::pair<int,int>> directPath;
        if (tryDirectMonotoneRoute(start, goal, firstStep, preGoal, directPath)) {
            return commitPath(directPath);
        }

        std::vector<std::pair<int,int>> dpPath;
        if (tryDynamicProgrammingRoute(start, goal, firstStep, preGoal, dpPath)) {
            return commitPath(dpPath);
        }
    }

    std::vector<std::pair<int,int>> sharedPrefixPath;
    if (trySharedPrefixRoute(start, goal, preGoal, sharedPrefixPath)) {
        return commitPath(sharedPrefixPath);
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
