# 电路示例与验证 / Circuit examples and validation

**2026-09-13** · [简体中文](../README.md) · [English](../README.en.md) · [完整 CSV / Full CSV](examples.csv)

示例目录只保存实际导出的 `.ifcn`。下表覆盖每个原始输入路径，内容相同的副本也逐项保留；搜索失败且没有合格历史候选的条目没有电路文件。
Only exported `.ifcn` circuits are stored in examples. Every source path is recorded, including duplicates. A failed search without a validated historical candidate has no fabricated output file.

面积为占用时钟块的外接矩形，单元数为实际映射 QCA 数量。布局、时钟、布尔逻辑与物理波形分别记账；`__drc_only` 是已知物理失败的检查案例。
Area is the occupied clock-tile bounding box; cell counts are actual mapped QCA cells. Layout, clocks, Boolean logic and physical waveforms are recorded separately. `__drc_only` marks a known physical failure.

| 算法 / Family | 本轮案例 / Runs | 本轮布局通过 / Routed | 本轮搜索失败 / Failed | 保存文件 / Saved | 物理 E / S / F / 未测 |
| --- | ---: | ---: | ---: | ---: | --- |
| 规则启发式 / Heuristic · USE | 23 | 7 | 16 | 7 | 0 / 0 / 0 / 7 |
| 规则启发式 / Heuristic · RES | 23 | 9 | 14 | 9 | 0 / 0 / 0 / 9 |
| 规则启发式 / Heuristic · 2DDWave | 23 | 13 | 10 | 13 | 0 / 0 / 0 / 13 |
| 固定图绘制 / Fixed graph · 2DDWave | 23 | 20 | 3 | 22 | 1 / 2 / 3 / 16 |
| 不规则图绘制 / Irregular graph | 37 | 37 | 0 | 37 | 30 / 5 / 2 / 0 |
| 寄存器切分 / Register cut | 1 | 1 | 0 | 1 | 0 / 0 / 0 / 1 |
| 反馈时序 / Cyclic feedback | 1 | 1 | 0 | 1 | 0 / 0 / 0 / 1 |

**E**：完整真值表通过；**S**：明确向量抽样通过；**F**：物理真值失败；**—**：未验证。**H**：本轮搜索失败后采用单独复验的同源、同方案历史候选。通过结果只随源文件与 QCA 哈希匹配复用。
**E** = exhaustive physical pass; **S** = sampled pass; **F** = physical failure; **—** = untested. **H** marks a separately revalidated historical candidate after the current search failed. Evidence is reused only with matching source and QCA hashes.

表中 E/S/F 对应慢输入 Bistable 检查：每周期 128 样本、序列重复两遍，检查每个输入块最后三周期的输出保持区，极化符号正确且绝对值至少 0.5。具体保持周期和向量数量列在 CSV；README 的 XOR 另有 Coherence 验证。
E/S/F refer to slow-input Bistable checks at 128 samples per cycle, two passes, and the final three output-hold windows per block, requiring the correct sign and absolute polarization ≥ 0.5. Per-case hold duration and vector counts are in the CSV; the README XOR also has Coherence validation.

CSV 保留本轮 43 次搜索失败；其中两条固定 MUX41 路径另有已复验的历史文件，41 个源×算法组合仍无文件。RCA2 的两次额外有界种子尝试也均失败，CSV 单独保留，因此共 133 条记录对应 90 个实际电路文件。
The CSV retains all 43 failed main searches. Two fixed-MUX41 paths have separately revalidated historical files; 41 source/algorithm combinations still have no file. Two additional bounded RCA2 seeds also failed and are separate CSV rows: 133 records, 90 actual circuit files.

不规则示例来自当前候选池、六项历史重映射和有限坐标对称补充，只称已检查候选中的最小合格结果。时序切分和反馈示例保留 II=4 的状态边界/反馈路由；状态器件尚未完成物理表征。
Irregular examples combine the current pool, six historical remapping checks and finite geometric symmetries. Minimum-area claims cover only inspected candidates. Sequential examples retain II=4 state boundaries or feedback routes; their state devices remain physically uncharacterized.

<details>
<summary>规则布局：全部 23 个输入 × 4 个入口 / Regular layouts: all 23 inputs × four entries</summary>

单元格为 **面积 / QCA 单元数 · 物理状态**，点击数字打开实际电路。
Cells show **area / QCA cells · physical status**; numbers link to the saved circuit.

| 源输入 / Source | Heuristic USE | Heuristic RES | Heuristic 2DDWave | Fixed 2DDWave |
| --- | --- | --- | --- | --- |
| [1bitAdderAOIG.v](../tests/benchmarks_f/TOY/1bitAdderAOIG.v) | 搜索失败 / Failed | 搜索失败 / Failed | [63/247](../examples/regular_heuristic/2DDWave/TOY/1bitAdderAOIG.ifcn) · — | [42/232](../examples/regular_2ddwave/TOY/1bitAdderAOIG.ifcn) · — |
| [1bitAdderMaj.v](../tests/benchmarks_f/TOY/1bitAdderMaj.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [154/647](../examples/regular_2ddwave/TOY/1bitAdderMaj.ifcn) · — |
| [RCA2.v](../tests/benchmarks_f/TOY/RCA2.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed |
| [b1_r2.v](../tests/benchmarks_f/TOY/b1_r2.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [72/328](../examples/regular_2ddwave/TOY/b1_r2.ifcn) · — |
| [clpl.v](../tests/benchmarks_f/TOY/clpl.v) | 搜索失败 / Failed | 搜索失败 / Failed | [156/525](../examples/regular_heuristic/2DDWave/TOY/clpl.ifcn) · — | [440/1236](../examples/regular_2ddwave/TOY/clpl.ifcn) · S |
| [clpl_source_generate/clpl_source.v](../tests/benchmarks_f/TOY/clpl_source_generate/clpl_source.v) | 搜索失败 / Failed | 搜索失败 / Failed | [156/525](../examples/regular_heuristic/2DDWave/TOY/clpl_source_generate/clpl_source.ifcn) · — | [440/1236](../examples/regular_2ddwave/TOY/clpl_source_generate/clpl_source.ifcn) · S |
| [mux21.v](../tests/benchmarks_f/TOY/mux21.v) | [15/62](../examples/regular_heuristic/USE/TOY/mux21.ifcn) · — | [12/66](../examples/regular_heuristic/RES/TOY/mux21.ifcn) · — | [12/56](../examples/regular_heuristic/2DDWave/TOY/mux21.ifcn) · — | [16/80](../examples/regular_2ddwave/TOY/mux21.ifcn) · — |
| [mux41.v](../tests/benchmarks_f/TOY/mux41.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [143/737](../examples/regular_2ddwave/TOY/mux41.ifcn) · — · H |
| [mux41_source_generate/mux41_source.v](../tests/benchmarks_f/TOY/mux41_source_generate/mux41_source.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [143/737](../examples/regular_2ddwave/TOY/mux41_source_generate/mux41_source.ifcn) · — · H |
| [newtag.v](../tests/benchmarks_f/TOY/newtag.v) | 搜索失败 / Failed | 搜索失败 / Failed | [81/255](../examples/regular_heuristic/2DDWave/TOY/newtag.ifcn) · — | [252/810](../examples/regular_2ddwave/TOY/newtag.ifcn) · — |
| [paper_2ddwave_carry_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_carry_demo.v) | [45/152](../examples/regular_heuristic/USE/TOY/paper_2ddwave_carry_demo.ifcn) · — | [56/195](../examples/regular_heuristic/RES/TOY/paper_2ddwave_carry_demo.ifcn) · — | [30/145](../examples/regular_heuristic/2DDWave/TOY/paper_2ddwave_carry_demo.ifcn) · — | [20/125](../examples/regular_2ddwave/TOY/paper_2ddwave_carry_demo.ifcn) · — |
| [paper_2ddwave_crossing_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_crossing_demo.v) | [15/69](../examples/regular_heuristic/USE/TOY/paper_2ddwave_crossing_demo.ifcn) · — | [12/69](../examples/regular_heuristic/RES/TOY/paper_2ddwave_crossing_demo.ifcn) · — | [20/107](../examples/regular_heuristic/2DDWave/TOY/paper_2ddwave_crossing_demo.ifcn) · — | [16/93](../examples/regular_2ddwave/TOY/paper_2ddwave_crossing_demo.ifcn) · — |
| [paper_2ddwave_xor_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_xor_demo.v) | [9/56](../examples/regular_heuristic/USE/TOY/paper_2ddwave_xor_demo.ifcn) · — | [9/56](../examples/regular_heuristic/RES/TOY/paper_2ddwave_xor_demo.ifcn) · — | [40/155](../examples/regular_heuristic/2DDWave/TOY/paper_2ddwave_xor_demo.ifcn) · — | [12/74](../examples/regular_2ddwave/TOY/paper_2ddwave_xor_demo.ifcn) · — |
| [par_check.v](../tests/benchmarks_f/TOY/par_check.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [90/405](../examples/regular_2ddwave/TOY/par_check__drc_only.ifcn) · F |
| [par_gen.v](../tests/benchmarks_f/TOY/par_gen.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [90/310](../examples/regular_2ddwave/TOY/par_gen__drc_only.ifcn) · F |
| [random_clock_cell_demo.v](../tests/benchmarks_f/TOY/random_clock_cell_demo.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [527/1084](../examples/regular_2ddwave/TOY/random_clock_cell_demo.ifcn) · — |
| [t.v](../tests/benchmarks_f/TOY/t.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [304/721](../examples/regular_2ddwave/TOY/t.ifcn) · — |
| [xnor2.v](../tests/benchmarks_f/TOY/xnor2.v) | 搜索失败 / Failed | [40/140](../examples/regular_heuristic/RES/TOY/xnor2.ifcn) · — | [25/100](../examples/regular_heuristic/2DDWave/TOY/xnor2.ifcn) · — | [15/81](../examples/regular_2ddwave/TOY/xnor2.ifcn) · — |
| [xnor2_source_generate/xnor2_source.v](../tests/benchmarks_f/TOY/xnor2_source_generate/xnor2_source.v) | 搜索失败 / Failed | [40/140](../examples/regular_heuristic/RES/TOY/xnor2_source_generate/xnor2_source.ifcn) · — | [25/100](../examples/regular_heuristic/2DDWave/TOY/xnor2_source_generate/xnor2_source.ifcn) · — | [15/81](../examples/regular_2ddwave/TOY/xnor2_source_generate/xnor2_source.ifcn) · — |
| [xor2.v](../tests/benchmarks_f/TOY/xor2.v) | [9/56](../examples/regular_heuristic/USE/TOY/xor2.ifcn) · — | [9/56](../examples/regular_heuristic/RES/TOY/xor2.ifcn) · — | [40/155](../examples/regular_heuristic/2DDWave/TOY/xor2.ifcn) · — | [12/74](../examples/regular_2ddwave/TOY/xor2.ifcn) · — |
| [xor2_demo.v](../tests/benchmarks_f/TOY/xor2_demo.v) | [9/56](../examples/regular_heuristic/USE/TOY/xor2_demo.ifcn) · — | [9/56](../examples/regular_heuristic/RES/TOY/xor2_demo.ifcn) · — | [15/84](../examples/regular_heuristic/2DDWave/TOY/xor2_demo.ifcn) · — | [12/74](../examples/regular_2ddwave/TOY/xor2_demo.ifcn) · E |
| [xor2_source_generate/xor2_source.v](../tests/benchmarks_f/TOY/xor2_source_generate/xor2_source.v) | [9/56](../examples/regular_heuristic/USE/TOY/xor2_source_generate/xor2_source.ifcn) · — | [9/56](../examples/regular_heuristic/RES/TOY/xor2_source_generate/xor2_source.ifcn) · — | [42/144](../examples/regular_heuristic/2DDWave/TOY/xor2_source_generate/xor2_source.ifcn) · — | [12/74](../examples/regular_2ddwave/TOY/xor2_source_generate/xor2_source.ifcn) · — |
| [xor5R.v](../tests/benchmarks_f/TOY/xor5R.v) | 搜索失败 / Failed | 搜索失败 / Failed | 搜索失败 / Failed | [224/669](../examples/regular_2ddwave/TOY/xor5R__drc_only.ifcn) · F |

</details>

<details>
<summary>不规则布局：全部 37 个 TOY / MAJ 输入 / Irregular layouts: all 37 inputs</summary>

| 源输入 / Source | 实际电路：面积 / 单元数 · 物理状态 | 物理向量 / Vectors |
| --- | --- | --- |
| [MAJ/1bitAdderAOIG.v](../tests/benchmarks_f/MAJ/1bitAdderAOIG.v) | [102/459](../examples/irregular/MAJ/1bitAdderAOIG.ifcn) · E | 8 / 8 |
| [MAJ/1bitAdderMaj.v](../tests/benchmarks_f/MAJ/1bitAdderMaj.v) | [15/64](../examples/irregular/MAJ/1bitAdderMaj.ifcn) · E | 8 / 8 |
| [MAJ/1bitAdderMaj_source_generate/1bitAdderMaj_source.v](../tests/benchmarks_f/MAJ/1bitAdderMaj_source_generate/1bitAdderMaj_source.v) | [15/64](../examples/irregular/MAJ/1bitAdderMaj_source_generate/1bitAdderMaj_source.ifcn) · E | 8 / 8 |
| [MAJ/RCA2.v](../tests/benchmarks_f/MAJ/RCA2.v) | [782/1775](../examples/irregular/MAJ/RCA2.ifcn) · E | 32 / 32 |
| [MAJ/clpl.v](../tests/benchmarks_f/MAJ/clpl.v) | [176/607](../examples/irregular/MAJ/clpl.ifcn) · S | 32 / 2048 |
| [MAJ/mux41.v](../tests/benchmarks_f/MAJ/mux41.v) | [464/1062](../examples/irregular/MAJ/mux41__drc_only.ifcn) · F | 32 / 64 |
| [MAJ/newtag.v](../tests/benchmarks_f/MAJ/newtag.v) | [130/403](../examples/irregular/MAJ/newtag.ifcn) · S | 32 / 256 |
| [MAJ/par_check.v](../tests/benchmarks_f/MAJ/par_check.v) | [180/681](../examples/irregular/MAJ/par_check.ifcn) · E | 16 / 16 |
| [MAJ/par_gen.v](../tests/benchmarks_f/MAJ/par_gen.v) | [55/270](../examples/irregular/MAJ/par_gen.ifcn) · E | 8 / 8 |
| [MAJ/t.v](../tests/benchmarks_f/MAJ/t.v) | [63/254](../examples/irregular/MAJ/t.ifcn) · E | 32 / 32 |
| [MAJ/xnor2.v](../tests/benchmarks_f/MAJ/xnor2.v) | [28/125](../examples/irregular/MAJ/xnor2.ifcn) · E | 4 / 4 |
| [MAJ/xor2.v](../tests/benchmarks_f/MAJ/xor2.v) | [28/121](../examples/irregular/MAJ/xor2.ifcn) · E | 4 / 4 |
| [MAJ/xor5R.v](../tests/benchmarks_f/MAJ/xor5R.v) | [261/961](../examples/irregular/MAJ/xor5R.ifcn) · E | 32 / 32 |
| [MAJ/xor5_r1.v](../tests/benchmarks_f/MAJ/xor5_r1.v) | [261/961](../examples/irregular/MAJ/xor5_r1.ifcn) · E | 32 / 32 |
| [TOY/1bitAdderAOIG.v](../tests/benchmarks_f/TOY/1bitAdderAOIG.v) | [84/420](../examples/irregular/TOY/1bitAdderAOIG.ifcn) · E | 8 / 8 |
| [TOY/1bitAdderMaj.v](../tests/benchmarks_f/TOY/1bitAdderMaj.v) | [40/260](../examples/irregular/TOY/1bitAdderMaj.ifcn) · E | 8 / 8 |
| [TOY/RCA2.v](../tests/benchmarks_f/TOY/RCA2.v) | [912/2156](../examples/irregular/TOY/RCA2__drc_only.ifcn) · F | 32 / 32 |
| [TOY/b1_r2.v](../tests/benchmarks_f/TOY/b1_r2.v) | [170/716](../examples/irregular/TOY/b1_r2.ifcn) · E | 8 / 8 |
| [TOY/clpl.v](../tests/benchmarks_f/TOY/clpl.v) | [208/708](../examples/irregular/TOY/clpl.ifcn) · S | 32 / 2048 |
| [TOY/clpl_source_generate/clpl_source.v](../tests/benchmarks_f/TOY/clpl_source_generate/clpl_source.v) | [208/708](../examples/irregular/TOY/clpl_source_generate/clpl_source.ifcn) · S | 32 / 2048 |
| [TOY/mux21.v](../tests/benchmarks_f/TOY/mux21.v) | [16/71](../examples/irregular/TOY/mux21.ifcn) · E | 8 / 8 |
| [TOY/mux41.v](../tests/benchmarks_f/TOY/mux41.v) | [176/596](../examples/irregular/TOY/mux41.ifcn) · E | 64 / 64 |
| [TOY/mux41_source_generate/mux41_source.v](../tests/benchmarks_f/TOY/mux41_source_generate/mux41_source.v) | [176/596](../examples/irregular/TOY/mux41_source_generate/mux41_source.ifcn) · E | 64 / 64 |
| [TOY/newtag.v](../tests/benchmarks_f/TOY/newtag.v) | [110/447](../examples/irregular/TOY/newtag.ifcn) · S | 32 / 256 |
| [TOY/paper_2ddwave_carry_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_carry_demo.v) | [35/164](../examples/irregular/TOY/paper_2ddwave_carry_demo.ifcn) · E | 8 / 8 |
| [TOY/paper_2ddwave_crossing_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_crossing_demo.v) | [6/28](../examples/irregular/TOY/paper_2ddwave_crossing_demo.ifcn) · E | 8 / 8 |
| [TOY/paper_2ddwave_xor_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_xor_demo.v) | [12/80](../examples/irregular/TOY/paper_2ddwave_xor_demo.ifcn) · E | 4 / 4 |
| [TOY/par_check.v](../tests/benchmarks_f/TOY/par_check.v) | [133/561](../examples/irregular/TOY/par_check.ifcn) · E | 16 / 16 |
| [TOY/par_gen.v](../tests/benchmarks_f/TOY/par_gen.v) | [112/418](../examples/irregular/TOY/par_gen.ifcn) · E | 8 / 8 |
| [TOY/random_clock_cell_demo.v](../tests/benchmarks_f/TOY/random_clock_cell_demo.v) | [48/225](../examples/irregular/TOY/random_clock_cell_demo.ifcn) · E | 16 / 16 |
| [TOY/t.v](../tests/benchmarks_f/TOY/t.v) | [88/355](../examples/irregular/TOY/t.ifcn) · E | 32 / 32 |
| [TOY/xnor2.v](../tests/benchmarks_f/TOY/xnor2.v) | [20/117](../examples/irregular/TOY/xnor2.ifcn) · E | 4 / 4 |
| [TOY/xnor2_source_generate/xnor2_source.v](../tests/benchmarks_f/TOY/xnor2_source_generate/xnor2_source.v) | [20/117](../examples/irregular/TOY/xnor2_source_generate/xnor2_source.ifcn) · E | 4 / 4 |
| [TOY/xor2.v](../tests/benchmarks_f/TOY/xor2.v) | [12/80](../examples/irregular/TOY/xor2.ifcn) · E | 4 / 4 |
| [TOY/xor2_demo.v](../tests/benchmarks_f/TOY/xor2_demo.v) | [12/80](../examples/irregular/TOY/xor2_demo.ifcn) · E | 4 / 4 |
| [TOY/xor2_source_generate/xor2_source.v](../tests/benchmarks_f/TOY/xor2_source_generate/xor2_source.v) | [12/80](../examples/irregular/TOY/xor2_source_generate/xor2_source.ifcn) · E | 4 / 4 |
| [TOY/xor5R.v](../tests/benchmarks_f/TOY/xor5R.v) | [972/2318](../examples/irregular/TOY/xor5R.ifcn) · E | 32 / 32 |

</details>

| 时序示例 / Sequential example | 源网表 / Source | 面积 / QCA cells | 检查范围 / Scope |
| --- | --- | --- | --- |
| [register_cut](../examples/sequential/register_cut/toggle_ff.ifcn) | [toggle_ff_cut.v](../tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v) | 11 / 54 | 时钟与映射通过；状态物理未表征 / Clocks and mapping pass; state physics uncharacterized |
| [cyclic](../examples/sequential/cyclic/toggle_ff.ifcn) | [toggle_ff_cut.v](../tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v) | 4 / 19 | 时钟与映射通过；状态物理未表征 / Clocks and mapping pass; state physics uncharacterized |

## 使用与复现 / Use and reproduce

在 GUI 使用 **File → Open** 打开表中的 `.ifcn`。从 Verilog 重新生成时，分别选择 **Heuristic P&R**、**2DDWave Fixed-Clock P&R** 或 **Irregular-Clock Graph P&R**。规则启发式本轮使用 32 代、64 个体和按电路规模选取的网格；搜索存在随机性，结果可能不同。
Open the linked `.ifcn` with **File → Open**. To regenerate from Verilog, choose the corresponding P&R toolbar entry. The heuristic batch used 32 generations, 64 individuals and a circuit-sized grid; randomized search can produce different results.

规则批量复现使用[规则基准入口](../tests/benchmarks/generate_regular_layouts.py)。固定图绘制也可通过[工作流入口](../src/python/ifcn/workflow.py)运行；不规则批量检查使用[基准入口](../tests/benchmarks/benchmark_irregular_layout.py)。时序命令和物理检查见[算法说明](algorithm-availability.md)，构建与 71 项回归见[运行验证](validation.md)。
Use the [regular benchmark runner](../tests/benchmarks/generate_regular_layouts.py), or the [workflow](../src/python/ifcn/workflow.py) for fixed graph drawing and the [benchmark runner](../tests/benchmarks/benchmark_irregular_layout.py) for irregular batches. See [algorithm commands](algorithm-availability.md) and [execution validation](validation.md) for sequential and physical checks.

CSV 保存原始源路径、最终电路路径、哈希、每层检查状态及原运行失败项。`evidence_location` 是原运行相对目录或本地归档目录标识，不是仓库内可下载文件。原始 QCA、波形、日志和工具快照保存在本地仓库外归档，不随仓库分发。
The CSV records sources, final circuits, hashes, distinct checks and every failed run. `evidence_location` identifies an original run-relative path or local archive entry, not a downloadable repository file. Raw QCA, waveforms, logs and tool snapshots remain in a local external archive.
