#include "phaseSolver.h"

namespace fcngraph
{

    PhaseSolver::PhaseSolver(const std::vector<Path> &p, const std::vector<int> &sp)
        : paths(p), start_phases(sp), gen(rd())
    {
        find_cross_nodes();
    }

    // 主求解函数
    std::vector<std::vector<int>> PhaseSolver::solve()
    {
        int attempts = 0;
        const int max_attempts = 1000000;

        while (attempts++ < max_attempts)
        {
            // 1. 随机分配交叉点相位
            assign_cross_phases();

            // 2. 检查所有路径可行性
            if (validate_all_paths())
            {
                // 3. 生成最终相位分配
                return generate_all_phases();
            }
        }
        std::cout << "检查所有路径可行性失败" << std::endl;
        throw std::runtime_error("No valid solution found");
    }

    // 找到所有路径之间的复用路径
    std::map<std::pair<size_t, size_t>, Path> PhaseSolver::find_all_reused_paths(const std::vector<Path> &paths)
    {
        std::map<std::pair<size_t, size_t>, Path> reused_paths;

        // 比较每两条路径
        for (size_t i = 0; i < paths.size(); ++i)
        {
            for (size_t j = i + 1; j < paths.size(); ++j)
            {
                Path reused_path;

                // 检查起始节点是否相同
                if (paths[i].grids[0] == paths[j].grids[0])
                {
                    // 逐节点比较
                    size_t min_length = std::min(paths[i].grids.size(), paths[j].grids.size());
                    for (size_t k = 1; k < min_length; ++k)
                    {
                        if (paths[i].grids[k] == paths[j].grids[k])
                        {
                            reused_path.grids.push_back(paths[i].grids[k]); // 记录相同的节点
                        }
                        else
                        {
                            break; // 遇到第一个不同的节点，停止比较
                        }
                    }
                }

                // 如果找到复用路径，则记录下来
                if (!reused_path.grids.empty())
                {
                    reused_paths[{i, j}] = reused_path;
                }
            }
        }

        return reused_paths;
    }
    // 查找交叉节点
    void PhaseSolver::find_cross_nodes()
    {
        std::unordered_map<Grid, int> count;
        for (const auto &path : paths)
        {
            std::unordered_set<Grid> unique(path.grids.begin(), path.grids.end());
            for (const auto &g : unique)
                count[g]++;
        }

        // 找到所有复用路径
        auto reused_paths = find_all_reused_paths(paths);

        // 收集复用路径中的所有节点，排除最后一个元素，剔除复用路径中的节点是为防止在对交叉节点随机分配相位时，很难找到可行解，保留后一个元素是为了确保复用的两条路径时钟相位不发生冲突
        std::unordered_set<Grid> reused_nodes;
        for (const auto &[key, path] : reused_paths)
        {
            // 遍历 path.grids，排除最后一个元素
            for (auto it = path.grids.begin(); it != std::prev(path.grids.end()); ++it)
            {
                reused_nodes.insert(*it);
            }
        }

        // 剔除复用路径中的节点，只保留真正的交叉节点
        for (const auto &[g, c] : count)
        {
            if (c >= 2 && reused_nodes.find(g) == reused_nodes.end())
            {
                cross_nodes.push_back(g);
            }
        }
    }

    // 随机分配交叉点相位
    void PhaseSolver::assign_cross_phases()
    {
        global_phases.clear();
        for (const auto &g : cross_nodes)
            global_phases[g] = phase_dist(gen);
    }

    // 验证单条路径可行性
    bool PhaseSolver::validate_path(size_t path_idx)
    {
        const auto &path = paths[path_idx];
        std::vector<std::pair<int, int>> check_points;

        // 收集所有相位确定点
        if (path_idx < start_phases.size())
        {
            check_points.emplace_back(0, start_phases[path_idx]);

            for (size_t i = 1; i < path.grids.size(); ++i)
            {
                if (global_phases.count(path.grids[i]))
                    check_points.emplace_back(i, global_phases[path.grids[i]]);
            }
        }
        else
        {
            for (size_t i = 0; i < path.grids.size(); ++i)
            {
                if (global_phases.count(path.grids[i]))
                    check_points.emplace_back(i, global_phases[path.grids[i]]);
            }
        }

        std::sort(check_points.begin(), check_points.end());

        // 检查每段相位跨度
        for (size_t i = 1; i < check_points.size(); ++i)
        {
            auto [pos1, phase1] = check_points[i - 1];
            auto [pos2, phase2] = check_points[i];
            int steps = pos2 - pos1;
            int required = phase_distance(phase1, phase2);

            if (required > steps)
                return false;
        }
        return true;
    }

    // 计算相位跨度
    int PhaseSolver::phase_distance(int from, int to) const
    {
        if (from == to)
            return 0;
        if (to > from)
            return to - from;
        return (4 - from) + to;
    }

    // 验证所有路径
    bool PhaseSolver::validate_all_paths()
    {
        for (size_t i = 0; i < paths.size(); ++i)
            if (!validate_path(i))
                return false;
        return true;
    }

    std::vector<int> PhaseSolver::generate_path(size_t path_idx)
    {
        const auto &path = paths[path_idx];
        std::vector<int> phases(path.grids.size(), -1);
        std::vector<std::pair<int, int>> key_points;

        // 收集并排序关键点
        if (path_idx < start_phases.size()) // 对第一层不能包括起始节点
        {
            key_points.emplace_back(0, start_phases[path_idx]);
            for (size_t i = 1; i < path.grids.size(); ++i)
            {
                if (global_phases.count(path.grids[i]))
                {
                    key_points.emplace_back(i, global_phases[path.grids[i]]);
                }
            }
        }
        else
        {
            for (size_t i = 0; i < path.grids.size(); ++i) // 对其他层需要包含起始节点
            {
                if (global_phases.count(path.grids[i]))
                {
                    key_points.emplace_back(i, global_phases[path.grids[i]]);
                }
            }
        }
        std::sort(key_points.begin(), key_points.end());

        // 生成相位序列
        for (size_t seg = 0; seg < key_points.size(); ++seg)
        {
            auto [current_pos, current_phase] = key_points[seg];
            phases[current_pos] = current_phase;

            if (seg > 0)
            {
                auto [prev_pos, prev_phase] = key_points[seg - 1];
                const int steps = current_pos - prev_pos;
                const int phase_steps = phase_distance(prev_phase, current_phase);

                // 分情况处理相位填充
                if (phase_steps == steps)
                {
                    // 精确递增
                    for (int i = 1; i <= steps; ++i)
                    {
                        int phase = (prev_phase + i) % 4;
                        phases[prev_pos + i] = phase == 0 ? 4 : phase;
                    }
                }
                else if (phase_steps < steps)
                {
                    if ((steps - phase_steps) >= 4)
                    {
                        // 递增后超过4个位置
                        for (int i = 1; i <= (phase_steps + 4); ++i)
                        {
                            int phase = (prev_phase + i) % 4;
                            phases[prev_pos + i] = phase == 0 ? 4 : phase;
                        }
                        // 剩余位置保持最终相位
                        std::fill(phases.begin() + prev_pos + phase_steps + 4,
                                  phases.begin() + current_pos + 1,
                                  current_phase);
                    }
                    else
                    {
                        // 部分递增后保持
                        for (int i = 1; i <= phase_steps; ++i)
                        {
                            int phase = (prev_phase + i) % 4;
                            phases[prev_pos + i] = phase == 0 ? 4 : phase;
                        }
                        // 剩余位置保持最终相位
                        std::fill(phases.begin() + prev_pos + phase_steps + 1,
                                  phases.begin() + current_pos + 1,
                                  current_phase);
                    }
                }
            }
        }

        // 处理最后一个关键点之后的节点
        if (!key_points.empty())
        {
            int last_phase = key_points.back().second;
            size_t last_pos = key_points.back().first;
            for (size_t i = last_pos + 1; i < phases.size(); ++i)
            {
                phases[i] = (last_phase + (i - last_pos)) % 4;
                if (phases[i] == 0)
                    phases[i] = 4;
            }
        }

        // 相位连续性验证（调试用）
        for (size_t i = 1; i < phases.size(); ++i)
        {
            int diff = phases[i] - phases[i - 1];
            if (diff != 1 && diff != -3 && diff != 0)
            {
                throw std::runtime_error("Phase continuity violation");
            }
        }

        return phases;
    }

    // 生成所有相位
    std::vector<std::vector<int>> PhaseSolver::generate_all_phases()
    {
        std::vector<std::vector<int>> result;
        for (size_t i = 0; i < paths.size(); ++i)
            result.push_back(generate_path(i));
        return result;
    }
}