# Classic layout backend / 经典布局后端

This directory provides Graphviz layer ordering, exact-gain sifting, fixed-clock
2DDWave routing, and legality-checked layout contraction. The parser and grid
routers are exposed through the `iFCN_Lab`
C++ extension. Python uses NumPy, SciPy, Matplotlib, and NetworkX.

本目录提供 Graphviz 分层排序、精确增益筛选、固定时钟 2DDWave 布线，
以及经过合法性检查的布局收缩。`iFCN_Lab` 扩展提供 C++ 电路解析与网格布线功能。

Irregular-clock combinational routing uses the repository's single native
`irregular` backend. This Python directory retains the fixed-clock workflow and
shared ordering, grid-routing, and clock-field tools.

组合电路的不规则时钟布局统一使用仓库的原生 `irregular` 后端。
本 Python 目录保留固定时钟流程，以及共用的排序、网格布线和时钟字段工具。

## Setup / 环境配置

Run from the repository root / 在仓库根目录执行：

```bash
bash include/layout_backend/scripts/setup_python_env.sh
cmake -S . -B build-layout \
  -DIFCN_BUILD_LAYOUT_BINDINGS=ON \
  -DPython3_EXECUTABLE="$PWD/include/layout_backend/myenv/bin/python" \
  -Dpybind11_DIR="$(include/layout_backend/myenv/bin/python -m pybind11 --cmakedir)"
cmake --build build-layout --target iFCN_Lab -j2
export IFCN_LAYOUT_PYTHON="$PWD/include/layout_backend/myenv/bin/python"
export IFCN_LAYOUT_BINDINGS_DIR="$PWD/build-layout/python/lib"
```

Install Graphviz (`dot`) separately. The extension is automatically discovered
under `build-layout/python/lib`, `build/python/lib`, or this directory's
`build/python/lib`. Set `IFCN_LAYOUT_BINDINGS_DIR` for another build directory.
`IFCN_LAYOUT_ROOT` can select a relocated backend and `IFCN_LAYOUT_PARSE_MODE`
selects `auto`, `compact`, or `layered` parsing.

Graphviz 的 `dot` 需单独安装。自定义扩展构建位置请设置 `IFCN_LAYOUT_BINDINGS_DIR`；
GUI 启动前设置 `IFCN_LAYOUT_PYTHON` 以选择安装了依赖的解释器。

## Commands / 命令

| Entry / 入口 | Purpose / 功能 |
| --- | --- |
| `src/algorithm/main/test_normal_graph_draw.py` | Fixed-clock 2DDWave placement, routing, and contraction / 固定时钟布局布线与收缩 |
| `scripts/run_all_normal_graph_layouts.py` | Batch fixed-clock validation / 固定时钟批量验证 |
| `src/algorithm/src/graph_layout.py` | Graphviz ordering, structural features, and diagrams / 排序、结构特征和图形 |
| `src/algorithm/src/normalGraphDraw.py` | Fixed-clock expansion, rerouting, and contraction / 固定时钟扩容、重布线与收缩 |
| `src/algorithm/src/stochastic_clock.py` | Shared clock-field representation and checks / 通用时钟字段表示与检查 |

```bash
include/layout_backend/myenv/bin/python \
  include/layout_backend/src/algorithm/main/test_normal_graph_draw.py \
  --benchmark tests/benchmarks_f/TOY/xnor2.v \
  --output-dir output/fixed-clock-demo --seed 1
```

Outputs belong under `output/` or the build directory. Geometry and local clock
checks do not establish input-to-output timing alignment or physical waveform
correctness; retain those as separate validation stages.

生成结果应写入 `output/` 或构建目录。几何及局部时钟检查不代表输入输出时序已对齐，
器件波形正确性也需单独验证。

## Tests / 测试

```bash
include/layout_backend/myenv/bin/python -m pip install pytest
include/layout_backend/myenv/bin/python -m pytest include/layout_backend/tests
```

The suite covers graph ordering, right/down port direction, routing,
contraction, stage snapshots, negotiated congestion, and clock fields.
