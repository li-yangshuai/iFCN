# Graphviz / GCN / PPO 布局后端

[返回中文首页](../../README.md) · [English overview](../../README.en.md#optional)

本目录维护 iFCN 的可选 Python 布局布线后端、C++ Python 扩展，以及图策略训练与推理代码。
当前桌面工具栏默认是 **Compact Graph Draw**，下拉菜单包含 **Heuristic P&R** 与
**2DDWave Fixed-Clock P&R**。其中 2DDWave 使用本目录的 Python 后端；GCN/PPO 和记忆策略
通过命令行运行，当前工具栏不提供 `Universal AI P&R` 入口。

目录和脚本名称中的 `gui_` 保留兼容含义：runner 仍可独立调用，生成的 `.ifcn` 可在桌面中打开。
基础 C++ GUI、Compact Graph 和物理仿真无需安装本模块；使用 Python 布局器时再配置依赖。
所有命令从 **iFCN 仓库根目录** 执行。

## 代码入口

| 路径 | 功能 |
| --- | --- |
| `src/algorithm/main/test_normal_graph_draw.py` | 固定 2DDWave 图布局与布线 |
| `src/algorithm/main/test_randomPhase.py` | 自适应放置与相位感知布线基础实现 |
| `src/algorithm/main/train_layout_ppo.py` | Graphviz/sifting warm start 与逐电路 PPO 压缩 |
| `src/algorithm/main/train_universal_graph_ppo.py` | 多电路、冻结随机时钟场、动态图动作的通用 GNN/PPO 训练 |
| `src/algorithm/main/evaluate_universal_graph_ppo.py` | 独立电路和独立时钟种子上的 policy / warm-start 对比 |
| `scripts/gui_universal_agent_runner.py` | 通用记忆策略推理、严格随机时钟验证与 `.ifcn` 导出 |
| `scripts/gui_gcn_rl_runner.py` | 多种子并行逐电路训练与结果选择 |
| `src/algorithm/src/` | Python 解析、排序、布局导出、图策略、随机时钟与检索记忆 |
| `src/parse/`, `src/chessboard/`, `lib/bindings/` | `iFCN_Lab` 的 C++ 解析、路由与 pybind11 源码 |

通用策略包含跨电路共享参数、IFCN 拓扑检索记忆、GRU 工作记忆、冻结相位场精确布线、
学习到的路径优先级、ragged PPO、Python absolute-stage 后验检查，以及多场景 mean/CVaR 反馈。
这些实现能力不代表已保证任意新电路的成功率、面积或延迟；泛化能力需要独立训练与评估。
详细模型与阶段审计见 [UNIVERSAL_STOCHASTIC_CLOCK.md](UNIVERSAL_STOCHASTIC_CLOCK.md)；
其中历史实验与 GUI 接入记录不作为当前工具栏说明。

## 安装依赖并构建扩展

先安装仓库首页列出的基础构建依赖，然后创建 Python 环境。以下使用 CPU 版 PyTorch：

```bash
TORCH_INDEX_URL=https://download.pytorch.org/whl/cpu \
  bash include/gcn_rl_layout/scripts/setup_python_env.sh
include/gcn_rl_layout/myenv/bin/python -m pip install pybind11

cmake -S . -B build-rl -DCMAKE_BUILD_TYPE=Release \
  -DIFCN_BUILD_GCN_RL_BINDINGS=ON \
  -DPython3_EXECUTABLE="$PWD/include/gcn_rl_layout/myenv/bin/python" \
  -Dpybind11_DIR="$(include/gcn_rl_layout/myenv/bin/python -m pybind11 --cmakedir)"
cmake --build build-rl --target iFCN_Lab -j2
```

安装脚本添加 PyTorch、PyTorch Geometric、scikit-learn、Matplotlib 与 NetworkX。
可用 `TORCH_INDEX_URL` 选择与本机兼容的 GPU wheel 源；脚本默认源为 CUDA 12.8。
若复用已配置的系统包，可同时设置 `IFCN_GCN_RL_USE_SYSTEM_SITE=1` 与
`IFCN_GCN_RL_SKIP_TORCH_INSTALL=1`。通过 `IFCN_GCN_RL_VENV` 可改变虚拟环境目录。

扩展仅生成到构建目录的 `python/lib/`，上例为 `build-rl/python/lib/iFCN_Lab*.so`，不会复制到源码目录。
后端自动发现仓库的 `build-rl/python/lib/`、`build/python/lib/` 与
`include/gcn_rl_layout/build/python/lib/`。自定义构建目录可使用优先级最高的显式设置：

```bash
export IFCN_GCN_RL_BINDINGS_DIR=/path/to/build/python/lib
```

也可将 `/path/to/build/python` 添加到 `PYTHONPATH`。解释器与扩展必须使用兼容的 Python ABI；
切换解释器后应重新配置并构建。扩展、虚拟环境、checkpoint 和训练结果均为本地生成文件。

桌面 Python 后端依次查找：

1. `IFCN_GCN_RL_PYTHON` 环境变量；
2. 当前后端目录的 `myenv/bin/python`；
3. 系统 `python3`；
4. 系统 `python`。

显式指定当前工程的后端与解释器：

```bash
export IFCN_GCN_RL_ROOT="$PWD/include/gcn_rl_layout"
export IFCN_GCN_RL_PYTHON="$PWD/include/gcn_rl_layout/myenv/bin/python"
```

## 当前确定性布局布线闭环

`test_normal_graph_draw.py` 的 2DDWave 生产流程为：Graphviz dot/mincross 与精确增益
sifting 交叉优化、固定分层右下可达放置、端口预留、RightDown A* 全边布线、按失败端点
压力插入行列，最后执行三阶段“压缩收缩相位算法”。第一阶段从顶部向下扫描上半区
节点，让节点吸收自己唯一出线的相邻 cell；再从底部向上扫描下半区节点，让节点吸收
自己唯一入线的相邻 cell。目标位置必须无其他节点、只被该节点的一根关联线占用，而且
移动方向必须朝当前收缩区间的中心。第二阶段递归二分物理 y 层区间，为内部层建立局部
中心并重复双向收缩；窗口按递归深度广度优先调度，同一节点首次成功后锁定移动方向，
避免不同层级来回振荡。每次移动都完整重布全网并验证节点无重叠、右下可达、失败边为
零和 2DDWave 模板一致。允许面积暂时不变但已用 cell 数不增加的向内移动，以便连续
局部移动释放边界。第三阶段反复删除不含 node 的行/列；候选层即使有 wire 也会先压缩
节点坐标，再丢弃旧线路并完整重布。每次删除都重新校验相位，直到不存在仍可合法删除
的行/列。非法、面积未严格下降或破坏布线/相位的候选完整回退。
重复失败集合长期不改善时会停止扩张并记录停滞原因，避免用无效空白制造超大面积。

`test_randomPhase.py` 不施加 2DDWave 右下流向：先紧凑放置，A* 搜索路径时同步分配相位，
失败后在拥塞端点和跨距中部插入行列并完整重布线；成功后使用同样的由外向内收缩，
每一个收缩候选都重新执行相位感知布线和端口方向校验。

## 批量布局

运行 2DDWave 基准，输出放在构建目录：

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/scripts/run_all_normal_graph_layouts.py \
  --benchmark-root tests/benchmarks_f/TOY --jobs 2 \
  --output-root build/artifacts/normal_2ddwave
```

输出包含逐电路 `.ifcn`、时钟编码布局、日志、JSON，以及聚合 CSV、JSON、Markdown 和
可单独编译的 LaTeX 长表。扩大 `--benchmark-root` 前先检查小规模运行结果与时间预算。

按同一电路清单将 2DDWave 失败项转入随机时钟复测：

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/scripts/run_random_clock_fallbacks.py \
  --normal-results build/artifacts/normal_2ddwave/layout_results.json \
  --output-root build/artifacts/random_clock_fallback
```

批量 runner 隔离进程并限制搜索/运行预算，保留失败与超时；部分路由不能计为合法成功。
这些 Python 随机时钟流程与原生 `ifcn_combinational_pnr june_random` 是不同的可选入口。

## 通用策略：训练、评估与推理

### 训练

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/src/algorithm/main/train_universal_graph_ppo.py \
  --benchmarks tests/benchmarks_f/TOY/xor2.v tests/benchmarks_f/TOY/xnor2.v \
  --clock-mode stochastic-bands --device cpu \
  --episodes 2000 --episodes-per-update 16 \
  --exact-feedback-interval 10 --exact-field-samples 4 \
  --output-dir build/artifacts/universal_graph_ppo
```

这是流程示例，训练耗时与显卡、搜索预算和电路规模有关。仓库不携带已训练权重；
`universal_graph_ppo_best_exact.pt` 只有在训练获得对应的精确评估结果后才可用。

### 独立评估

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/src/algorithm/main/evaluate_universal_graph_ppo.py \
  --checkpoint build/artifacts/universal_graph_ppo/universal_graph_ppo.pt \
  --benchmark-glob 'tests/benchmarks_f/IWLS93/*.v' \
  --clock-field-samples 32 --require-unseen \
  --output-dir build/artifacts/universal_evaluation
```

`--require-unseen` 检查评估电路是否出现在 checkpoint 的训练元数据中。还需选择独立时钟种子，
并分别记录成功率、失败/超时、面积和延迟；对训练电路的成功不能作为泛化结论。

### 推理与严格导出

使用已经训练好的 checkpoint：

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/scripts/gui_universal_agent_runner.py \
  --benchmark tests/benchmarks_f/TOY/xnor2.v \
  --checkpoint build/artifacts/universal_graph_ppo/universal_graph_ppo.pt \
  --output-dir build/artifacts/universal_inference \
  --device cpu --clock-field-samples 8 --require-legal
```

runner 默认启用 `--require-legal`；只有完成布线和相位合法性检查的结果才能作为严格成功导出。
检查结果中的 `strict_success`，不要将降级候选或策略回退误报为策略成功。
输出 `.ifcn` 可在桌面打开；对其进行器件映射和物理功能验证仍是后续步骤。

| 参数 | 含义 |
| --- | --- |
| `--device auto|cpu|cuda` | 选择运行设备，`auto` 优先使用可用 CUDA |
| `--checkpoint` | 指定权重路径；使用构建目录时建议显式指定 |
| `--clock-field-samples` | 冻结时钟场样本数 |
| `--policy-trials` / `--steps-per-episode` | 候选试验数与策略步数 |
| `--exact-eval-timeout-sec` | 精确布线评估超时 |
| `--retrieval-memory` / `--retrieval-top-k` | 检索记忆及邻居数 |
| `--no-allow-exact-memory-retrieval` | 禁止检索相同拓扑的示例 |

`--checkpoint auto` 查找 `IFCN_UNIVERSAL_AGENT_CHECKPOINT` 或后端结果目录下的
`universal_graph_ppo_best_exact.pt`；不会自动找到任意构建目录中的权重。
逐电路的 legacy PPO 仍由 `train_layout_ppo.py` / `gui_gcn_rl_runner.py` 提供，
其固定输入/动作结构与通用图策略不同，checkpoint 不能任意互换。

## 测试与输出管理

基础 CTest 注册了不需要 ML 权重的路由/时钟/解析等回归；完整 Python 算法测试需要本模块依赖。
测试目录同时包含 unittest 类与 pytest 函数，使用 pytest 进行完整收集：

```bash
include/gcn_rl_layout/myenv/bin/python -m pip install pytest
include/gcn_rl_layout/myenv/bin/python -m pytest include/gcn_rl_layout/tests
```

请将输出显式写入 `build/artifacts/` 或其他忽略目录；不要提交模型、虚拟环境、扩展二进制、
波形和批量实验结果。电路样例来自仓库的 `.ifcn` 与必要 Verilog 输入。
