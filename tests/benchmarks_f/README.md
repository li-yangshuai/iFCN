# 算法输入与电路样例 / Algorithm inputs and circuit examples

[项目说明](../../README.md) · [English guide](../../README.en.md)

此目录保留布局布线算法所需的 Verilog/SystemVerilog 输入和 `.ifcn` 电路。
QCA、波形、绘图和实验汇总由工具按需生成到构建或输出目录。

This directory retains HDL inputs used by the algorithms and `.ifcn` layouts.
Generate QCA designs, waveforms, figures and experiment summaries in a build or
output directory.

| 目录 / Directory | 用途 / Purpose |
|---|---|
| `TOY/` | 小型组合逻辑与回归输入 / Small combinational and regression inputs |
| `MAJ/` | 多数门表示与布局比较 / Majority-based representations |
| `IWLS93/`, `ISCAS85/`, `EPFL/`, `fontes18/` | 较大逻辑与标准基准输入 / Larger and standard benchmark inputs |
| `original_TOY/`, `original_large/` | 保留的基准源表示 / Preserved source representations |
| `SEQUENTIAL/` | 时序前端、状态边界、约束和验证程序 / Sequential inputs, constraints and validators |

初次运行可用 `TOY/xnor2.v`；门级映射样例见
[`../cell_level_examples/GoodiFCN/`](../cell_level_examples/GoodiFCN/)。
Pi 集成使用独立的冻结快照 [`../benchmarks_pi/v1/`](../benchmarks_pi/v1/)，
通过 `python3 integrations/pi/curate_benchmarks.py --check` 从仓库根目录校验。

Start with `TOY/xnor2.v`. Gate-mapping examples live in
`../cell_level_examples/GoodiFCN/`. The Pi integration uses a separate versioned
snapshot; its checker verifies source hashes and complete truth tables.

源基准的历史来源包括 [MNT Bench](https://www.cda.cit.tum.de/mntbench/)、
[Trindade16](https://ieeexplore.ieee.org/document/7724048)、
[Fontes18](https://ieeexplore.ieee.org/document/8351001) 和
[EPFL](https://www.epfl.ch/labs/lsi/page-102566-en-html/benchmarks/)。保留网表中的
来源说明；不同表示和历史文件名不自动保证逻辑等价，应使用相应验证器。

Retain provenance comments in the netlists. Historical filenames and alternative
representations do not by themselves establish logical equivalence; use the
associated validators when comparing designs.
