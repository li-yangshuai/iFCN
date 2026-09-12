# XOR 案例图片 / XOR walkthrough images

[中文案例](../../../README.md#xor-walkthrough) · [English walkthrough](../../../README.en.md#xor-walkthrough)

这些图片来自 `tests/benchmarks_f/TOY/xor2_demo.v` 的一次完整实际运行。
运行及选图日期：2026-09-13。使用固定 2DDWave 布局，并通过组合电路 DRC 与两个
物理模型下的慢输入稳定真值检查。原始数据由
[`run_readme_demo.py`](../../examples/run_readme_demo.py) 生成。

These images come from a complete execution of the repository's two-input XOR
example on 2026-09-13 using fixed 2DDWave routing. Both physical models passed
settled-output checks against the source truth table. After building the programs
and classical Python backend, regenerate the raw artifacts and all ten PNGs:

```bash
include/layout_backend/myenv/bin/python docs/examples/run_readme_demo.py \
  --build-dir build --python include/layout_backend/myenv/bin/python \
  --bindings-dir build/python/lib --output-dir /tmp/ifcn-readme-demo \
  --screenshots docs/images/xor2
```

本次验证使用新构建的 GUI、命令行工具和共享解析器绑定，实际命令如下。
The validation run used newly built GUI/CLI programs and parser bindings:

```bash
/tmp/ifcn-layout-env/bin/python docs/examples/run_readme_demo.py \
  --build-dir /tmp/ifcn-clean-build --python /tmp/ifcn-layout-env/bin/python \
  --bindings-dir /tmp/ifcn-layout-build/python/lib \
  --output-dir /tmp/ifcn-readme-final-audit \
  --screenshots /tmp/ifcn-readme-final-audit/screenshots
cp /tmp/ifcn-readme-final-audit/screenshots/*.png docs/images/xor2/
```

`summary.json` in the selected output directory records the validation results;
`screenshots.json` records each image's source artifact and SHA-256 digest. The
PNGs were inspected before being copied into this directory. The latest default
layout and both physical models were rerun; no previous device's waveform or
energy data was reused.

本轮以最新默认布局重新执行两种物理模型和能量计算，所有图片对应同一个 **74 单元**
器件。器件 SHA-256：
`317cbc5d44bb483f21771ea85d139eb47686f99ab391b81e249365532a01c21c`。
仅检查解析、布局和映射时，可在上述命令中用 `--map-only` 替换 `--screenshots …`；
这会生成 `map_summary.json`，不会执行物理仿真。

The device SHA-256 above identifies all layout, structure, waveform and energy
figures. To check only parsing, routing and mapping, replace `--screenshots …`
with `--map-only`; its `map_summary.json` does not claim physical validation.

| Image | Source |
|---|---|
| `01-source.png` | Native Qt Verilog source dock; `xor2.v` |
| `02-logic.png` | Graphviz rendering of the actual parsed `xor2_dag.json` |
| `03-algorithms.png` | Native toolbar Place & Route menu, including the unified Irregular-Clock entry; no action is run |
| `03-routing.png` | Native Qt gate-level schematic; `xor2_layout.ifcn` |
| `04-device.png` | Native application canvas; exported `xor2_device.qca` |
| `05-structure.png` | Native layered structure view of the same exported QCA design |
| `06-waveform.png` | Native waveform viewer; `xor2_bistable_baseline.rst` |
| `06-coherence.png` | Native waveform viewer; `xor2_coherence_baseline.rst` |
| `07-energy.png` | Matplotlib plot of the actual per-cycle/per-cell energy report |
| `08-export.png` | Native View menu, captured without triggering an export |

Qt screenshots use the offscreen platform and capture the actual widgets without
compositing. The DAG and energy figures are data exports. The reproduction was
checked with Qt 5.15.13, Graphviz 2.43.0, Python 3.12.3 and the project's Debug build.

The physical export contains 74 cells in a 3 × 4 clock layout. Bistable uses
8,192 samples over 64 clock cycles. Coherence uses RK4 with a 0.1 fs step over
640 ps: 6,400,000 integration steps and 3,001 stored samples. Each input combination
is held for eight cycles; `00, 01, 10, 11` repeats twice. The final three cycles of
each combination are checked in the output clock's hold window: all 1,560 Bistable
and 574 Coherence samples have the expected sign and definite polarization.

The energy plot is a separate eight-cycle demonstration on the same device:
80 ps duration, 0.1 fs step, 10 ps clock/input periods and 1 ps clock slope.
Its profile and independent input vectors are recorded in `energy_profile.json`;
complete numerical arguments are in `commands.json`.

The slow-input checks validate this example's settled truth table, not single-cycle
throughput or every circuit. The energy figure includes the startup transient and
balance residual; time-step convergence has not been tested.
