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
#include <random>
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

    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> phase_dist{1, 4};

public:
    PhaseSolver(const std::vector<Path> &p, const std::vector<int> &sp);

    // 主求解函数
    std::vector<std::vector<int>> solve();

private:
    std::map<std::pair<size_t, size_t>, Path> find_all_reused_paths(const std::vector<Path> &paths);
    // 查找交叉节点
    void find_cross_nodes();

    // 随机分配交叉点相位
    void assign_cross_phases();

    // 验证单条路径可行性
    bool validate_path(size_t path_idx);

    // 计算相位跨度
    int phase_distance(int from, int to) const;

    // 验证所有路径
    bool validate_all_paths();

    std::vector<int> generate_path(size_t path_idx);

    // 生成所有相位
    std::vector<std::vector<int>> generate_all_phases();
};

}