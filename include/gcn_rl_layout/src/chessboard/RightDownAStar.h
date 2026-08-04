#pragma once

#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include "MapChessboard.hpp"

namespace iFCN_Lab {

/**
 * @brief 右下限定 A* 布线器
 * 特性：
 *  - 起点按指定的扇出端口向右或向下布线；
 *  - 只能向右(1,0)或下(0,1)扩展；
 *  - 搜索范围为起点与终点构成的矩形；
 *  - 扇入方向由 fanin_dir 指定，如 (-1,0) 表示左侧入，(0,-1) 表示上方入。
 */
class RightDownAStar {
public:
    explicit RightDownAStar(MapChessboard& chessboard)
        : board(chessboard) {}

    /**
     * @param srcIndex 起点节点索引
     * @param dstIndex 终点节点索引
     * @param fanin_dir 终点扇入方向，例如 (-1,0) 左入，(0,-1) 上入
     * @return 路径点序列（包含起点和终点），若失败返回空
     */
    std::vector<std::pair<int,int>> route(int srcIndex,
                                          int dstIndex,
                                          const std::pair<int,int>& fanin_dir);

    /**
     * 同时约束源端扇出和宿端扇入端口。fanout_dir 只能为
     * (1,0)/ (0,1)，fanin_dir 只能为 (-1,0)/(0,-1)。
     */
    std::vector<std::pair<int,int>> routeWithDirs(
        int srcIndex,
        int dstIndex,
        const std::pair<int,int>& fanout_dir,
        const std::pair<int,int>& fanin_dir);
    
    void reset() {
        finishedRoutes.clear();
    }
private:
    MapChessboard& board;

    inline double heuristic(const std::pair<int,int>& a, const std::pair<int,int>& b) const {
        // 若只保持普通曼哈顿：
        return static_cast<double>(std::abs(a.first - b.first) + std::abs(a.second - b.second));
        // 若想稍微偏向“向下”，可用：
        // return 0.8 * std::abs(a.second - b.second) + 1.0 * std::abs(a.first - b.first);
    }


    // 右下方向邻居，带矩形边界约束
    std::vector<std::pair<int,int>> getNeighbors(
        const std::pair<int,int>& pos,
        int minX, int maxX, int minY, int maxY);

    bool appendStraightSegment(std::vector<std::pair<int,int>>& path,
                               const std::pair<int,int>& target,
                               bool allowOccupiedTarget) const;

    bool tryDirectMonotoneRoute(const std::pair<int,int>& start,
                                const std::pair<int,int>& goal,
                                const std::pair<int,int>& firstStep,
                                const std::pair<int,int>& preGoal,
                                std::vector<std::pair<int,int>>& path) const;

    bool tryDynamicProgrammingRoute(const std::pair<int,int>& start,
                                    const std::pair<int,int>& goal,
                                    const std::pair<int,int>& firstStep,
                                    const std::pair<int,int>& preGoal,
                                    std::vector<std::pair<int,int>>& path) const;

    bool tryDirectMonotoneRouteFromAnchor(const std::pair<int,int>& anchor,
                                          const std::pair<int,int>& goal,
                                          const std::pair<int,int>& preGoal,
                                          std::vector<std::pair<int,int>>& path) const;

    bool tryDynamicProgrammingRouteFromAnchor(const std::pair<int,int>& anchor,
                                              const std::pair<int,int>& goal,
                                              const std::pair<int,int>& preGoal,
                                              std::vector<std::pair<int,int>>& path) const;

    bool trySharedPrefixRoute(const std::pair<int,int>& start,
                              const std::pair<int,int>& goal,
                              const std::pair<int,int>& preGoal,
                              std::vector<std::pair<int,int>>& path) const;

    std::vector<std::pair<int,int>> commitPath(const std::vector<std::pair<int,int>>& path);

    // 记录每个起点的历史布线路径。多扇出时不能只保留最后一条，
    // 后续分叉需要能从任意已完成干线复用公共前缀。
    std::unordered_map<
        std::pair<int,int>,
        std::vector<std::vector<std::pair<int,int>>>,
        pair_hash
    > finishedRoutes;

};

} // namespace iFCN_Lab
