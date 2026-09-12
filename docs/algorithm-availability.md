# 算法可用性与验证 / Algorithm availability and validation

**2026-09-13** · [简体中文 README](../README.md) · [English README](../README.en.md)

当前不规则时钟布局只有一个公开入口：**`irregular` / Irregular-Clock Graph P&R**。
布局完成、DRC 通过和物理输出正确是三个不同结论。37 路径主轮已完成：31 个路径找到
通过物理检查的候选；独立有限 D4 补充后有 35 个路径具备通过证据，仍有 2 个未通过。

There is one public irregular-clock layout entry: **`irregular` / Irregular-Clock Graph P&R**.
Completed routing, passing DRC and correct physical outputs are separate results.
The 37-path main run is complete: 31 paths have a physically passing candidate. A separate
finite D4 supplement raises recorded coverage to 35 paths; two still have no passing device.

## 当前入口 / Current entry points

| 流程 / Flow | 入口 / Entry | 行为 / Behavior |
| --- | --- | --- |
| 不规则时钟 / Irregular clock | CLI `ifcn_combinational_pnr irregular`; GUI **Irregular-Clock Graph P&R**; workflow `irregular` | 同一共享搜索流程，内部评估摆放与布线种子，并进行压缩；按实际占用时钟块面积、实际 QCA 元胞数、路线长度依次择优。 / One shared search evaluates internal placement/routing seeds and compaction, ordered by occupied tile area, actual QCA cells, then route length. |
| 规则时钟启发式 / Regular-clock heuristic | GUI **Heuristic P&R**: `USE`, `RES`, `2DDWave` | 遗传摆放与 A* 布线；时钟选择传给对应后端，2DDWave 使用 TDD 方案。 / Genetic placement and A* routing with the selected clock backend; 2DDWave selects TDD. |
| 固定 2DDWave 图绘制 / Fixed 2DDWave graph layout | GUI **2DDWave Fixed-Clock P&R**; workflow `normal_2ddwave` | 经典 Python 图绘制、布线与压缩，保持固定 2DDWave 时钟模板。 / Classical Python graph placement, routing and compaction under the fixed 2DDWave template. |
| 寄存器切分 / Register cut | `ifcn_sequential_pnr` | 对 cut 网表与状态边界求解时钟约束。 / Clock closure for a cut netlist with explicit state boundaries. |
| 物理反馈 / Cyclic feedback | `ifcn_paper_cyclic_pnr` | 恢复反馈连线，联合处理 phase、epoch、II 和跨周期距离。 / Restore feedback routing and solve phase, epoch, II and iteration-distance constraints. |

旧的 Compact/June 版本名称不再是独立的公开算法选项。压缩是共享流程的一部分；
机器学习算法和外部智能体集成已移除。

The former Compact/June version names are no longer separate public algorithm choices.
Compaction belongs to the shared flow. Machine-learning algorithms and external agent
integrations have been removed.

启发式新增全局时钟与器件 DRC 验收后，三个固定时钟方案各实际运行一次 AND2：
使用 8×8 网格、32 代、64 个体，保持原时钟模板，结果如下。

After adding global-clock and device-DRC acceptance to the heuristic, one real AND2 search
was run for each fixed-clock scheme on an 8×8 grid with 32 generations and population 64.
All retained their original clock patterns.

| 时钟 / Clock | 占用时钟块 / Occupied tiles | QCA cells | Bistable 真值 / Truth |
| --- | ---: | ---: | --- |
| USE | 4×2 = 8 | 25 | 通过 / Pass |
| RES | 3×4 = 12 | 35 | 通过 / Pass |
| 2DDWave | 2×2 = 4 | 15 | 通过 / Pass |

每例均通过独立 DRC、4/4 源逻辑向量及 1,560 个输出 hold 样本检查：全部输入组合重复
两遍，每组合保持 8 周期、每周期 128 样本；基线／加速差为零，无迭代超限。
这是各方案一次随机运行的成功证据，不能保证其他电路或随机种子均成功。
之后已对三种启发式方案和固定图绘制分别运行全部 23 个 TOY 输入，合计 92 项。
最新逐例结果、失败原因及保存的版图见[示例验证表](examples.md)。

Each case passes independent DRC, all four source-logic vectors and 1,560 output hold checks:
all combinations are repeated twice, with eight cycles per vector and 128 samples per cycle.
Baseline/accelerated differences are zero and every sample converges. These are single
successful random runs, not guarantees for other circuits or seeds. Each heuristic scheme
and the fixed graph flow have subsequently run all 23 TOY inputs, totaling 92 jobs.
See the [example validation table](examples.md) for current results, failures and saved layouts.

## 验证标准 / Acceptance criteria

1. **源逻辑 / Source logic**：将原始 Verilog 的独立真值与实际路由 DAG 比较，保留每个输入、
   输出的名字与身份。穷举和抽样结果分别记录。 / Compare an independent original-Verilog
   oracle with the routed DAG, preserving every named input and output. Exhaustive and sampled
   coverage are reported separately.
2. **结构与时钟 / Structure and clocks**：检查边覆盖、端点、不同 fanin 的独立门端口、
   实际分层元胞连通性、跨源所有权、QCA 引脚，以及全局 epoch 与门前后相位边界。
   / Check complete routes, endpoints, distinct fanin ports, actual layered-cell connectivity,
   source ownership, QCA terminals, global epochs and gate clock boundaries.
3. **物理输出 / Physical outputs**：使用实际导出的 QCA 器件运行 Bistable；标准分辨率为
   **每周期 128 个样本**。小型回归对每个输入向量保持 8 周期、全部组合重复两遍，检查
   最后 3 周期的输出 hold 阶段：符号符合源真值且 **|P| ≥ 0.5**，并逐点核对输入。
   / Simulate the exported QCA device at **128 samples per clock cycle**. Small regressions
   hold each vector for 8 cycles and repeat all combinations twice. In the final 3 cycles,
   output hold samples must match source truth with **|P| ≥ 0.5**; actual inputs are also checked.

较大网表按记录的传播 epoch 延长保持时间，失败案例另做更长保持对照。
同时要求仿真没有迭代超限，并核对基线与加速实现的输出一致性。两个实现结果相同，
仍可能同时算出错误逻辑；DRC 通过也不能替代器件真值检查。

Larger circuits use documented hold durations based on propagation epochs, with longer-hold
controls for counterexamples. Checks also require convergence and agreement between baseline
and accelerated engines. Two engines can agree on an incorrect logical result, and DRC does
not replace device truth-table testing.

### 门模板与接口修复 / Gate templates and interface fixes

共享标量解析器按 `~`、`&`、`^`、`|` 的优先级解析，严格识别单表达式及跨连续赋值的
三文字两两乘积多数门；普通括号不会被误判为 MAJ。输出别名、供下游使用的输出和同信号的
多个输出均保留身份。映射修复也保留带 fanout 的输出端口和叶节点多数门输出；压缩相位编码
与展开相位编码的 QCA 导出保持一致。

The shared scalar parser respects `~`, `&`, `^`, `|` precedence and recognizes exact majority
sum-of-products within and across continuous assignments. Parentheses alone never imply MAJ.
Output aliases, outputs feeding later gates and multiple observed outputs retain distinct
identities. Mapping preserves output fanout and leaf-majority terminals; packed and expanded
clock encodings produce consistent QCA exports.

**40 个独立门方向案例已通过物理回归**：NOT 12、AND 12、OR 12、MAJ 4，覆盖不同的
东、南、西、北输入／输出端口组合，共检查 59,280 个 hold 样本，无错误或不确定输出。
该结果验证孤立门模板，不能推广为含交叉、共享 fanout 或相邻门相互作用的整电路已通过。

**All 40 isolated gate orientations pass physical regression**: 12 NOT, 12 AND, 12 OR and
4 MAJ cases cover the cardinal input/output port combinations. All 59,280 checked hold samples
are correct and determinate. This validates isolated templates, not complete circuits with
crossings, shared fanout or interacting neighboring gates.

### 当前全量基准 / Current full-suite measurement

主轮使用统一 `irregular` 入口，对 37 个 TOY/MAJ 文件路径运行布局及候选物理筛选。
重复内容文件保留独立路径，统计不把主轮、历史复验与 D4 补充混在同一次运行中。
完整逐例数据和哈希见[不规则基准](irregular-benchmarks.md)及其 CSV。

The main run uses the unified `irregular` entry for 37 TOY/MAJ paths, retaining paths with
identical contents. Main-run selection, historical remapping and D4 supplements remain
separate records. See [irregular benchmarks](irregular-benchmarks.md) and its CSV for per-case
data and hashes.

| 检查 / Check | 主轮结果 / Main-run result |
| --- | --- |
| 原始 TOY/MAJ 文件 / Original TOY/MAJ files | 37 路径：TOY 23 + MAJ 14 / 37 paths: TOY 23 + MAJ 14 |
| 原始源到路由 DAG / Original source to routed DAG | 37/37 等价，共 7,240 个穷举向量 / 37/37 equivalent, 7,240 exhaustive vectors |
| 路由、全局时钟、实际映射及接口 / Routing, global clocks, actual mapping and interfaces | 37/37 通过 / 37/37 pass |
| 默认几何最优输出的物理状态 / Default geometric winner, physical status | 18 通过、17 失败、2 未测 / 18 pass, 17 fail, 2 unchecked |
| 候选池物理筛选 / Candidate-pool physical selection | 31/37 通过：26 穷举、5 抽样；6 未找到通过候选 / 31/37 pass: 26 exhaustive, 5 sampled; 6 without a pass |

默认 CLI 只按面积、实际元胞数和线长择优，不自动执行物理真值筛选。
37/37 结构通过不能写成 37/37 器件功能通过；另一个候选通过也不会改变默认器件的失败状态。

The default CLI ranks area, actual cells and route length without physical truth selection.
Structural 37/37 is not functional 37/37. A different passing candidate does not change the
failed status of the default device.

**有限 D4 独立补充**只变换已有候选的平面方向，保持拓扑、I/O 和相位序列，重新映射、
检查并运行相同的 128 样本／周期物理协议。新增结果如下；主轮 31/37 计数保持不变。

The **separate finite D4 audit** transforms existing planar layouts while preserving topology,
I/O and phase sequences, then remaps and tests them at the same 128 samples/cycle.
The following supplemental results do not change the main-run 31/37 count.

| 补充案例 / Supplemental case | 结果 / Result |
| --- | --- |
| `TOY/mux41.v` 及同内容生成文件 / and its byte-identical generated source | 176 时钟块 / 596 元胞；全部 64 输入组合通过，24,960 hold 点；两路径复用同一证据 / 176 tiles / 596 cells; all 64 vectors and 24,960 hold samples pass; one evidence record shared by two paths |
| `TOY/xor5R.v` | X 坐标镜像，972 / 2318；全部 32 组合、12,480 hold 点通过 / Reflect X coordinate, 972 / 2318; all 32 vectors and 12,480 hold samples pass |
| `MAJ/par_check.v` | 旋转 180°，180 / 681；全部 16 组合、6,240 hold 点通过 / Rotate 180°, 180 / 681; all 16 vectors and 6,240 hold samples pass |
| `TOY/RCA2.v` | 两候选的 14 个新方向均在完整 32 表下失败 / All 14 new directions of two candidates fail the complete 32-vector check |
| `MAJ/mux41.v` | 两候选的 14 个新方向均未通过 32/64 向量筛选，未追加完整表 / All 14 new directions fail the 32/64-vector screen; no full-table follow-up |

因此，合并明确记录的补充证据后有 **35/37 路径**具备物理通过器件；仍未通过的是
**TOY RCA2 与 MAJ MUX41**。这是限定候选和慢输入协议内的结果，不证明失败电路无可行布局。
旋转／镜像可能改变实际精细几何；不同方向的合法性与物理行为必须分别验证。

Recorded evidence therefore covers **35/37 paths**, with **TOY RCA2 and MAJ MUX41** still
unsuccessful. This is a bounded-candidate, slow-input result, not proof that these circuits
have no feasible layout. Remapping a symmetry may change fine geometry; each direction
requires fresh geometric and physical validation.

此外，历史面积优于新池的 **6 个路径**均已通过当前 Release 映射器重新导出与实际
Bistable 复验：**6/6、46,800 个 hold 点全部正确**。这些已复验历史副本保留独立来源，
不会被表述为当前默认 CLI 自动生成的结果，也不据历史不同构建的耗时比较算法速度。

The **six paths with smaller historical devices** were also re-exported using the current
Release mapper and freshly simulated: **6/6 pass, with all 46,800 hold samples correct**.
These historical copies retain separate provenance; they are not presented as current default
CLI outputs, and timing across different historical builds is not used to rank speed.

## 组合与时序模式 / Combinational and sequential modes

两个时序入口显式使用 `PreserveSequential` 解析选项，并以 `MappingMode::Sequential`
映射。组合流程可以将输出 NOT 直接命名为输出节点；时序流程保留 `q → NOT → d` 的
独立 D 边界，恢复反馈后为 `d → NOT → d`，避免融合成不可布线的 `d → d` 自环。
组合图的 epoch 约束不套用于带 iteration distance 的反馈图。

Both sequential entries explicitly select `PreserveSequential` parsing and
`MappingMode::Sequential`. Combinational output NOT fusion remains enabled, while sequential
parsing preserves the separate D event in `q → NOT → d`. Restoring feedback then gives
`d → NOT → d`, rather than an unroutable `d → d` self-loop. Combinational epoch assumptions are
not applied to feedback graphs with iteration distances.

cut、反馈布线、全局 phase/epoch/II 约束及输出边界由各自回归检查。结构通过不等于
完整触发器的复位、保持或多周期状态行为完成表征；相关报告保留
`physical_state_signoff=not_characterized`。详见[时序设计](sequential-design.md)。

Separate regressions cover cut graphs, feedback routing, global phase/epoch/II constraints
and output boundaries. Structural success does not characterize complete flip-flop reset,
retention or multi-cycle state behavior; reports retain
`physical_state_signoff=not_characterized`. See [sequential design](sequential-design.md).

## 仿真与能量 / Simulation and energy

README 的固定 2DDWave XOR 案例为 3×4 时钟块、79 个元胞，通过 1,560 个 Bistable hold
样本及 574 个 Coherence RK4 hold 样本检查。两个模型的基线／加速输出差均为零。
波形和截图来源见[示例图片说明](images/xor2/README.md)。能量分析另输出逐周期数值与残差；
能运行和逻辑正确均不能代替能量模型的数值收敛验证。

The fixed-2DDWave README XOR uses 3×4 clock tiles and 79 cells. It passes 1,560 Bistable
and 574 Coherence RK4 hold-sample checks, with zero baseline/accelerated output differences.
See the [example image record](images/xor2/README.md) for waveform and screenshot provenance.
Energy analysis separately reports per-cycle values and residuals; executable or logical
success does not establish numerical convergence of the energy model.

## 复现 / Reproduce

按 README 构建后运行。下列命令使用当前公开入口；物理检查会在不满足条件时返回失败。
Build as described in the README. These commands use the current public interface and report
failure when the physical acceptance criteria are not met.

```bash
mkdir -p output/algorithm-check
./build/ifcn_combinational_pnr irregular tests/benchmarks_f/TOY/xnor2.v \
  output/algorithm-check/xnor2.json --budget 120 --attempts 320

ctest --test-dir build \
  -R 'gate_template_physical_regression|combinational_physical_' --output-on-failure

python3 tests/test_combinational_physical_waveform.py \
  --case xnor2 --pnr-binary build/ifcn_combinational_pnr \
  --energy-binary build/ifcn_energy_analysis \
  --simulation-binary build/ifcn_physical_benchmark \
  --output-dir output/xnor2-physical-check
```

物理回归案例为 `xor2`、`xnor2`、`mux21`、`majority`。`--output-dir` 须为空目录。
The physical regression cases are `xor2`, `xnor2`, `mux21`, and `majority`.
Use an empty `--output-dir` to retain generated artifacts.

```bash
python3 tests/test_gate_template_physical.py \
  --energy-binary build/ifcn_energy_analysis \
  --simulation-binary build/ifcn_physical_benchmark \
  --output-dir output/gate-physical-check

mkdir -p output/sequential-check
./build/ifcn_sequential_pnr tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v \
  output/sequential-check/toggle_cut.ifcn --state d:q --ii 4,8,12,16 --spacing 5
./build/ifcn_paper_cyclic_pnr tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v \
  output/sequential-check/toggle_cyclic.ifcn --state d:q --ii 4,8 --spacing 2 \
  --route-search-cost 80 --compaction-max-states 256 --compaction-seeds 16

ctest --test-dir build \
  -R 'verilog_expression_semantics_regression|sequential_pnr_toggle_regression|sequential_cyclic_feedback_regression' \
  --output-on-failure
```

整体构建与验证记录见[运行验证](validation.md)。实验中生成的 IFCN/QCA、输入向量和波形
不作为测试的预置依赖。

See [validation](validation.md) for build and repository-wide checks. Generated IFCN/QCA
layouts, vectors and waveforms are experiment artifacts, not pre-existing test dependencies.
