# 合并与运行验证 / Merge and execution validation

验证日期 / Validation date: **2026-09-12**

[简体中文](../README.md) · [English](../README.en.md)

## 合并范围 / Integration scope

`master` 保留 `version1.1`、`feat/sequential-circuit-automation` 和远程
`version1.2` 的历史与工程能力。原 `review-clock-comparison` 与原主分支一致。
合并同时纳入工作区中的原生组合布局 CLI、Python 自动化适配器和 Pi 集成。

The integration preserves the branch histories, combinational and sequential
algorithms, ordered simulation graphs, cache controls and internal-state trace
verification. Current workspace additions are included as supported source.

## 验证环境 / Environment

| Component | Version / configuration |
|---|---|
| Compiler | GCC 13.3.0, C++17 |
| CMake | 3.28.3 |
| Qt | 5.15.13 |
| Graphviz | 2.43.0 |
| Python | 3.12.3, separate test environment with Z3 |
| Core build | Fresh out-of-source Debug build |
| Optional native Python module | Fresh out-of-source Release build |

## 已执行检查 / Executed checks

| 检查 / Check | 结果 / Result |
|---|---|
| GUI、全部原生 CLI 和 C++ 测试构建 / GUI, native CLIs and C++ tests | Passed |
| CTest | **58/58 passed**, including 3 optional external hfut dataset checks |
| Committed-source archive → fresh build → standalone CTest | **55/55 passed**, no historical build or untracked source dependencies |
| Standalone Python unit discovery under `tests/` | **91 passed** |
| Optional GCN/layout pytest suite | **113 passed**, including 5 subtests |
| Pi integration unit tests | **18 passed** |
| Frozen Pi benchmark source/hash/truth-table validation | Passed, 30 circuit representations |
| README CLI and offscreen GUI examples | **10 workflows passed** |
| Native automation: parse → P&R → mapping → simulation | Completed |
| Python XOR → 2DDWave layout → device mapping | Passed, 0 failed routes |
| Optional existing OGDF orderer regression | Passed; separate executable, not a fresh OGDF build |

最后独立验证使用代码提交 `304deef` 的 `git archive` 快照，完整构建后运行
55 项测试，全部通过。相同快照中的可选 Python 测试通过 113 项，扩展从外部
构建目录加载。此后的提交仅补充本验证记录。

The final independent check used a `git archive` snapshot of code commit
`304deef`. All targets rebuilt and all 55 standalone tests passed. The optional
Python suite also passed all 113 tests from that snapshot, loading its native
extension from an external build. Subsequent commits only update this record.

物理检查包含 Bistable/Coherence 基线与加速实现、选择性输入、复用耦合图、
完整内部轨迹及映射后的临时 QCA。示例的最大数值误差为零。时序检查覆盖
反馈线路、全局相位求解、Z3、映射元数据和波形。GUI 检查覆盖无显示导出和
IO 收缩控制的正常退出。

Physical checks cover baseline/accelerated Bistable and Coherence, selective
inputs, reusable interaction graphs and internal-state trajectories. Example
comparisons have zero maximum numerical error. Sequential checks cover feedback,
global clock solving, Z3, mapping metadata and waveforms. GUI checks include
offscreen export and clean termination of the IO-contraction control.

## 修复与可复现性 / Fixes and reproducibility

- 保留设计去重先于 I/O 初始化的顺序，避免失效指针；输入生成器就绪后再建缓存。
  Initialize design geometry before I/O pointers; build input caches after the
  generators exist.
- 避拥塞路由在有空路径时避开已占用网格，保留端口方向与交叉合法性检查。
  Prefer an unoccupied route when available while retaining port and crossover rules.
- 恢复布局、首次布线与冲突修复过程快照，并移除相邻重复帧。
  Restore intermediate layout/routing snapshots with adjacent-frame deduplication.
- 不兼容的 CUDA 内核自动回退到 CPU；Python 原生扩展仅生成到构建目录。
  Fall back to CPU for unsupported CUDA kernels and keep native Python binaries
  in the build directory.
- 修复任务状态与锁释放之间的竞态，避免把刚完成的任务误报为中断。
  Read a consistent job-state snapshot across lock release; deterministic
  regressions and ten repeated timeout/resume checks passed.
- 缺失电路文件返回错误状态；自动 GUI 导出使用非交互错误处理。
  Missing files return an error and automated exports avoid modal dialogs.
- 测试在 Release 下仍执行断言；不再隐式调用机器上的旧 OGDF 构建。
  Keep test assertions enabled in Release and make optional OGDF selection explicit.
- 图布局测试检查完整布线、合法相位、有界回退和压缩面积，避免绑定某一版本的固定坐标。
  Check routing completeness, legal phases, bounded fallback and compaction area
  rather than one Graphviz coordinate snapshot.
- QCA 与向量表在构建目录动态生成；Z3 来自指定的独立 Python 环境。
  Generate QCA/vector fixtures in the build directory and resolve Z3 from the
  selected environment.

运行命令与依赖设置见 README 的测试章节。没有外部 hfut 数据时不会注册那
3 项测试；缺少 Z3 时会明确关闭相关可选测试。完整模型训练、长时物理收敛
研究以及连接实际 LLM 的 Pi 会话不属于本次执行检查。数值实现等价不等于
器件波形已符合任意输入 RTL 的完整功能。

See the README for commands. Without external hfut data, those 3 optional tests
are not registered; missing Z3 is reported explicitly. Full model training,
long-duration physical convergence studies and live LLM-backed Pi sessions were
not part of these execution checks. Numerical implementation equivalence does
not establish arbitrary RTL-to-device functional equivalence.
