# iFCN 时序 QCA 自动布局布线交接

更新时间：2026-08-27

工作分支：`feat/sequential-circuit-automation`

基准提交：`700cf95179ef91bf08bb069bb4998e152151e237`

> 当前工作树包含大量尚未提交的时序代码，也包含其他组合电路、GCN-RL
> 和 UI 工作。接手时不要执行 `git reset --hard`、`git clean` 或覆盖整个
> 工作树。上述基准提交不包含本文描述的全部实现。

> **2026-08-27 P0 语义纠正：相位单位是 5x5 coarse clock tile。** 同一
> tile 内所有 QCA 元胞和所有 layer 必须同相；元胞/层不产生额外 epoch
> occurrence。此前短暂生成的 `qca_cell/layer_aware_xyz` 相位批次无效，已由
> 本文件第 8.3 节所列 corrected tile-phase artifact 覆盖，consumer 也会明确
> 拒绝该旧格式。

## 1. 项目定位

本项目当前聚焦：

> **面向 QCA 时序电路的反馈感知自动布局布线，以及布线后的全局时钟闭合。**

建议论文题目：

> *Feedback-Aware Placement and Routing for Sequential QCA Circuits with
> Post-Route Clock Closure*

核心输入是带寄存器状态边界的 Verilog，核心输出是保留真实有向反馈线路的
门级 IFCN 版图、经 solver 和独立 validator 检查的相位/绝对 epoch/启动间隔
可行性见证，以及沿用 iFCN 原生样式的 LaTeX 版图。

当前成果应准确称为：

- sampled-state 的时序门级 P&R 研究原型；
- 反馈线路、结构 Mapping/DRC，以及以 coarse clock tile 为 occurrence 的
  `phase + absolute epoch + II` 已打通；
- QCA 状态器件尚未完成物理表征，多周期物理功能尚未签核。

它不是已经通过器件级验证的通用 QCA DFF/latch 自动设计系统。功耗和模拟器
加速不属于论文主贡献，只保留为探索性诊断。

## 2. 已实现的数据流

```text
RTL .v
  -> Yosys synthesis/write_json
  -> scripts/yosys_json_to_seqir.py
       -> ifcn.seqir.v0：seqir.json
       -> register-cut AOI DAG：cut.v
       -> D/Q 状态边界：state.json
  -> 独立的一步状态转移穷举检查
  -> 对 d=0 调度图进行分层和门放置
  -> 恢复 iteration_distance=1 的真实反馈物理网
  -> phase-blind 四方向 A* 布线全部普通网和反馈网
  -> sequential-aware Mapping + crossover DRC
       -> 保留有序迂回 waypoint
       -> 生成端口边界到端口边界的有序、layer-aware QCA cell trace
       -> 该 trace 只负责器件拓扑/连通性，不参与时钟 occurrence 计数
  -> 固定 coarse route tile 几何上的全局 phase/epoch/II 闭合
       -> 内置 bounded reference solver，或
       -> JSON 导出 + Z3 求解 + C++ 独立复核
  -> .ifcn（含唯一权威 tile phase map）+ <output.ifcn>.json + 原生样式 layout.tex
  -> UI SVG/PDF 或 QCADesigner 2.0 .qca 元胞级输出
```

`include/autopr/sequential/sequentialIr.*` 已实现独立的双图验证和分层模型，
但当前 RTL 主运行链仍经过：

```text
Python SeqIR -> legacy cut Verilog -> Parse/CircuitGraph
```

尚未让 C++ P&R 直接消费 `SequentialIR`。

## 3. 时序模型与算法合同

### 3.1 双图语义

- `physical graph` 保留所有普通网和反馈网；P&R、导出和 DRC 不能删除反馈边。
- `schedule graph` 只保留 `iterationDistance == 0` 的依赖，用于 DAG 分层。
- 删除所有正距离边后仍有环，表示组合环，当前版本明确拒绝。
- 正距离边只解除当前拍的调度依赖，不代表物理线路被删除。

### 3.2 当前循环 P&R

真正恢复循环反馈的主程序是 `ifcn_paper_cyclic_pnr`。
`ifcn_sequential_pnr` 只是 register-cut 抽象基线，不是完整时序物理版图。

当前主程序的做法：

1. 从 `d=0` cut DAG 建立物理放置层；先过滤只用于 schedule cut 的 Q
   pseudo-node，避免幽灵槽位，同时保留旧 Q-inclusive 居中作为不回退兜底。
2. 同时枚举 unit/各向异性间距、原 spacing ladder、反馈加权 barycenter
   sweep 和有界 adjacent-swap 层内顺序。
3. 将每个 Q pseudo-input 的扇出 `Q -> v` 改为
   `D -> v, iterationDistance=1`，再删除 Q pseudo-node。
4. 对完整物理边集使用 phase-blind 四方向 A*；先在按四边独立扩张的紧致
   routing window 中搜索，再完整保留原无界 4-policy + 48-seed 路由兜底。
5. 对 DRC-valid 候选按 `bbox area -> route steps -> max dimension -> perimeter`
   排序，并原子保存/恢复 node、route 和 chessboard；随后对前 16 个候选做
   最多 256 state/candidate 的单调 seam contraction。
6. 所有粗网格线路完成后，以每条 `graph.routes` 的有序 coarse tile 序列
   建立全局时钟问题；一个 5x5 tile 是一个 clock resource，每次路径经过是
   一个 occurrence。随后仍执行 Sequential Mapping，生成 source terminal
   boundary 到 sink terminal boundary 的有序、三维相邻 QCA cell trace，
   但该 trace 只用于器件拓扑、连通性和 crossover DRC，绝不产生额外时钟
   occurrence。外部 Z3 runner 按面积 rank 逐个求解，选择第一个
   phase/epoch/II SAT 几何；默认 rank 上限 64、ladder 总预算 120 秒，截断
   时保守报告 `LIMIT`。

当前失败后仍是整轮 reset/retry，不是 negotiated congestion 或 selective
rip-up/reroute。这是下一阶段最重要的算法缺口。

### 3.3 布线后时钟闭合

内部统一使用 0-based phase。每个 route occurrence 有绝对整数 epoch `tau`，
每个逻辑事件有 epoch `theta`：

```text
phase(resource(o)) = tau(o) mod P
tau(i+1) - tau(i) in {0, 1}
固定物理路径：tau(first) = theta(source)
固定物理路径：tau(last) = theta(sink) + distance * II
精确宏/时序弧：theta(sink) + distance * II - theta(source) = latency
II = P * k, k >= 1
```

上述 route occurrence 是有序 coarse route tile，而不是 Mapping 后的 QCA
元胞。其中：

- 同源 fanout trunk 通过相同 epoch variable 绑定到同一个绝对 epoch；
- 同一 coarse tile 内所有细粒度 XY、所有 L0/L1/L2 元胞都继承该 tile 的
  唯一相位；层和细元胞都不产生新的 phase advance；
- 不同源 crossing 使用同一个 tile clock resource，因此可有不同绝对 epoch，
  但必须具有同一个 modulo phase；
- 反馈路径必须获得正的跨拍推进，不能只验证局部模 4 连续；
- 最多允许 4 个连续同相 route tile；这是 clock-zone abstraction 的约束，
  不是“4 个 QCA 元胞”的约束。一个 tile 内可以有多个线路/门/交叉元胞，
  它们仍全部同相且只计一个 clock zone；
- layer-aware trace 不改变相位，只证明器件导出保持有序 waypoint、端口连通
  和合法 crossover 柱；
- 输出到 legacy GridCell/LaTeX 时统一转换为 1..4，避免 off-by-one。

内置 solver 先对 II candidate 排序去重，再按升序枚举，严格区分 `SAT`、
`UNSAT`、`LIMIT` 和
`INVALID_INPUT`。当前 bounded 合同为：

- phase：2..8；
- event：最多 256；
- resource/occurrence：各最多 4096；
- route：最多 128；
- 总 route edge：最多 8192；
- exact timing arc：最多 512；
- II candidate：最多 16；
- iteration distance：当前只支持 0/1。

可选 Z3 后端只负责固定几何的可行性。Z3 产出的 TSV 必须重新交给
`GlobalPhaseSolver::validateSolution` 检查，不能直接作为最终签核。
四相等 2 的幂相位数在安全数值范围内使用 signed 32-bit BitVec 编码，
resource phase 用 epoch 低位同余；非 2 的幂或超出安全范围时回退到原始
整数编码。BitVec 只是求解加速，最终仍以 C++ 精确整数 replay 为准。

### 3.4 IFCN 时序标识与元胞映射合同

新生成的时序 IFCN 必须恰好包含一次全局标识：

```text
#mapping mode: sequential
```

并在 paths section 中为每条 route 紧前写出：

```text
#iteration_distance=N
(source,sink): (x0,y0),...,(xn,yn);
```

canonical value 只有 `combinational|sequential`；显式未知/冲突 mode、负数或
溢出的 distance、重复/悬空 route directive，以及显式 combinational 配正
distance 都 fail closed。为了读取旧 artifact，缺失 mode 时可由正 distance，
或两个已知 v0 sequential flow 名推断 Sequential；其他 legacy 文件保持
Combinational。显式 Sequential 的每条 route 都必须有 distance，当前 writer
即使为 0 也不能省略。

时钟闭合后的新文件还必须写出：

```text
#phase granularity: tile
#tile phase drc scope: ordered_route_tiles
#max same phase tiles: 4
#observed max same phase tile run: N
#phase map
(tile_x,tile_y): zero_based_phase;
#phase map
```

`#phase map` 是唯一权威相位表。Sequential energy/GUI consumer 对每个实际
mapped cell 严格使用 `phase[(physical_x/5, physical_y/5)]`；同一 tile 的普通
线、门模板、L0/L1/L2 crossover/vertical stack 必须全部同相。缺少任一占用
tile、相位越界、route 相邻 tile 不是 hold/+1，或连续同相 tile 超过 4 均
fail closed。旧错误格式 `#phase granularity: qca_cell`、
`#physical phase trace` 和 `#physical phase map` 在 Sequential 模式下明确拒绝，
不会再静默导出错误器件图；组合电路的 legacy physical-map 兼容路径保留。

`Mapping::mapping_line` 现在显式接收 `MappingMode` 和与 route 同序的
`iterationDistances`。Sequential 分支不是组合 mapper 的无条件复用：

- 保留非单调/轴向反转的合法迂回路径，并验证每个有序 intermediate tile；
- `node_mapping` 在 Sequential 模式为 `input/output` 终端同时生成实际存在的
  fanin 与 fanout 端口；状态 cut 的 `d` 虽以 output 表示，也能作为反馈源；
- 按 route 指定方向检查 source port、每个 waypoint 的入口/出口和 sink port，
  并验证 waypoint tile 内部的 4-neighbor 连通，禁止接错支路或抄近路；
- 允许同源连续 shared prefix 后单向分叉；
- 拒绝重复 tile/回头、strict-prefix sink tap、分叉后重汇合、重复端点，及
  不同源信号共用 coarse edge；
- Mapping 失败前先清空旧 node/route/crossover state，避免复用对象残留旧结果；
- 暴露 `orderedLayerAwarePhysicalRoutes`，按每条 route 自己拥有的 maximal
  crossover run 生成入口/出口 L0-L1-L2 pillar，并以确定的 source-to-sink
  顺序返回 terminal boundary 之间的 exact-layer 物理元胞路径；该 API 仅
  描述器件拓扑，不参与相位/epoch/最大同相 run；
- 拒绝 L0..L2 之外的层、非 L0 端点、L1 横向传播、同一路径重复 exact site，
  以及不同 source 复用同一 `(x,y,layer)` site；
- 仍执行 crossover DRC。GUI 和 QCA exporter 用 mapped cell 所属 coarse tile
  查询唯一相位；layer-aware site 只用于选择 Normal/Crossover/Vertical 模式。

GUI `GateLevelMapping`、`MappingExecutor`、`ifcn_mapping_metrics`、
`ifcn_energy_analysis`、两个 sequential PNR 和 physical state macro 均已接入
该分支。GCN IFCN dataset 会保存 normalized mode 与逐边 distance，并将其
加入 JSON/fingerprint；现有 offline learner 只支持 DAG，因此明确拒绝
Sequential sample，不能静默当组合图训练。

cyclic PNR report 的 canonical 几何计数是 `mapped_unique_xy_sites`；迁移期
继续写旧 `mapped_qca_cells` alias，但它仍是 layer-collapsed XY 数。
`tile_clock_resources` 是参与 solver 的 unique coarse tile 数；
`mapped_layer_cell_records` 是 mapper 实际发射的 layer-aware site 数，且与
`.qca` 的 `QCADCell` record 数相等。这些口径不能与 layer-collapsed
`mapped_unique_xy_sites` 混用，更不能把器件元胞数解释为时钟 occurrence 数。

### 3.5 LaTeX 输出合同

时序流程复用现有 `CircuitGraph::printLaTex` 的 node、`c1..c4` 和 route 样式。
新代码只负责提供节点坐标、线路坐标和求得的相位；不要在时序导出器中重写
节点大小、形状或 TikZ style。

## 4. 关键代码路径

| 模块 | 路径 |
|---|---|
| Yosys JSON -> SeqIR/cut DAG | `scripts/yosys_json_to_seqir.py` |
| RTL 批量实验 | `scripts/run_sequential_rtl_experiments.py` |
| 循环反馈 P&R 主程序 | `src/app/ifcn_paper_cyclic_pnr.cpp` |
| register-cut 基线 | `src/app/ifcn_sequential_pnr.cpp` |
| C++ SeqIR 双图模型 | `include/autopr/sequential/sequentialIr.{h,cpp}` |
| 全局时钟问题和 reference solver | `include/autopr/sequential/globalPhaseSolver.{h,cpp}` |
| 可选 Z3 后端 | `scripts/solve_global_clock_z3.py` |
| 手工结构宏原型 | `include/autopr/sequential/physicalStateMacro.{h,cpp}` |
| 手工结构宏 CLI | `src/app/ifcn_physical_state_layout.cpp` |
| 时钟消融实验 | `src/app/ifcn_sequential_clock_experiment.cpp`、`scripts/run_sequential_clock_experiments.py` |
| 论文电路重建实验 | `scripts/run_sequential_paper_benchmarks.py` |
| 物理/Simon 诊断 | `scripts/benchmark_sequential_cyclic_physical.py` |
| 最终结果聚合 | `scripts/aggregate_sequential_experiments.py` |
| IFCN mode/distance resolver | `include/autopr/io/ifcnMappingMetadata.h` |
| 元胞级 Mapping | `include/autopr/algorithms/mapping.{h,cpp}` |
| GUI IFCN parser/mapper | `src/controllers/GateLevelMapping.{h,cpp}` |
| 纯 CLI QCA exporter | `src/app/ifcn_energy_analysis.cpp` |
| RTL 样例 | `tests/benchmarks_f/SEQUENTIAL/rtl_v/` |
| 论文电路重建 | `tests/benchmarks_f/SEQUENTIAL/papers/` |

主要回归：

- `tests/SequentialGlobalPhaseSolverUnitTest.cpp`
- `tests/SequentialIrUnitTest.cpp`
- `tests/PhysicalStateMacroUnitTest.cpp`
- `tests/test_yosys_json_to_seqir.py`
- `tests/test_solve_global_clock_z3.py`
- `tests/test_validate_sequential_paper_benchmark.py`
- `tests/test_aggregate_sequential_experiments.py`
- `tests/test_cyclic_compaction_report.py`
- `tests/test_cyclic_compact_area_report.py`
- `tests/test_run_sequential_rtl_experiments.py`
- `tests/MappingCrossoverUnitTest.cpp`
- `tests/test_ifcn_sequential_mapping_metadata.py`
- `include/gcn_rl_layout/tests/test_ifcn_layout_dataset.py`
- `include/gcn_rl_layout/tests/test_ifcn_offline_learning.py`

## 5. 支持范围

当前 v0 已测试的目标范围：

- 单逻辑时钟域；注意 importer 目前尚未强制检查这一点；
- positive-edge DFF；
- 同步 reset 和 enable，并在 D 端下沉为组合 MUX；
- NOT/AND/OR/XOR/MUX 等受支持的规范化组合单元；
- `iterationDistance=1` 的 sampled-state 反馈；
- 4 相 QCA 时钟、可枚举 II；
- phase-blind 几何 P&R 后统一求时钟；
- 基于每条有序 coarse route tile 序列的 phase/epoch/II 闭合，最多连续
  4 个同相 tile；所有 mapped QCA cells 从所属 tile 继承相位；
- 结构 Mapping 和 crossover DRC；
- 原生门级 LaTeX 输出。

当前明确不支持：

- level-sensitive latch；
- asynchronous reset；
- negedge register；
- 多时钟/CDC、gated/generated clock；
- memory、tristate 和未识别单元；
- setup/hold window、可变宏延迟和 PVT；
- 已表征的物理 QCA DFF/latch 状态宏；
- 一般组合 SCC 或未注解的原生 stateful SCC。

除下面注明的缺口外，不支持结构应 fail closed，不能通过语义转换静默接受。
当前 importer 可能为多个时钟输入建立多个 domain，而后端仍按单一全局 II
求解；因此在补上前端检查前，调用端必须显式拒绝多时钟输入。

## 6. 构建和测试

### 6.1 构建

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DIFCN_BUILD_TESTS=ON

cmake --build build-release -j4 --target \
  ifcn_sequential_pnr \
  ifcn_paper_cyclic_pnr \
  ifcn_sequential_clock_experiment \
  ifcn_mapping_metrics \
  ifcn_energy_analysis \
  ifcn_physical_benchmark \
  mapping_crossover_tests \
  sequential_global_phase_solver_tests \
  sequential_ir_tests \
  physical_state_macro_tests
```

### 6.2 回归

```bash
ctest --test-dir build-release --output-on-failure \
  -R '^(mapping_crossover_regression|sequential_.*|physical_state_macro_regression|cell_level_io_contraction_regression)$'

python3 tests/test_yosys_json_to_seqir.py
python3 tests/test_validate_sequential_paper_benchmark.py
python3 tests/test_solve_global_clock_z3.py
```

2026-08-24 的结果：

- CTest：11/11；
- 上述三个 Python 测试：27/27；
- 最终聚合器：5/5；
- 本次最终验证批次中的 LaTeX 表及版图均通过 `pdflatex`。

2026-08-27 修正相位粒度、加入 Sequential Mapping 和端口连通 DRC 后的
最终回归：

- CTest（Mapping、metadata、global tile solver、Z3、RTL/IR、toggle
  P&R/Mapping/Simon/waveform、cyclic compaction、rank-0 external-Z3 紧致
  面积链和 cell-level I/O）：19/19；
- GCN dataset/offline/retrieval/protocol/bridge：37/37；
- 5 个最终 RTL artifact：`semantic_failures=0`，全部 Mapping DRC 和
  tile max-run DRC 通过；
- 5 组门级 IFCN/TeX、UI SVG/PDF 和 QCADesigner `.qca` 均重新生成，并完成
  IFCN tile phase 与 QCA clock 的逐元胞继承一致性检查。

## 7. 运行实验

### 7.1 完整 RTL 批次

```bash
python3 scripts/run_sequential_rtl_experiments.py \
  --output-dir build/artifacts/sequential_layout_compact_final_20260825 \
  --timeout-seconds 180 \
  --z3-timeout-ms 60000 \
  --ii 4,8,12,16,20,24,28,32 \
  --max-same-phase 4 \
  --max-dfs-nodes 5000000 \
  --spacing 2 \
  --route-search-cost 80 \
  --compaction-max-states 256 \
  --compaction-seeds 16 \
  --max-geometry-ranks 64 \
  --geometry-ladder-seconds 180 \
  --design toggle_ff \
  --design enable_hold_ff \
  --design johnson2_sync \
  --design johnson4_free_running \
  --design reconvergent_feedback_ff
```

器件级 QCA/SVG/PDF 需在上述 IFCN 成功后重新导出；不能复用旧
`qca_cell` 批次：

```bash
for design in toggle_ff enable_hold_ff johnson2_sync \
              johnson4_free_running reconvergent_feedback_ff; do
  dir="build/artifacts/sequential_layout_compact_final_20260825/${design}/cyclic_z3_adaptive"
  build-release/ifcn_energy_analysis \
    "$dir/layout.ifcn" "$dir/layout_cell_level" --qca-only
  QT_QPA_PLATFORM=offscreen IFCN_NONINTERACTIVE=1 \
    IFCN_AUTO_MAP_FILE="$dir/layout.ifcn" \
    IFCN_AUTO_EXPORT_CELL_LAYOUT="$dir/layout_cell_level.svg" \
    build-release/fcnx_gui
  QT_QPA_PLATFORM=offscreen IFCN_NONINTERACTIVE=1 \
    IFCN_AUTO_MAP_FILE="$dir/layout.ifcn" \
    IFCN_AUTO_EXPORT_CELL_LAYOUT="$dir/layout_cell_level.pdf" \
    build-release/fcnx_gui
done
```

默认依赖：

- `build/tools/yosys-local/usr/bin/yosys`
- `build/tools/z3-local`
- `build-release/ifcn_sequential_pnr`
- `build-release/ifcn_paper_cyclic_pnr`

当前正式实验实际使用 Yosys 0.33 和 Z3 4.8.12。可通过 `--yosys` 和
`--z3-root` 指定其他本地安装。没有使用 Docker 镜像；外部工具均按源码或
本地软件包方式加载。

这些工具目录和全部 artifact 都被 Git 忽略，干净 clone 不会自动包含它们，
目前也没有统一 provisioning 脚本。接手者需要自行安装 Yosys/Z3，或单独归档
`build/tools/yosys-local`、`build/tools/z3-local`。runner 会为当前本地 Yosys
设置所需的 `LD_LIBRARY_PATH` 和 `YOSYS_DATDIR`。

### 7.2 时钟正确性和 II 消融

```bash
taskset -c 23 python3 scripts/run_sequential_clock_experiments.py \
  --build-dir build-release \
  --output-dir build/artifacts/sequential_clock_comparison_v4 \
  --repetitions 50 \
  --jobs 4
```

CPU 空闲时再引用绝对运行时间；SAT/UNSAT/LIMIT 分类、oracle 结果和 DFS
节点数不受宿主机并发负载影响。

### 7.3 论文电路重建

```bash
python3 scripts/run_sequential_paper_benchmarks.py \
  --physical-feedback \
  --cyclic-pnr build-release/ifcn_paper_cyclic_pnr \
  --output-dir \
    build/artifacts/sequential_paper_cyclic_benchmarks_release_v2 \
  --timeout-seconds 60
```

### 7.4 汇总

```bash
python3 scripts/aggregate_sequential_experiments.py \
  --output-dir build/artifacts/sequential_master_results_v2
```

## 8. 当前实验结果

### 8.1 时钟模型正确性

- 1040 个 recurrence case；
- exact 模型：258 SAT、782 UNSAT；
- modulo-only 在 exact-UNSAT 中假接受 490 个，即 62.66%；
- modulo-only 假拒绝 0；
- 独立 DP oracle mismatch 0；
- strict-next-phase 子集仍假接受 52/244，即 21.31%。

这说明只给版图分配 0..3 相位，不能证明反馈属于正确的下一次状态迭代。

### 8.2 II 消融

- 260 个固定几何；
- 固定 `II=4`：98 个 SAT；
- 自适应 `{4,8,12,16}`：177 个 SAT；
- 额外恢复 79 个，较固定 II 相对增加 80.61%；
- oracle mismatch 和 LIMIT 均为 0。

### 8.3 RTL 自动流程

9 个 RTL、276 个一步状态向量全部通过，0 mismatch。

adaptive Z3 循环 P&R 在 8 个适用设计中成功 5 个。2026-08-27 纠正为
coarse clock-tile 相位闭合后，紧致布局结果如下（artifact：
`build/artifacts/sequential_layout_compact_final_20260825/`；目录名保留旧日期，
内容已于 2026-08-27 重跑覆盖）：

| 电路 | rank | II | bbox | area | route steps | coarse route sites | mapped unique XY | max same-phase tile run |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `toggle_ff` | 0 | 4 | 2x4 | 8 | 7 | 7 | 40 | 2 |
| `enable_hold_ff` | 0 | 4 | 5x7 | 35 | 32 | 29 | 167 | 3 |
| `johnson2_sync` | 0 | 4 | 3x6 | 18 | 20 | 17 | 99 | 2 |
| `johnson4_free_running` | 27 | 4 | 5x3 | 15 | 16 | 15 | 78 | 1 |
| `reconvergent_feedback_ff` | 0 | 8 | 10x13 | 130 | 112 | 93 | 525 | 4 |

相对此前 seam-only SAT 结果，bbox area 分别为 `8->8`、`45->35`、
`24->18`、`16->15`、`140->130`；总和由 233 降至 206（-11.59%）。全部保持
一步状态检查、循环 DRC、Mapping/crossover DRC、外部 Z3、C++ 独立 epoch
replay，以及 ordered-route-tile max-run DRC 通过。`johnson4_free_running`
的 rank 0--26 在正确 tile 时钟约束下均 UNSAT，首个 SAT 是 rank 27；它需要
真实 coarse-tile detour 来容纳四条 distance=1 recurrence，不能再依靠一个
tile 内给不同 QCA 元胞分配不同相位伪造延迟。`reconvergent_feedback_ff`
的可行 II 恢复为 8。
runner 的 IFCN fallback bbox parser 也已修正，不会再把路径头
`(sourceId,sinkId)` 当成版图坐标。

上述 5 个最终 `cyclic_z3_adaptive/layout.ifcn` 也已复用 UI 的
`Save cell-level layout` 映射/导出路径，分别生成同目录下的
`layout_cell_level.svg` 和单页裁剪 `layout_cell_level.pdf`；另通过正式
QCADesigner 2.0 writer 生成 `layout_cell_level_energy_input.qca`。5 个 QCA
文件逐元胞核对了
`cell_options.clock == tile_phase[(physical_x/5,physical_y/5)]`，并确认每个
tile（含所有 layer）只有一个相位；SVG/PDF 也均为有效非空导出。这是标准门、
线路和 crossover 的结构性 QCA cell-level 映射；它不替代状态器件的动态
功能表征。当前 UI `Save cell-level layout` 导出的是可见 L0 投影：高层元胞
在 `.qca` 中完整存在，但没有各自独立的 SVG/PDF 图元；
因此多层器件 signoff 应查看 `.qca`，不能把单张 SVG 当成完整三层清单。
当前 5 个 IFCN 均显式写出 `#mapping mode: sequential`，49 条 route 均有
逐路由 distance directive，其中 10 条为正距离反馈；它们已通过新的有序
waypoint/transition Mapping 检查后重新导出。对应 layer-aware QCA record 数
依次为 40、177、99、83、566。器件元胞不再作为时钟 occurrence：同一 tile
中的 L0/L1/L2 及所有 XY 均继承同一 tile phase。`johnson2_sync` 曾因 `d0`
同时是状态更新
sink 和反馈 source，而旧 output 模板只生成 fanin 端口，导致右下反馈线缺少
两个元胞。当前 Sequential 双向终端模板已补齐 `(32,43)`、`(32,44)`；正式
QCA 的 center-to-center 结构连通检查中，`d0 -> d1` 为连续 20-edge 物理
路径；Mapping 的 ordered interconnect trace 是 `(32,44) -> (22,44)`，共
17 cells/16 edges，且全程四邻接，但相位签核仍按它覆盖的 coarse tiles 计算。

其余状态：

- `counter2_sync`：Z3 `UNKNOWN`；
- `shift_register4`、`lfsr4`：`routing_failed`；
- `dff_sync`：Q 只用于观察、没有反馈 fanout，当前模型报
  `unsupported_observation_only_state`。

旧 raw report 的 `mapped_qca_cells` 实际是 layer-collapsed unique XY site。
最终 master 表已经改名为 `mapped_unique_xy_sites`；不能将它与导出的
layer-aware QCA cell 数直接比较。

### 8.4 论文电路与物理诊断

- 7/7 个论文重建符合预期；
- 4 个 sampled-state adapter 完成循环 P&R；
- 3 个忠实 level-sensitive latch 被安全拒绝；
- 4 个循环版图均完成结构 Mapping/export；
- 多周期 recurrence 物理诊断为 0/4 通过；
- 所有物理状态仍标记 `physical_state_signoff=false/not_characterized`。

五电路 `tile_phase_drc=5/5` 只证明 ordered coarse-route clock-zone 合同；
不改变上述状态器件动态诊断仍为 0/4，也不意味着 QCA 状态宏已经完成物理
capture/hold 表征。

能耗时间步尚未收敛，因此只能作为探索性附录，不能用于声称功耗优于近期
论文或其他 P&R 方法。

## 9. 最终 artifact

以下是清理后保留的最终结果目录：

- `build/artifacts/sequential_layout_compact_final_20260825/`（目录名保留旧日期，
  内容为 2026-08-27 的 corrected tile-phase 重跑结果）
- `build/artifacts/sequential_master_results_v2/`
- `build/artifacts/sequential_clock_comparison_v4/`
- `build/artifacts/sequential_rtl_experiments_z3_v3/`
- `build/artifacts/z3_audit_final/`
- `build/artifacts/sequential_paper_cyclic_benchmarks_release_v2/`
- `build/artifacts/sequential_cyclic_physical_analysis_v2/`
- `build/artifacts/sequential_cyclic_energy_convergence_v2/`
- `build/artifacts/sequential_cyclic_simon_models_v2/`
- `build/artifacts/external_baselines/`

主要入口：

- 总结果：`build/artifacts/sequential_master_results_v2/summary.json`
- 全指标：`build/artifacts/sequential_master_results_v2/summary.csv`
- LaTeX 总表：
  `build/artifacts/sequential_master_results_v2/tables/all_tables.tex`
- 编译检查：
  `build/artifacts/sequential_master_results_v2/latex_check/all_tables_check.pdf`
- RTL 总结：
  `build/artifacts/sequential_rtl_experiments_z3_v3/summary.json`
- 复杂示例：
  `build/artifacts/sequential_layout_compact_final_20260825/reconvergent_feedback_ff/cyclic_z3_adaptive/layout.tex`
- 对应器件图：
  `build/artifacts/sequential_layout_compact_final_20260825/reconvergent_feedback_ff/cyclic_z3_adaptive/layout_cell_level.svg`

整个 `build/` 被 Git 忽略。若需要跨机器交接，必须归档这些结果或按照本文件
命令重跑，不能假定新的 Git clone 会包含 artifact。

## 10. 外部对比的正确口径

- Walter/fiction `determine_clocking` 是固定组合布局上的 modulo clock-number
  assignment，不含寄存器边界、absolute epoch、iteration distance 或 II。
- fiction GOLD 是组合 2DDWave P&R。
- 它们只能作为组合几何/时钟分配背景，不能作为时序 head-to-head。
- Bhowmik、Deng 的电路可按公开拓扑重建，但没有同输入可执行 artifact；只做
  功能/结构重建，不计算跨平台面积、运行时间或功耗倍率。

本机已复现：

- Walter/fiction：390/390 equivalent，论文 Table-I 几何 39/39；
- GOLD 小集合：40/40 PASS、STRONG equivalence。

## 11. 已知问题与下一步

### P0：使算法成为真正完整的时序 P&R

1. **实现 phase failure 驱动的几何修复。**
   当前全局时钟 UNSAT/LIMIT 后不会自动插入 dogleg、局部拆共享或移动门。
2. **把整轮重试替换成 selective rip-up/reroute。**
   需要 present/history congestion、victim net 和局部窗口事务回滚。
3. **继续升级反馈感知放置。**
   当前已有 Q 过滤、反馈 barycenter 和 adjacent-swap；下一步需加入反馈
   corridor、重汇合长度和可实现 II 下界，并做 gate move + 局部重布线。
4. **接入经过物理表征的状态宏。**
   必须定义 footprint、D/Q pin、internal phase latency、capture/hold 合同；否则
   无法把结构反馈升级为物理时序签核。
5. **表征 gate/state macro 的 tile-level 时序合同。**
   当前每个 macro 所属 tile 是一个 clock zone，tile 内全部元胞同相。若目标
   PDK 需要更细的内部传播模型，应先改变物理 tile/macro 抽象并完成表征，不能
   在现有 5x5 tile 内私自给元胞切分相位。
6. **解决当前失败样例。**
   优先让 `shift_register4`、`lfsr4` 和 `counter2_sync` 稳定闭合。

### P1：收紧软件架构和复现

1. 让 C++ P&R 直接消费 SeqIR 和稳定 EdgeId，移除 legacy Parse 接缝。
2. 支持无反馈但 Q 可观察的普通寄存器。
3. 为 LIMIT/UNKNOWN 定义继续尝试更大 II 的可配置策略。
4. 固定并升级 Yosys 版本；当前正式数据仍是 0.33。
5. 在 importer/driver 中强制单时钟合同，未实现多时钟域求解前拒绝多 clock。
6. 增加 ISCAS'89 小型时序实例和更大参数化 counter/shift/LFSR。
7. 随机算法使用预注册 seeds；确定性 solver 重复运行只能统计时间，不能冒充
   多 seed 成功率实验。
8. **保持论文资产和正式 artifact 同步。** `paper-sequential-mej/main.tex`、
   Fig. 7、Fig. 14 及其嵌入的器件图已于 2026-08-27 同步到 corrected
   tile-phase 结果；以后重跑布局时必须一起更新，不能重新引入 Johnson4
   rank0/area8、reconvergent II32 或 tile 内分裂元胞相位的旧批次。

## 12. 清理记录与保护边界

2026-08-24 已清理：

- 根目录 `seed_1`..`seed_13`；
- `include/gcn_rl_layout/seed_1`..`seed_5`；
- benchmark 输出中的 `train_seed_*` 临时目录；
- 共 45 个经清理前审计确认已被最终版本替代的 smoke、probe、旧实验和诊断 artifact；
- 误生成的 `--help/`、空目录和源码缓存；
- 时序实验中重复的 Yosys/Z3 下载缓存及其临时虚拟环境；
- 两份被本文件取代的旧 V0 时序说明。

删除通过系统回收站完成。未跟踪生成物可从回收站恢复；原来已被 Git 跟踪的
LaTeX 缓存也可从版本历史恢复。

以下内容特意没有清理：

- `include/gcn_rl_layout/results/` 下的历史 RL 训练/评估数据；
- benchmark 中有来源意义的 seed fixture；
- 已 staged 的组合电路、GCN-RL、UI 和其他并行工作；
- 9 个最终时序实验目录；
- `build/tools/yosys-local`、`build/tools/z3-local` 和外部 fiction 源码。

不要使用无范围的 `git clean`，也不要为了“整理”本项目而回滚其他工作树修改。
