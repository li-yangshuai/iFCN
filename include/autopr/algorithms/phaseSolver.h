#pragma once
#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <unordered_map>
#include <utility> // For std::pair
#include <functional> // For std::hash
#include <vector>
#include <unordered_set>
// 定义一个自定义哈希函数
namespace std {
    template <>
    struct hash<std::pair<int, int>> {
        std::size_t operator()(const std::pair<int, int>& p) const noexcept {
            return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
        }
    };
}

namespace fcngraph {
    // 定义 Path 结构
struct Path {
    std::vector<std::pair<int, int>> grids;
};

using Grid = std::pair<int, int>;

class PhaseSolver
{
private:
    std::vector<Path> paths;
    std::vector<Grid> cross_nodes;
    std::vector<int> start_phases;
    std::unordered_map<Grid, int> global_phases;
    int phase_count = 4;

public:
    PhaseSolver(const std::vector<Path> &p, const std::vector<int> &sp, int phaseCount = 4);
    PhaseSolver(const std::vector<Path> &p, int phaseCount = 4);

    // 主求解函数
    std::vector<std::vector<int>> solve();

    const auto& return_cross_nodes() const {
        return cross_nodes;
    }

private:
    std::map<std::pair<size_t, size_t>, Path> find_all_reused_paths(const std::vector<Path> &paths);
    // 查找交叉节点
    void find_cross_nodes();

    // 验证单条路径可行性
    bool validate_path(size_t path_idx);

    // 计算相位跨度
    int phase_distance(int from, int to) const;
    int phase_after_steps(int from, int steps) const;

    // 验证所有路径
    bool validate_all_paths();

    std::vector<int> generate_path(size_t path_idx);

    // 生成所有相位
    std::vector<std::vector<int>> generate_all_phases();
};

}
