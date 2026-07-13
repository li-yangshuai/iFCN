# GCN+RL Layout Backend

这个目录是 iFCN GUI 使用的第二 Layout/P&R 后端，保留的是运行链路需要的代码：

- `src/algorithm/main/train_layout_ppo.py`: GCN warm start + PPO/RL 压缩主入口。
- `src/algorithm/main/train_universal_graph_ppo.py`: 多电路、冻结随机时钟场、动态图动作的通用 GNN+PPO 训练入口。
- `src/algorithm/main/evaluate_universal_graph_ppo.py`: 在独立电路与独立 clock seeds 上对比 policy 和 warm-start baseline。
- `scripts/gui_universal_agent_runner.py`: GUI 的通用记忆智能体推理、严格随机时钟验证和 `.ifcn` 导出入口。
- `scripts/gui_gcn_rl_runner.py`: GUI 调用入口，支持多个 seed 并行训练并选最优结果。
- `src/algorithm/main/test_randomPhase.py`: GCN 排序、自适应布局、相位感知布线基础实现。
- `src/algorithm/main/test_normal_graph_draw.py`: 面向 2DDWave 时钟方案的 normal graph draw P&R 入口。
- `src/algorithm/src`: Python 侧解析、GCN 和 `.ifcn` 导出工具。
- `src/algorithm/src/stochastic_clock.py`: 先采样后冻结的因果随机时钟场与多场景鲁棒指标。
- `src/algorithm/src/universal_graph_policy.py`: 面向任意电路规模和动态动作候选的有向图 actor/critic。
- `src/parse`、`src/chessboard`、`lib/bindings`: 独立 pybind 后端源码。

通用随机时钟模型目前已具备多电路共享策略、IFCN 拓扑检索记忆、GRU 工作记忆、
冻结 phase 场 exact router、learned route priority、ragged PPO、Python absolute-stage
后验 DRC 和多场景 mean+CVaR 反馈。桌面 GUI 已接入训练后的 checkpoint 推理和严格
合法导出；更大规模 held-out 电路的成功率、面积和延迟仍需继续训练与评估。
设计、现状审计和接入顺序见 `UNIVERSAL_STOCHASTIC_CLOCK.md`。

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

`Legacy PPO (advanced)` 页保留旧版逐电路训练的 runs/workers、GCN epochs、RL episodes、
PPO 参数、repair/pack 和旧 action memory，供对照实验使用。

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
