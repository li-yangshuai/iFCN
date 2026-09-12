# XOR 案例图片 / XOR walkthrough images

[中文案例](../../../README.md#xor-walkthrough) · [English walkthrough](../../../README.en.md#xor-walkthrough)

这些图片来自 `integrations/pi/examples/xor2.v` 的一次完整实际运行。
运行及选图日期：2026-09-12。原始数据由
[`run_readme_demo.py`](../../../scripts/run_readme_demo.py) 生成，完整复现命令见主 README。

These images come from a complete execution of the repository's two-input XOR
example on 2026-09-12. Run the command in the main README to regenerate the raw
artifacts, `summary.json`, and all eight PNGs under `output/readme-demo/`.

| Image | Source |
|---|---|
| `01-source.png` | Native Qt Verilog source dock; `xor2.v` |
| `02-logic.png` | Graphviz rendering of the actual parsed `xor2_dag.json` |
| `03-routing.png` | Native Qt gate-level schematic; `xor2_layout.ifcn` |
| `04-device.png` | Native application canvas; exported `xor2_device.qca` |
| `05-structure.png` | Native layered structure view of the same exported QCA design |
| `06-waveform.png` | Native waveform viewer; `xor2_bistable_baseline.rst` |
| `06-coherence.png` | Native waveform viewer; `xor2_coherence_baseline.rst` |
| `07-energy.png` | Matplotlib plot of the actual per-cycle/per-cell energy report |

Qt screenshots use the offscreen platform and capture the actual widgets without
compositing. The DAG and energy figures are data exports. The reproduction was
checked with Qt 5.15.13, Graphviz 2.43.0, Python 3.12.3 and the project's Debug build.

The physical export contains 106 cells; mapping metrics before export normalization
count 110. Bistable uses 2,048 samples. Coherence uses RK4 with a 0.1 fs step over
80 ps, storing 3,008 samples. The energy run uses the same step and duration, with
10 ps clock/input periods and a 1 ps clock slope. Input vectors repeat
`00, 01, 10, 11` twice. Complete numerical arguments are saved in `commands.json`.

Boolean source-to-DAG equivalence and baseline/accelerated engine agreement were
checked separately. They do not establish device-level functional or timing
signoff. The energy figure includes the startup transient and balance residual;
time-step convergence has not been tested.
