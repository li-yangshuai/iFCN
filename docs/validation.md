# 运行验证 / Execution validation

**2026-09-13** · [简体中文](../README.md) · [English](../README.en.md)

## 本次修复 / Corrections

组合电路检查发现了真实的器件功能错误：旧 Compact XOR 在输入 `11` 时输出为正。
原先只检查相邻相位是否前进，漏掉了汇合路径相差整周期的情况；门与相邻导线
处于同一相位时，还会出现极化回扰。现在组合布局检查全图绝对 epoch、主输入
启动周期、门前后相位边界与同相区域长度，不能满足约束的候选会被拒绝或在
搜索预算内重新布局。物理真值检查独立于布局器和布尔等价检查。

The previous Compact XOR produced the wrong device output for `11`. Local modulo
phase checks missed whole-cycle differences at reconvergent gates. Same-phase
loads could also disturb gate polarization. Combinational routing now checks
absolute epochs, input launch cycles, gate clock boundaries and bounded same-phase
regions. Invalid candidates are rejected or retried within the search budget.
Device truth is checked separately from layout legality and Boolean equivalence.

不规则图绘制现只有一个公共布局入口，候选统一经过布线、器件几何和全局时钟
检查，按实际占用矩形面积、QCA 单元数、线长排序。可导出搜索中完成的合法
候选，再独立检查实际输出；通过 DRC 不代表已经通过物理功能验证。

Irregular graph placement now has one public search entry. Completed candidates
share routing, mapped geometry and global clock validation, then rank by occupied
rectangle area, actual QCA cells and wire length. Candidate export supports an
independent physical-output audit; DRC acceptance alone is not a functional result.

Verilog 解析也已修正：括号表达式不再被误认作多数门；只有精确的
`ab | ac | bc` 模式才合并为 MAJ，包含跨赋值语句的等价表达式。显式输出身份
在简化后仍保留，多数门末端映射补齐输出角色。时序解析保留寄存器切分边界，
避免把 `NOT → D` 合并后产生错误的反馈自环。

The shared Verilog parser now handles parentheses as expressions and folds only
exact majority identities, including across assignments. Simplification preserves
observable outputs, and leaf majority mapping marks its output terminal. Sequential
parsing retains the `NOT → D` boundary to avoid creating a spurious self-loop.

多数门基底按流程选择：原生 `Parse` 的第三个参数 `allowMajority` 默认开启，
不规则和时序流程保留 MAJ。规则 GUI 启发式及经典 `CircuitParser` 关闭该选项，
将 `MAJ(a,b,c)` 等价展开为 `ab | c(a|b)` 的四个二输入门，以适配固定 2DDWave
仅有两个扇入方向的约束。回归覆盖 8 种输入取反组合 × 8 个真值向量 × 两套解析器，
同时检查展开后每个节点的扇入不超过 2。

The majority basis is selected per flow. Native `Parse` defaults its third
`allowMajority` argument to true, retaining MAJ in irregular and sequential
flows. Regular GUI heuristics and the classical `CircuitParser` disable it and
expand `MAJ(a,b,c)` into four two-input gates implementing `ab | c(a|b)`, matching
the two available fixed-2DDWave fanin directions. Regression checks cover eight
input-negation combinations × eight vectors × both parsers and verify fanin ≤ 2.

组合与时序映射明确使用不同的 `MappingMode`。组合路径可以在时钟块内部偏移；
时序路径必须保留有序时钟块、反馈线路和 iteration distance，不能套用组合
图的无环或零跨周期假设。GUI、映射统计和 QCA 导出统一使用通过验证的实际
单元位置，避免画布与仿真器读取不同器件。

Combinational and sequential layouts retain distinct mapping modes. Combinational
routes may shift within a tile; sequential routes retain ordered tiles, feedback
and iteration distances. GUI rendering, mapping metrics and QCA export now use
the same validated physical sites.

外部智能体集成及其专用评测集、机器学习模型与训练代码均已移除。经典布局后端
保留在 `include/layout_backend/`，使用 NumPy、Matplotlib、NetworkX、SciPy 和
pybind11，不依赖 Torch、PyG 或 scikit-learn。

External agent integration, its dedicated evaluation set, and machine-learning
models/training code were removed. The retained classical backend uses NumPy,
Matplotlib, NetworkX, SciPy and pybind11 without ML dependencies.

根目录 `scripts/` 已移除：通用工作流与校验整理为 `src/python/ifcn/`，必要批量
测试放在 `tests/benchmarks/`，波形校验在 `tests/support/`，README 演示入口在
`docs/examples/`。保留 12 个必要模块，删除 21 个历史论文/外部比较工具及其
4 个专属测试；实际逻辑、物理和时序回归继续保留。

The root scripts directory was removed. Shared workflow and validation modules
live in `src/python/ifcn/`; necessary benchmark runners, waveform helpers and the
README demonstration live under `tests/benchmarks/`, `tests/support/` and
`docs/examples/`. Twelve required modules remain; 21 historical comparison tools
and four dedicated tests were removed while retaining functional regressions.

## 验证环境 / Environment

| Component | Configuration |
| --- | --- |
| C++ | GCC 13.3.0, C++17, external Debug build and fresh Release build |
| CMake / Qt / Graphviz | 3.28.3 / 5.15.13 / 2.43.0 |
| Python | 3.12.3, clean virtual environment without ML packages |
| Optional native bindings | Separate fresh Release build |
| Optional clock solver | z3-solver in the selected Python environment |

## 检查结果 / Checks

v1.0.0 的相位编码与 GA 修复完成后，完整 Debug 和 Release 构建通过；
全部 71 项 CTest 最终通过：首次 69 项通过，两项时序报告测试更新为按公式
解码并排除空白填充后，单独复跑通过。原有时钟推进与资源计数断言保留，
将实际生成文件的相位全部置零时，两项测试仍明确失败。安装目录中的 GUI
成功打开编码 MUX41，安装后的固定时钟后端也完成 XOR 的布局及编码导出。

After the phase-codec and GA fixes, complete Debug and Release builds pass.
All 71 CTest checks ultimately pass: 69 initially, then two sequential report
checks after adapting their readers to formula decoding and excluding unused
padding. Clock progression and resource-count assertions remain; both checks
reject generated files whose phases are deliberately zeroed. The installed GUI
opens encoded MUX41, and the installed fixed-clock backend routes and exports XOR.

- 清理后完整 Debug 构建与 **71/71 项 CTest 通过**。首次执行通过 70 项，
  一项测试文件重命名后的注册路径修正后，单独重跑通过。清理前的 75 项中，
  4 项仅属于已删除的历史报告工具。
  基本门回归曾在 40 种有效方向下验证 **59,280** 个稳定输出点全部正确。
  The post-cleanup Debug build and **71/71 CTest checks pass**. Seventy passed
  initially; the remaining check passed after correcting its renamed test-file
  path. Four historical report-runner checks were removed from the earlier 75.
  The gate-template regression previously passed **59,280** settled samples
  across 40 valid orientations.
- 另从空目录配置并完整构建 Release，**4/4 项关键 CTest 通过**，覆盖表达式
  解析、遗传布局合法性、时序映射元数据和布局产物校验。该次仅运行这四项，
  不与上述完整 Debug 测试数量混计。
  A separate Release configuration and full build from an empty directory pass
  **4/4 selected CTest checks**: expression parsing, genetic-layout legality,
  sequential mapping metadata and layout artifact validation. This selected
  Release run is recorded separately from the full Debug suite.
- 独立重新读取最终保存的 **89 个组合 IFCN**：52 个规则版图通过全部
  **9,216** 个源逻辑向量和 DRC，37 个不规则版图通过 **7,240** 个向量和 DRC，
  合计 **16,456** 个向量。这里检查实际保存文件，物理功能范围仍按
  [案例记录](examples.md) 区分。
  Independent rereading of all **89 saved combinational IFCN files** passes DRC
  and **16,456 source-logic vectors**: 9,216 across 52 regular layouts and 7,240
  across 37 irregular layouts. Physical coverage remains separately recorded
  in the [example inventory](examples.md).
  其中新增 MUX41 的 64 组比对来自 GA 精英保留修复后的独立复测；原 88 份
  文件的审查记录继续保留，物理波形没有继承其他 MUX41 版图的结果。
  The additional 64 MUX41 comparisons come from a separate rerun after the GA
  elitism repair; the original 88-file audit is retained, and physical results
  from other MUX41 layouts are not reused.
- 迁移后独立 Python 单元检查：**99/99**，包含源逻辑、工作流状态和组合 DRC。
  Post-migration Python unit checks: **99/99**, including source logic, workflow
  state and combinational DRC.
- 经典 Python 后端：**57 项通过，另有 2 个子测试通过**，覆盖固定时钟布线、
  输入启动周期、布局压缩、阶段快照与输出保留。
  Classical backend: **57 tests and two subtests pass**, covering fixed-clock
  routing, input launch cycles, contraction, snapshots and observable outputs.
- 新工作流从仓库外目录启动，完成 XOR 的解析、不规则布局、4/4 源逻辑等价
  与器件映射；直接命令入口和 `doctor --deep` 的依赖探测均通过。
  The relocated workflow runs from outside the repository and completes XOR
  parsing, irregular P&R, exhaustive 4/4 logic comparison and device mapping.
  Direct command entry points and `doctor --deep` also pass.
- GUI 与 QCA 的实际坐标、层、相位、单元类型和 I/O 标签逐项比较：**5/5**，
  含三个组合版图和两个时序版图。切分/反馈案例的跨周期距离保持不变。
  Exact GUI/QCA site comparisons: **5/5**, spanning three combinational and two
  sequential layouts; sequential iteration distances were preserved.

全新构建目录标识为 `ifcn-final-fresh-build/`；关键测试日志为
`ifcn-fresh-critical-ctest.log`，保存文件复验记录为
`ifcn-examples-work/saved-regular-check.json` 和 `saved-irregular-check.json`。
这些原始证据归档在本地仓库外，不随仓库分发。
The fresh build used `ifcn-final-fresh-build/`. Its selected-test log and the two
saved-layout check reports are retained in the local external archive, not
distributed with the repository.

Graphviz 重复创建并释放上下文会触发 Pango 字体缓存的释放后访问，已用未屏蔽的
ASAN 独立复现。两个绘图入口现共享进程内上下文与互斥锁，局部图及布局数据由
RAII 释放。[真实入口回归](../tests/LegacyGraphvizRendererUnitTest.cpp) 的 16 轮
交替布局通过，独立 8 线程检查完成 400 次布局。共享实现的 101 次成功布局及
一次失败恢复未出现地址错误或未定义行为报告；退出时仍有 Graphviz／Fontconfig
相关 LeakSanitizer 报告，不能称为无泄漏。原始运行的相对目录为
`output/graphviz-lifetime-audit/`，证据保存在本地外部归档，不随仓库分发。

Repeated Graphviz context destruction reproduced a Pango font-cache use-after-free
under unsuppressed ASAN. Both rendering paths now share one serialized context,
with RAII cleanup for each graph and layout. Sixteen rounds of alternating production calls
and an independent 400-layout, eight-thread check pass. The shared helper completes
101 successful layouts plus recovery from a rejected layout without address or
undefined-behavior reports. Graphviz/Fontconfig LeakSanitizer reports remain at
exit; this is not a leak-free result. The original run used
`output/graphviz-lifetime-audit/`; raw evidence is retained in a local external
archive and is not distributed with the repository.

## 规则时钟补充检查 / Regular-clock follow-up

GA 交叉和变异现在跳过独立精英槽，尚无合法解时也保留最优有限适应度个体。
使用 96 代、128 个体、交叉率 0.9、20×20 网格、变异率 0.3 复测 MUX41，
在首个合法解处停止，得到 18×16、626 元胞版图；64/64 源逻辑向量、DRC、
全局时钟及实际映射均通过，物理波形未运行。相同代数与种群的 24×24/0.5
搜索失败，16×24/0.5 得到面积更大的 14×23、813 元胞结果；三组证据均保留于
仓库外 `ifcn-ga-mux41-repaired/`，完整比较与新增案例见[结果表](examples.md)。

GA crossover and mutation now skip an independent elite slot, preserving the
best finite-fitness individual even before a legal layout exists. A MUX41 rerun with 96 generations, 128 individuals, crossover 0.9,
a 20×20 grid and mutation 0.3 stops at its first legal result: 18×16 tiles and
626 cells, passing all 64 logic vectors, DRC, global clocks and mapping. Physical
waveforms are not run. At the same generation/population budget, 24×24/0.5 fails
and 16×24/0.5 finds a larger 14×23, 813-cell result. All three evidence sets are
retained outside the repository in `ifcn-ga-mux41-repaired/`; see [results](examples.md).

2DDWave 的相位模板保持不变。检查额外比较主输入的绝对启动周期
`floor((x+y)/4)`；同模四相位但相差整周期也会被拒绝。扩容在重布线前重新
对齐输入并刷新端口方向；压缩若破坏该条件，必须恢复原坐标与路由。
定向测试覆盖初始对齐、两类扩容调用和压缩拒绝后的恢复。

The 2DDWave coordinate template is unchanged. Validation also compares primary
input launch cycles, `floor((x+y)/4)`, so a whole-cycle delay cannot hide behind
equal modulo-four phases. Expansion realigns inputs before rerouting and refreshes
port directions; contraction restores the prior coordinates and routes if this
condition fails. Targeted tests cover initialization, both expansion paths and rollback.

四个原始 TOY 输入都通过布局、全局时钟、完整源逻辑等价和实际器件映射。
随后对同一器件执行 128 样本/周期、每向量保持 16 周期、末三周期保持区采样，
两遍输入序列的 Bistable 检查，结果如下。两种实现的输出差为零且迭代均收敛，
但三个电路仍有稳定的物理真值错误。

All four original TOY inputs pass layout, global-clock, exhaustive source-logic
and actual mapping checks. Their devices then undergo Bistable checks at 128
samples per cycle, 16 cycles per vector, and the last three hold windows, with
the input sequence repeated twice. Both implementations agree exactly and all
iterations converge, but three devices still have settled functional errors.

| Source | Tiles / QCA cells | Physical vectors | Incorrect / checked hold samples |
| --- | --- | --- | --- |
| `clpl.v` | 440 / 1,236 | Deterministic sample: 32 of 2,048 | **0 / 62,400** |
| `par_check.v` | 90 / 405 | Exhaustive: 16 | **3,120 / 6,240** |
| `par_gen.v` | 90 / 310 | Exhaustive: 8 | **780 / 3,120** |
| `xor5R.v` | 224 / 669 | Exhaustive: 32 | **6,240 / 12,480** |

这些结论绑定源文件、候选与 QCA 哈希，只适用于该次实际器件；同名电路的
其他布局不能继承结果。原始审查目录名为 `fixed-launch-review/`，保存在本地
外部归档；仓库中的示例验证表按具体器件记录其验证范围。

These results are bound to source, candidate and QCA hashes. A different layout
of the same named circuit cannot inherit them. Raw records from
`fixed-launch-review/` are held in a local external archive; example validation
records describe the scope for each specific device.

算法逐项结果和测试范围见 [算法可用性](algorithm-availability.md)。时序器件的
复位、保持与真实反馈存储尚未完成物理表征，不能将时钟约束通过表述为器件签核。
See [algorithm availability](algorithm-availability.md) for case results and scope.
Physical sequential reset, retention and feedback memory remain uncharacterized.

## README 小电路 / README example

所有图来自同一个固定 2DDWave XOR：**3 × 4 时钟块、74 个 QCA 单元**。
脚本从 Verilog 完成解析、布局、器件映射、仿真和能量分析。十张图包括八张
真实 Qt 窗口/菜单截图，以及从实际解析和能量数据绘制的两张图。

The walkthrough uses one fixed-2DDWave XOR with **3 × 4 tiles and 74 QCA cells**.
The runner executes the full pipeline. Ten figures comprise eight actual Qt
window/menu captures and two plots derived from parsed logic and energy data.

| Check | Result |
| --- | --- |
| Geometry, clock epochs and source-to-DAG truth | Passed; all four input combinations |
| Bistable | 8,192 samples over 64 cycles; 1,560 settled hold samples match source truth |
| Coherence RK4 | 6,400,000 steps at 0.1 fs; 3,001 stored samples; 574 settled hold samples match source truth |
| Baseline versus accelerated implementations | Maximum absolute output difference 0 in both models |
| Input stimulus | Each combination held eight cycles, four combinations repeated twice |
| Stable output criterion | Last three cycles per block, output hold phase, correct sign and absolute polarization ≥ 0.5 |
| Negative checker probes | Wrong-sign and near-zero output samples both rejected |
| Separate energy demonstration | 80 ps, 0.1 fs step, seven counted cycles; bath 0.150270 eV, residual 0.126950 eV |

能量图保留启动瞬态和残差；尚未完成时间步收敛检查，不能将这些数值作为收敛
功耗结论。慢输入真值检查验证本例的稳定功能，不证明单周期吞吐率。
Energy includes the startup transient and residual; convergence is untested.
Settled truth checks do not establish single-cycle throughput.

截图来源与完整复现命令见 [图片说明](images/xor2/README.md)。当前截图的原始运行目录为
`ifcn-readme-final-audit/`；此前诊断目录为 `output/drc-audit/`。原始报告保存在本地
外部归档，不随仓库分发，复现命令会生成新的结果目录。
See the [image record](images/xor2/README.md) for provenance and reproduction.
Raw reports from the named run directories are retained in a local external
archive, not shipped with the repository. Reproduction creates fresh results.

## 运行检查 / Run checks

按首页构建后，用安装了所需依赖的 Python 运行：
After building, select a Python interpreter with the required dependencies:

```bash
cmake -S . -B build -DIFCN_BUILD_TESTS=ON \
  -DPython3_EXECUTABLE="$PWD/include/layout_backend/myenv/bin/python"
cmake --build build -j2
ctest --test-dir build --output-on-failure
include/layout_backend/myenv/bin/python -m unittest discover -s tests -p 'test_*.py'
IFCN_LAYOUT_BINDINGS_DIR="$PWD/build/python/lib" \
  include/layout_backend/myenv/bin/python -m pytest include/layout_backend/tests
```

Z3 为可选依赖；未安装时明确跳过相关集成检查。外部 hfut 数据集不随仓库分发。
Z3 is optional; its integration tests are disabled explicitly when unavailable.
External hfut datasets are not distributed with the repository.

## 合并记录 / Consolidation record

`master` 保留原 `version1.1`、`feat/sequential-circuit-automation`、`version1.2`
的合并历史。本地与 origin 仅保留 `master`。2026-09-12 的历史提交 `304deef`
曾以 `git archive` 独立构建并通过 55 项测试；该历史数量与本次修复后的检查分开记录。

The former branch histories remain in master. Local and origin branch cleanup
retains only master. Historical commit `304deef` passed a separate archive build
and 55 tests on 2026-09-12; that count describes the earlier consolidation snapshot.
