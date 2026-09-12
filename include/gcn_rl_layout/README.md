# Graphviz+sifting + RL Layout Backend

这个目录是 iFCN GUI 使用的第二 Layout/P&R 后端，保留的是运行链路需要的代码：

- `src/algorithm/main/train_layout_ppo.py`: Graphviz dot/mincross + 精确增益 sifting warm start 和 PPO/RL 压缩主入口。
- `src/algorithm/main/train_universal_graph_ppo.py`: 多电路、冻结随机时钟场、动态图动作的通用 GNN+PPO 训练入口。
- `src/algorithm/main/evaluate_universal_graph_ppo.py`: 在独立电路与独立 clock seeds 上对比 policy 和 warm-start baseline。
- `scripts/gui_universal_agent_runner.py`: GUI 的通用记忆智能体推理、严格随机时钟验证和 `.ifcn` 导出入口。
- `scripts/gui_gcn_rl_runner.py`: GUI 调用入口，支持多个 seed 并行训练并选最优结果。
- `src/algorithm/main/test_randomPhase.py`: Graphviz+sifting 排序、自适应布局、相位感知布线基础实现。
- `src/algorithm/main/test_normal_graph_draw.py`: 面向 2DDWave 时钟方案的 normal graph draw P&R 入口。
- `src/algorithm/src`: Python 侧解析、Graphviz+sifting（并保留旧 GCN API 兼容层）和 `.ifcn` 导出工具。
- `src/algorithm/src/stochastic_clock.py`: 先采样后冻结的因果随机时钟场与多场景鲁棒指标。
- `src/algorithm/src/universal_graph_policy.py`: 面向任意电路规模和动态动作候选的有向图 actor/critic。
- `src/parse`、`src/chessboard`、`lib/bindings`: 独立 pybind 后端源码。

通用随机时钟模型目前已具备多电路共享策略、IFCN 拓扑检索记忆、GRU 工作记忆、
冻结 phase 场 exact router、learned route priority、ragged PPO、Python absolute-stage
后验 DRC 和多场景 mean+CVaR 反馈。桌面 GUI 已接入训练后的 checkpoint 推理和严格
合法导出；更大规模 held-out 电路的成功率、面积和延迟仍需继续训练与评估。
设计、现状审计和接入顺序见 `UNIVERSAL_STOCHASTIC_CLOCK.md`。

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

全电路 2DDWave 回归命令：

```bash
python3 include/gcn_rl_layout/scripts/run_all_normal_graph_layouts.py \
  --include-generated-sources --jobs 2 \
  --output-root include/gcn_rl_layout/results/<experiment-name>
```

目录内会保存逐电路 IFCN、encoded IFCN、日志和 JSON，以及聚合 CSV、JSON、Markdown
和可单独编译的 LaTeX 长表。

将 2DDWave 未通过项按相同电路清单转入随机时钟复测：

```bash
python3 include/gcn_rl_layout/scripts/run_random_clock_fallbacks.py \
  --normal-results include/gcn_rl_layout/results/<experiment-name>/layout_results.json \
  --output-root include/gcn_rl_layout/results/<experiment-name>/random_clock_fallback
```

回退脚本同样隔离每个进程、限制单边 A* 状态数和单电路时间，并保留失败/超时，
不会把部分路由 IFCN 计为合法成功。

最小通用训练示例：

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/src/algorithm/main/train_universal_graph_ppo.py \
  --benchmarks tests/benchmarks_f/TOY/xor2.v tests/benchmarks_f/TOY/xnor2.v \
  --clock-mode stochastic-bands \
  --episodes 2000 --episodes-per-update 16 \
  --exact-feedback-interval 10 --exact-field-samples 4
```

在训练集之外的电路与时钟种子上独立评估（`--require-unseen` 会阻止误用训练电路）：

```bash
include/gcn_rl_layout/myenv/bin/python \
  include/gcn_rl_layout/src/algorithm/main/evaluate_universal_graph_ppo.py \
  --checkpoint include/gcn_rl_layout/results/universal_graph_ppo/universal_graph_ppo.pt \
  --benchmark-glob 'tests/benchmarks_f/IWLS93/*.v' \
  --clock-field-samples 32 --require-unseen
```

这里不再保存 benchmark、批量实验结果或虚拟环境。GUI 会把用户选择的 Verilog 作为输入，
输出写到源文件旁边的 `<name>_gcn_rl_layout/` 目录，生成 `<name>_rl_layout.ifcn` 后自动载入版图。

## Python 环境

`fcnx_gui` 本体是 C++/Qt 程序，但工具栏的 `Universal AI P&R` 会在后台启动本目录的
Python 后端。默认查找顺序是：

1. 环境变量 `IFCN_GCN_RL_PYTHON`
2. `include/gcn_rl_layout/myenv/bin/python`
3. 系统 `python3`
4. 系统 `python`

当前工程不会再默认回退到其他工程目录。建议在本目录创建本地环境：

```bash
cd include/gcn_rl_layout
./scripts/setup_python_env.sh
```

如果机器不使用 CUDA 12.8，可以指定 PyTorch wheel 源，例如 CPU 版本：

```bash
TORCH_INDEX_URL=https://download.pytorch.org/whl/cpu ./scripts/setup_python_env.sh
```

如果系统 Python 里已经装好了 PyTorch，也可以复用系统包，只补齐其他依赖：

```bash
IFCN_GCN_RL_USE_SYSTEM_SITE=1 IFCN_GCN_RL_SKIP_TORCH_INSTALL=1 ./scripts/setup_python_env.sh
```

注意：较新的显卡需要匹配的 CUDA/PyTorch wheel。默认脚本会在当前 `myenv` 内安装
`TORCH_INDEX_URL` 指向的 PyTorch，不会依赖系统 Python 里的 PyTorch。

也可以显式指定 GUI 使用的解释器：

```bash
export IFCN_GCN_RL_PYTHON=/home/lys/projects/github/iFCN/include/gcn_rl_layout/myenv/bin/python
./build/fcnx_gui
```

## GUI 参数

点击 `Universal AI P&R` 后会弹出分组参数窗口。默认页是通用智能体推理：

- GPU/CPU：默认 `Auto (CUDA first)`，有 CUDA 时优先用 GPU。
- `Fast preview / Balanced / High quality`：控制随机时钟样本、策略试验数、GRU 步数和 exact timeout。
- checkpoint：默认 `auto`，自动选择最新 `universal_graph_ppo_best_exact.pt`。
- causal clock mode、clock-aligned start、retrieval top-k 和同拓扑记忆检索。
- 每个候选都经过 exact routing；只有 `strict_success=true` 才导出并加载 `.ifcn`。

`Legacy PPO (advanced)` 页保留旧版逐电路训练的 runs/workers、Graphviz/sifting 搜索预算、
RL episodes、PPO 参数、repair/pack 和旧 action memory，供对照实验使用。

## 可选构建 Python 扩展

如果本目录没有 `src/algorithm/lib/iFCN_Lab*.so`，需要从 iFCN 源码树构建扩展：

```bash
cmake -S . -B build -DIFCN_BUILD_GCN_RL_BINDINGS=ON \
  -Dpybind11_DIR=/path/to/python/site-packages/pybind11/share/cmake/pybind11
cmake --build build --target iFCN_Lab
```

也可以用环境变量指定当前工程内的后端和 Python：

```bash
export IFCN_GCN_RL_ROOT=/home/lys/projects/github/iFCN/include/gcn_rl_layout
export IFCN_GCN_RL_PYTHON=/home/lys/projects/github/iFCN/include/gcn_rl_layout/myenv/bin/python
```
