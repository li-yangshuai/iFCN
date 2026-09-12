# 不规则时钟基准 / Irregular-clock benchmarks

**2026-09-13** · [简体中文 README](../README.md) · [English README](../README.en.md) · [完整 CSV / Full CSV](irregular-benchmarks.csv)

本轮对 **37 个 TOY/MAJ 文件路径**运行统一 `irregular` 布局器，并独立筛选真实 QCA 器件。
候选池中 **31 个路径找到通过物理检查的器件，6 个在本轮已测范围内没有通过候选**。
默认 CLI 按几何面积与元胞数择优，不执行物理真值筛选；这两种结果在下表分开记录。
所有“最小”仅指记录范围内已测且通过的候选，不表示全局最优。

The unified `irregular` flow was run on **37 TOY/MAJ paths**. Independent QCA-device checks
found a passing candidate for **31 paths**; **6 have no pass within the tested scope**.
The default CLI ranks geometry and cell count without physical truth selection. Its result is
reported separately below. Every minimum is among recorded, tested passing candidates, not a global optimum.

独立的有限 D4 旋转／镜像实验又为 **4 个文件路径**找到物理穷举通过的器件，累计有
**35/37 个路径**具备已记录的物理通过证据；`TOY/RCA2.v` 与 `MAJ/mux41.v` 仍未通过。
补充实验不回填主轮默认输出、候选池或 **31/37** 计数，详见下方独立小节。

A separate finite D4 rotation/reflection audit found exhaustive physical passes for **four
additional paths**. Recorded passing evidence now covers **35/37 paths**; `TOY/RCA2.v` and
`MAJ/mux41.v` still fail. The supplement does not rewrite default outputs, the main candidate
pool or its **31/37** result.

## 范围与预算 / Scope and budget

| Item | Recorded configuration |
| --- | --- |
| Inputs | 37 Verilog paths: TOY 23 + MAJ 14; duplicate contents retain separate rows |
| Matching | Exact original-source SHA-256, candidate SHA-256 and QCA SHA-256 |
| Solver | One native `irregular` entry; soft 120 s/case; at most 320 attempts |
| Process deadline | 130 s per native command, including 10 s export/validation grace |
| Parallelism | 4 layout jobs; 3 physical jobs; BLAS/OpenMP threads fixed at 1 |
| Build | Frozen Release P&R and mapper; fresh physical runs use Release, reused evidence retains its original build hash |
| Candidate pool | `--export-candidates`; geometry duplicates removed before physical selection |
| Logic/DRC | Full source-to-DAG truth up to 11 inputs; route/global-clock DRC; native mapping DRC; exact QCA I/O labels |
| Physical budget | At most 8 unique legal candidates per path; 30 s mapping / 600 s simulation deadline per candidate |
| Selection order | Clock-tile bounding-box area → actual QCA cells → route length; stop at first physical pass |

软预算结束时已完成且通过最终验收的结果可以保留；进程硬超时、非零退出及未通过上游验收的
候选不会纳入成功或面积比较。归一化与逻辑优化随版本变化，因此这是同源端到端比较，
不是固定前端后的纯路由器比较，也不据不同历史构建的耗时排名。

A completed, validated incumbent may survive the solver soft deadline. A hard timeout, nonzero
exit or upstream rejection cannot count as a pass or an area winner. Frontend optimization
changes across versions, so these are end-to-end comparisons for identical source bytes,
not isolated-router or historical-speed rankings.

## 验证结果 / Validation results

| Check | Result |
| --- | --- |
| Default geometry / source truth / mapping | 37/37 validated; 7,240 exhaustive source-to-DAG vectors |
| Default candidate physical status | 18 pass, 17 fail, 2 unchecked |
| New candidate pool | 31/37 pass: 26 exhaustive, 5 sampled |
| Alternative recovered after default failure | 11 paths |
| Candidate collection | 610 exported snapshots; 464 unique candidates pass mapping checks |
| Physical candidate evaluations | 66, including 16 exact-device/protocol evidence reuses |
| Recorded passes including history | 31/37 paths; historical devices are not automatically current-CLI outputs |

每个输入向量保持 `max(8, ceil(max_epoch / 4) + 3)` 周期，全部向量重复两遍，每周期
**128 个样本**。检查最后 3 周期的输出 hold 阶段，要求与原始源逻辑一致且 **|P| ≥ 0.5**；
同时核对实际输入、全部样本收敛，以及基线／加速两份 RST 的逐点一致性与各自源真值。
输入数不超过 5 时物理穷举；更大电路使用固定的 32 个不同向量，明确标为抽样。
物理通过仅覆盖该慢输入 Bistable 协议，不代表所有连续输入序列、单周期吞吐或能量收敛已获验证。

Each vector is held for `max(8, ceil(max_epoch / 4) + 3)` cycles and the sequence runs twice,
at **128 samples per cycle**. Final-three-cycle output hold samples must match original-source
truth with **|P| ≥ 0.5**. Checks also verify actual input traces, convergence and both baseline
and accelerated RSTs independently. Physical coverage is exhaustive up to five inputs and
uses a fixed 32 distinct vectors for larger circuits. Passing this settled-input Bistable protocol
does not certify arbitrary input streams, single-cycle throughput or energy convergence.

## 表格说明 / Table key

- **历史几何参考 / Reference**：冻结基准最小几何合法结果；`†` 表示该冻结表未记录物理通过，不能作为已通过器件。旧算法名称及哈希在 CSV 中保留。
- **默认 / Default**：本轮 CLI 实际默认输出及其独立物理状态。`Unchecked` 保留未测状态，即使同面积候选已经通过。
- **新池通过 / New pool pass**：本轮已测有序候选中最小通过结果；`—` 表示没有通过，失败详情见下文。
- **已录通过最小 / Recorded minimum**：本轮与已复核历史器件中，按相同源哈希选出的最小通过结果；不会改写默认 CLI 的输出。
- **单位 / Units**：`时钟块面积 / QCA 元胞数`。面积为实际占用时钟块包围框的宽×高。`E` = 物理穷举，`S` = 物理抽样。

来源 / Provenance: **N** = this candidate pool; **H1** = historical `unified-physical`;
**H2** = historical `unified-optimized-physical`; **P** = retired Python random experiment.
Historical waveform evidence is rechecked from raw traces. A historical IFCN must be remapped
and revalidated with the current mapper before promotion as a current example.

下列主表的“已录通过最小”及 CSV 原有字段只合并主轮与历史证据；D4 结果单独列出，
保存在新增的 `supplement_d4_*` 字段，避免把补充筛选当成默认 CLI 的行为。
The main tables and existing CSV fields combine only main-run and historical evidence.
D4 results remain separate in the new `supplement_d4_*` fields.

## TOY

| Source path | Reference | Default + physical status | New pool pass | Recorded minimum |
| --- | ---: | --- | ---: | ---: |
| [1bitAdderAOIG.v](../tests/benchmarks_f/TOY/1bitAdderAOIG.v) | 102 / 423 † | 65 / 315 · Fail | 84 / 420 E | 84 / 420 E · N |
| [1bitAdderMaj.v](../tests/benchmarks_f/TOY/1bitAdderMaj.v) | 510 / 1413 † | 40 / 260 · Pass | 40 / 260 E | 40 / 260 E · N |
| [RCA2.v](../tests/benchmarks_f/TOY/RCA2.v) | — | 912 / 2156 · Fail | — | — |
| [b1_r2.v](../tests/benchmarks_f/TOY/b1_r2.v) | 200 / 707 † | 140 / 725 · Fail | 252 / 711 E | 170 / 716 E · H1 |
| [clpl.v](../tests/benchmarks_f/TOY/clpl.v) | 210 / 821 † | 208 / 708 · Pass | 208 / 708 S | 208 / 708 S · N |
| [clpl_source_generate/clpl_source.v](../tests/benchmarks_f/TOY/clpl_source_generate/clpl_source.v) | 210 / 821 † | 208 / 708 · Pass | 208 / 708 S | 208 / 708 S · N |
| [mux21.v](../tests/benchmarks_f/TOY/mux21.v) | 16 / 76 † | 16 / 71 · Pass | 16 / 71 E | 16 / 71 E · N |
| [mux41.v](../tests/benchmarks_f/TOY/mux41.v) | 176 / 598 † | 176 / 598 · Fail | — | — |
| [mux41_source_generate/mux41_source.v](../tests/benchmarks_f/TOY/mux41_source_generate/mux41_source.v) | 176 / 598 † | 176 / 598 · Fail | — | — |
| [newtag.v](../tests/benchmarks_f/TOY/newtag.v) | 165 / 456 † | 110 / 447 · Pass | 110 / 447 S | 110 / 447 S · N |
| [paper_2ddwave_carry_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_carry_demo.v) | 40 / 177 † | 35 / 164 · Pass | 35 / 164 E | 35 / 164 E · N |
| [paper_2ddwave_crossing_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_crossing_demo.v) | 42 / 199 † | 6 / 28 · 未测 / Unchecked | 6 / 28 E | 6 / 28 E · H2 |
| [paper_2ddwave_xor_demo.v](../tests/benchmarks_f/TOY/paper_2ddwave_xor_demo.v) | 12 / 80 † | 12 / 80 · Pass | 12 / 80 E | 12 / 80 E · H2 |
| [par_check.v](../tests/benchmarks_f/TOY/par_check.v) | 345 / 736 † | 133 / 536 · Fail | 133 / 561 E | 133 / 561 E · N |
| [par_gen.v](../tests/benchmarks_f/TOY/par_gen.v) | 102 / 466 † | 78 / 337 · Fail | 112 / 418 E | 112 / 418 E · N |
| [random_clock_cell_demo.v](../tests/benchmarks_f/TOY/random_clock_cell_demo.v) | 220 / 684 † | 35 / 186 · Fail | 48 / 225 E | 48 / 225 E · H2 |
| [t.v](../tests/benchmarks_f/TOY/t.v) | 221 / 567 † | 84 / 372 · Fail | 88 / 355 E | 88 / 355 E · N |
| [xnor2.v](../tests/benchmarks_f/TOY/xnor2.v) | 20 / 117 | 24 / 116 · Pass | 24 / 116 E | 20 / 117 E · P |
| [xnor2_source_generate/xnor2_source.v](../tests/benchmarks_f/TOY/xnor2_source_generate/xnor2_source.v) | 20 / 117 | 24 / 116 · 未测 / Unchecked | 24 / 116 E | 20 / 117 E · P |
| [xor2.v](../tests/benchmarks_f/TOY/xor2.v) | 12 / 80 † | 12 / 80 · Pass | 12 / 80 E | 12 / 80 E · H2 |
| [xor2_demo.v](../tests/benchmarks_f/TOY/xor2_demo.v) | 12 / 80 † | 12 / 80 · Pass | 12 / 80 E | 12 / 80 E · H2 |
| [xor2_source_generate/xor2_source.v](../tests/benchmarks_f/TOY/xor2_source_generate/xor2_source.v) | 12 / 80 † | 12 / 80 · Pass | 12 / 80 E | 12 / 80 E · H2 |
| [xor5R.v](../tests/benchmarks_f/TOY/xor5R.v) | 1175 / 2090 † | 972 / 2318 · Fail | — | — |

## MAJ

| Source path | Reference | Default + physical status | New pool pass | Recorded minimum |
| --- | ---: | --- | ---: | ---: |
| [1bitAdderAOIG.v](../tests/benchmarks_f/MAJ/1bitAdderAOIG.v) | — | 108 / 507 · Fail | 114 / 557 E | 102 / 459 E · H2 |
| [1bitAdderMaj.v](../tests/benchmarks_f/MAJ/1bitAdderMaj.v) | 55 / 239 † | 15 / 64 · Pass | 15 / 64 E | 15 / 64 E · H2 |
| [1bitAdderMaj_source_generate/1bitAdderMaj_source.v](../tests/benchmarks_f/MAJ/1bitAdderMaj_source_generate/1bitAdderMaj_source.v) | 55 / 239 † | 15 / 64 · Pass | 15 / 64 E | 15 / 64 E · H2 |
| [RCA2.v](../tests/benchmarks_f/MAJ/RCA2.v) | — | 748 / 1725 · Fail | 782 / 1775 E | 782 / 1775 E · H1 |
| [clpl.v](../tests/benchmarks_f/MAJ/clpl.v) | 1176 / 2484 † | 176 / 607 · Pass | 176 / 607 S | 176 / 607 S · N |
| [mux41.v](../tests/benchmarks_f/MAJ/mux41.v) | — | 464 / 1062 · Fail | — | — |
| [newtag.v](../tests/benchmarks_f/MAJ/newtag.v) | 1035 / 1503 † | 128 / 518 · Fail | 130 / 403 S | 130 / 403 S · N |
| [par_check.v](../tests/benchmarks_f/MAJ/par_check.v) | — | 133 / 551 · Fail | — | — |
| [par_gen.v](../tests/benchmarks_f/MAJ/par_gen.v) | 855 / 1726 † | 55 / 270 · Pass | 55 / 270 E | 55 / 270 E · H2 |
| [t.v](../tests/benchmarks_f/MAJ/t.v) | 180 / 711 † | 63 / 254 · Pass | 63 / 254 E | 63 / 254 E · N |
| [xnor2.v](../tests/benchmarks_f/MAJ/xnor2.v) | 140 / 542 † | 28 / 125 · Pass | 28 / 125 E | 28 / 125 E · N |
| [xor2.v](../tests/benchmarks_f/MAJ/xor2.v) | 319 / 673 † | 28 / 121 · Pass | 28 / 121 E | 28 / 121 E · N |
| [xor5R.v](../tests/benchmarks_f/MAJ/xor5R.v) | — | 324 / 1104 · Fail | 369 / 1250 E | 261 / 961 E · H2 |
| [xor5_r1.v](../tests/benchmarks_f/MAJ/xor5_r1.v) | — | 324 / 1104 · Fail | 369 / 1250 E | 261 / 961 E · H2 |

## 差异与未通过案例 / Differences and unsuccessful cases

例如 TOY XNOR 的历史 Python 器件为 **20 / 117**，本轮通过器件为 **24 / 116**：
本轮少一个元胞，却使用更多时钟块，不能称作历史最小面积。历史独立 Python 算法已经移除，
该证据只保留可追溯的比较价值，不能声称当前 CLI 自动生成它。

For example, the historical Python TOY XNOR is **20 / 117**, while this pool passes at
**24 / 116**. One fewer cell does not make the larger tile area a historical minimum.
The independent Python algorithm is retired; its traceable evidence does not mean the
current CLI automatically produces that device.

| Path with no new-pool pass | Tested candidates | Stop status | Historical passing device available |
| --- | ---: | --- | --- |
| `TOY/RCA2.v` | 2 | `no_pass_within_tested_prefix` | No |
| `TOY/mux41.v` | 1 | `no_pass_within_tested_prefix` | No |
| `TOY/mux41_source_generate/mux41_source.v` | 1 | `no_pass_within_tested_prefix` | No |
| `TOY/xor5R.v` | 3 | `no_pass_within_tested_prefix` | No |
| `MAJ/mux41.v` | 2 | `no_pass_within_tested_prefix` | No |
| `MAJ/par_check.v` | 4 | `no_pass_within_tested_prefix` | No |

`no_pass_within_tested_prefix` 表示预算内已测候选均失败；若有剩余候选，它们仍未测试。
这不证明该电路不存在其他可行布局。默认输出中的失败不会因另一个候选或历史器件通过而被改成通过。
完整 CSV 保留源、默认候选、新池候选和历史最小器件的哈希、预算、覆盖范围与来源。

`no_pass_within_tested_prefix` means the evaluated prefix failed within its budget; any remaining
candidates are untested. It does not prove that no other layout can work. A different passing
candidate or historical device never changes the default device’s failed status.

## 有限 D4 补充 / Finite D4 supplement

对主轮未通过的电路，只枚举已有合法候选的平面旋转／镜像。节点、路由与时钟块坐标
同时变换，保留拓扑、端口身份及每条路线的相位序列，再独立检查源真值、全局时钟与
实际 QCA 映射。后续批量实验按面积、实际元胞数、线长排序后测试；每个原候选最多 8 个方向，
已有原方向失败记录不重复运行，首次 TOY MUX 诊断除外。

Only planar rotations/reflections of existing candidates were considered. Nodes, routes and
clock tiles transform together while topology, terminal identity and route-phase sequences
remain fixed. Each direction is freshly checked against source truth, global clocks and actual
QCA mapping; later batches test in area/cell-count/route-length order. There are at most eight directions
per base. Existing identity failures were reused except in the initial TOY MUX diagnostic.

| Source path | Supplemental result | Area / actual cells | Physical coverage | Checked hold samples |
| --- | --- | ---: | --- | ---: |
| `TOY/mux41.v` | Anti-diagonal reflection: pass | 176 / 596 | 64/64 vectors, twice; 8 cycles/vector | 24,960 |
| `TOY/mux41_source_generate/mux41_source.v` | Same source SHA-256; same passing evidence | 176 / 596 | 64/64 vectors, twice; 8 cycles/vector | 24,960, reused |
| `TOY/xor5R.v` | Reflect X coordinate: pass | 972 / 2318 | 32/32 vectors, twice; 12 cycles/vector | 12,480 |
| `MAJ/par_check.v` | Rotate 180°: pass | 180 / 681 | 16/16 vectors, twice; 8 cycles/vector | 6,240 |
| `TOY/RCA2.v` | Two bases × seven new directions: all fail | No passing device | 32/32 vectors per tested direction | Failed |
| `MAJ/mux41.v` | Two bases × seven new directions: all fail | No passing device | Fixed 32/64-vector screen per direction | Failed |

所有补充仿真仍使用每周期 **128 样本**、两遍输入序列、最后 3 周期 hold、**|P| ≥ 0.5**，
核对输入、收敛及两引擎原始波形。TOY MUX 的 32 向量筛选通过后另跑完整 64 表；其两个
文件内容哈希完全相同，只复用一次物理证据，不计为两次独立实验。MAJ MUX 没有筛选通过，
所以没有追加完整 64 表。上述通过均来自完整物理穷举，不代表单周期吞吐或能量签核。

All supplemental simulations retain **128 samples/cycle**, two input passes, the final-three-cycle
hold check and **|P| ≥ 0.5**, with input, convergence and raw dual-engine checks. The TOY MUX
passed a 32-vector screen and then a separate complete 64-vector run. Its two byte-identical
source paths share one evidence record, not two independent experiments. No MAJ MUX direction
passed screening, so its full 64-vector test was not run. Supplemental passes are exhaustive
physical truth results, not throughput or energy signoff.

D4 不是直接旋转已生成的 QCA：重新映射可能产生不同的精细几何，必须重新验收。
例如 TOY MUX 两个方向都映射为 596 个元胞，但 180° 旋转仍有 65 个错误 hold 点；
只有反对角线镜像通过完整检查。MAJ 奇偶校验的 28 个新方向中，10 个因实际映射冲突被拒绝；
其余 18 个按顺序测试 7 个后找到最小已测通过结果，11 个后续方向未测试。

D4 transforms are remapped gate layouts, not rigid rotations of a previously generated QCA.
Fine geometry can change and needs fresh validation. Two TOY MUX directions have 596 cells,
but the 180° rotation still fails 65 hold samples. For MAJ parity, mapping rejects 10 of 28 new
directions; seven of the 18 legal directions are physically tested before the first pass,
leaving 11 later directions untested.

原始证据位于 `output/mux-symmetry-experiment/`、`output/irregular-symmetry-toy/` 和
`output/irregular-symmetry-maj/`。CSV 独立记录变换、器件及报告哈希、完整物理覆盖和同源复用。
`supplement_d4_new_physical_tests` 计实际新仿真调用，包含通过筛选后的完整真值表复测。
主轮的 6 个失败路径因此仍保留原状态；补充后仍有 2 个电路没有已通过器件。

Raw artifacts remain in those three ignored output directories. Separate CSV fields record
the transform, device/report hashes, coverage and identical-source reuse. The new-test count
includes the full-table rerun after screening. All six main-run failure rows retain their
original status; two circuits still lack a passing device after the supplement.

## 历史最小器件的当前复验 / Current revalidation of historical minima

对上表中历史面积优于新池的 **6 个路径**，已用 IFCN 副本通过当前 Release 映射器重新导出 QCA，
并完成独立源真值、全局时钟 DRC、准确 I/O 标签及双引擎 Bistable 复验：**6/6 通过**，
共 46,800 个 hold 样本，无错误、无迭代超限，两引擎差为零。六项均重新运行仿真，未复用旧波形。

The **six paths whose historical devices use less area than the new pool** were re-exported
with the current Release mapper. All **6/6 pass** source truth, global-clock DRC, exact I/O
labels and paired Bistable checks: 46,800 hold samples, zero errors, full convergence and
zero engine differences. All six simulations are fresh.

| Source | Area / cells | Exhaustive vectors | Hold cycles / vector | Checked hold samples |
| --- | ---: | ---: | ---: | ---: |
| `TOY/b1_r2.v` | 170 / 716 | 8/8 | 8 | 12,480 |
| `TOY/xnor2.v` | 20 / 117 | 4/4 | 8 | 1,560 |
| `TOY/xnor2_source_generate/xnor2_source.v` | 20 / 117 | 4/4 | 8 | 1,560 |
| `MAJ/1bitAdderAOIG.v` | 102 / 459 | 8/8 | 8 | 6,240 |
| `MAJ/xor5R.v` | 261 / 961 | 32/32 | 9 | 12,480 |
| `MAJ/xor5_r1.v` | 261 / 961 | 32/32 | 9 | 12,480 |

两份 XNOR 仅依据冻结编译器的显式输出别名，把 `~w5` 改为原始源端口 `out`，并补显式
输出节点头；坐标、路径和时钟相位没有改变。重新导出的 QCA 与历史 QCA 逐字节一致，
唯一允许的差异是上述输出标签。其他四项的 QCA 完全相同。

For both XNOR files, frozen compiler alias metadata maps `~w5` back to source output `out`;
an explicit primary-output header was added. Coordinates, routes and phases are unchanged.
Their remapped QCAs match the historical files byte-for-byte after this sole label substitution.
The other four QCAs are identical without substitution.

原始历史证据未改动。后续示例只可采用 `output/historical-minima-remapped/cases/*/layout.ifcn`
中这六个已复验副本；未复验历史记录不能仅凭旧通过状态获得该资格。CSV 分别保留历史
器件哈希、当前副本哈希及复验报告哈希。这是独立补充验收，不改变主轮默认输出或 31/37 计数。

Historical evidence remains unchanged. These six revalidated copies in
`output/historical-minima-remapped/cases/*/layout.ifcn` are eligible for later examples;
other historical records do not gain that status from an old pass alone. The CSV preserves
original-device, current-copy and report hashes separately. This supplemental check does not
change the main-run default outputs or its 31/37 count.

复验汇总 / Remap summary SHA-256: `480381867c2abc01f3247ce0d28c2041466aaa3e50f5593533f376744de3b457`.
QCA 一致性 / QCA identity-check SHA-256: `d9ec1a31ed92983f326db75a4c8e2907def50f6a0b999412b36d751979fff358`.

## 复现与来源 / Reproduction and provenance

在空输出目录生成候选池；下面的命令仅完成逻辑／DRC／映射检查，不能代替物理筛选。
Generate the pool in a fresh directory. This command performs logic/DRC/mapping checks,
not the separate physical selection.

```bash
python3 tests/benchmarks/benchmark_irregular_layout.py \
  --build-dir build --output-dir output/irregular-candidates \
  --jobs 4 --budget 120 --attempts 320 --export-candidates
```

以下为原运行在 `output/irregular-consolidation/` 下的相对路径。原始波形、
候选池与工具快照已在本地移至仓库外归档，不随 Git 分发；仓库仅保留电路示例、
必要验证工具和本报告。重跑命令会在指定输出目录生成新的记录。

These are paths relative to the original run directory. Raw traces, candidate pools and
tool snapshots are archived locally outside the repository and are not distributed in Git.
The reproduction commands generate new records in the requested output directory.

- `baseline-reference/winners.csv`：历史几何参考。
- `unified-repaired/manifest.json`、`inventory.json`、`cases/*/result.json`：冻结输入、预算、命令和原生输出。
- `unified-repaired-physical/summary.json`、`cases/*/selection.json`：逐候选物理结果、停止原因和原始 RST。
- `unified-repaired-physical/historical-comparison/`：哈希联表与重新核对历史波形的记录。

本次有界批量物理筛选器保存在实验快照中；仓库的
[物理回归](../tests/test_combinational_physical_waveform.py)及
[门模板回归](../tests/test_gate_template_physical.py)在临时目录中重建输入，守住相同真值标准。
The bounded batch selector is retained in the experiment snapshot. The repository physical
and gate-template regressions regenerate their inputs and enforce the same truth criteria.

| Frozen artifact | SHA-256 |
| --- | --- |
| `snapshot/ifcn_combinational_pnr` | `e4242ef38f89196f6a97696c2a86adc1aec6ed365d8a34f5ff61d5d8e2029a22` |
| `snapshot/ifcn_energy_analysis` | `ad96402d7951f70d6df4f5656c1ba6e7a4cbd0d884df20aebdff3f35d312aed4` |
| Physical snapshot `ifcn_physical_benchmark` | `2cc8816f3bb01f88e94de7e99349cc4b8d54f77306e8a6cf7abc6cfc5b4e6039` |
| Physical snapshot `selection_tool.py` | `b12e934dba47425174d5177c3cdb51f79c98e29fdbd6b96732d5a2b91cd5b1f1` |
| Historical reference CSV | `1a19ed8ed79dee470ff0e61e96d473a305ea29de8f5bbb13db930622740eac89` |
| Current physical summary | `1e8a704210139024e7026e6a13260ba5a6b864c5487219ee396dc89e42873ac3` |
| Historical comparison | `71ee8f4c4b8ecff8086bb653cc0ed62d90f364ff686c5d08434b93055db7d435` |

历史复用器件另存原仿真器哈希；上述 Release 仿真器哈希只标识新执行的仿真。
Reused evidence retains its original simulator hash; the Release hash above identifies fresh simulations.
