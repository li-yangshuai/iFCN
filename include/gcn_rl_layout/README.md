# GCN+RL Layout Backend

这个目录是 iFCN GUI 使用的第二 Layout/P&R 后端，保留的是运行链路需要的代码：

- `src/algorithm/main/train_layout_ppo.py`: GCN warm start + PPO/RL 压缩主入口。
- `scripts/gui_gcn_rl_runner.py`: GUI 调用入口，支持多个 seed 并行训练并选最优结果。
- `src/algorithm/main/test_randomPhase.py`: GCN 排序、自适应布局、相位感知布线基础实现。
- `src/algorithm/src`: Python 侧解析、GCN 和 `.ifcn` 导出工具。
- `src/parse`、`src/chessboard`、`lib/bindings`: 独立 pybind 后端源码。

这里不再保存 benchmark、批量实验脚本、运行结果或虚拟环境。GUI 会把用户选择的 Verilog 作为输入，
输出写到源文件旁边的 `<name>_gcn_rl_layout/` 目录，生成 `<name>_rl_layout.ifcn` 后自动载入版图。

## GUI 参数

点击 `Algorithms -> GCN+RL P&R` 后会弹出参数窗口，可设置：

- GPU/CPU：默认 `Auto (CUDA first)`，有 CUDA 时优先用 GPU。
- Parallel runs / Workers：多 seed 并行训练，最后按合法性和面积选最好结果。
- GCN epochs、RL episodes、steps、PPO epochs、minibatch。
- Placement fast eval / exact routing eval。
- final exact validation、layout memory、shared RL action memory。

默认配置偏向 UI 交互速度：GPU 优先、placement 快评估、保存布局记忆库和 RL action memory。

## 可选构建 Python 扩展

如果本目录没有 `src/algorithm/lib/iFCN_Lab*.so`，GUI 会回退到开发机上的
`/home/lys/projects/github/no_phase_layout_project`。要从 iFCN 源码树构建扩展：

```bash
cmake -S . -B build -DIFCN_BUILD_GCN_RL_BINDINGS=ON \
  -Dpybind11_DIR=/path/to/python/site-packages/pybind11/share/cmake/pybind11
cmake --build build --target iFCN_Lab
```

也可以用环境变量指定外部后端和 Python：

```bash
export IFCN_GCN_RL_ROOT=/path/to/no_phase_layout_project
export IFCN_GCN_RL_PYTHON=/path/to/python
```
