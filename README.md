# iFCN

iFCN is a Qt-based design and analysis tool for field-coupled nanocomputing circuits. It supports manual cell-level editing, Verilog-to-layout placement and routing, QCA file import/export, simulation, and energy analysis.

This project is under active development. The current documentation focuses on building and using the software from source.

## Main Features

- Manual cell-level layout editing with clock phase visualization
- Verilog parsing and automated placement/routing
- Compact graph-based placement/routing with selectable 3-phase or 4-phase assignment
- Gate-level layout mapping from `.ifcn` examples
- Bistable and coherence simulation
- Energy analysis reports and energy distribution images
- Export to `.qca`, `.rst`, layout image, and related analysis files

## Requirements

Recommended environment:

- Ubuntu 20.04 or newer
- CMake
- C++17 compiler
- Qt 5
- Boost
- Graphviz development libraries
- Python 3
- LaTeX tools, if you need LaTeX/TikZ export

Install common dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
    qtbase5-dev qtchooser qt5-qmake qtbase5-dev-tools \
    libboost-all-dev graphviz libgraphviz-dev \
    python3 python3-dev libreadline-dev
```

Optional LaTeX support:

```bash
sudo apt install -y texlive-full
```

## Build

```bash
git clone --recursive https://github.com/li-yangshuai/iFCN.git
cd iFCN
mkdir -p build
cmake -S . -B build
cmake --build build -j
```

Run the GUI:

```bash
./build/fcnx_gui
```

## Basic Usage

### Open or Create a Layout

1. Start `fcnx_gui`.
2. Use `File -> Open` to load an existing `.qca` or supported project file.
3. Use the left cell toolbox to insert input, output, normal, fixed, crossover, and via cells.
4. Use the clock selector to assign clock phases.
5. Use the layer controls to add, delete, and switch active cell layers.

### Run Graph-Based Placement and Routing

1. Click `Graph P&R` in the `Algorithms` toolbar.
2. Select a Verilog file, for example:

```text
tests/benchmarks_f/IWLS93/parity.v
```

3. In the options dialog, choose:
   - `4-phase` for the default clocking flow
   - `3-phase` for 3-phase phase assignment
   - Search attempts for compact layout exploration
4. The progress area shows the current layout candidate, spacing, routing search cost, and best area found so far.
5. When the run succeeds, the generated layout is inserted into the main scene.

### Run Gate-Level Mapping

1. Click `Gate Mapping`.
2. Select a gate-level `.ifcn` file, for example:

```text
tests/cell_level_examples/GoodiFCN/xor2_gate_level_pr.ifcn
```

3. The circuit is mapped into cell-level layout form in the scene.

### Run Simulation

Use the `Simulation` menu:

- `Start Bistable Simulation`
- `Start Coherence Simulation`
- selective simulation options when vector-table input is needed

Simulation progress is shown in the dynamic progress area. The UI remains usable while simulation tasks run.

### Run Energy Analysis

1. Open or map a circuit layout.
2. Use `Simulation -> Energy Analysis`.
3. The software writes an energy report and, when data is available, an energy distribution image.
4. Progress and summary information are shown in the dynamic progress area instead of only printing to the terminal.

Typical generated files are placed next to the source circuit:

```text
*_energy.txt
*_energy.rst
*_energy_distribution.png
```

## Example Test Circuits

Useful files for quick checks:

```text
tests/benchmarks_f/IWLS93/parity.v
tests/cell_level_examples/GoodiFCN/xor2_gate_level_pr.ifcn
tests/cell_level_examples/GoodiFCN/parity_gate_level_pr.ifcn
```

## Output Files

iFCN may generate:

- `.qca`: QCADesigner-compatible layout
- `.rst`: simulation or analysis result
- `.txt`: text analysis report
- `.png`: layout or energy distribution image
- `.svg`: graph/layout visualization
- `.tex`: optional TikZ/LaTeX output

Generated files may appear in the same directory as the input circuit or under the build/output location, depending on the workflow.

## Troubleshooting

If CMake cannot find Qt, install Qt development packages and rerun CMake from a clean build directory.

If Graphviz headers or libraries are missing, install:

```bash
sudo apt install -y graphviz libgraphviz-dev pkg-config
```

If a placement/routing run fails, increase the search attempts in the `Graph P&R` options dialog. Large or dense circuits may need more candidate layouts.

If phase assignment fails, try the other phase mode or increase layout search attempts. More spacing may give routing and phase assignment more freedom.

## Citation

If you use iFCN in academic work, please cite the related publications:

- F. Peng, Y. Zhang, R. Kuang and G. Xie, "Spars: A Full Flow Quantum-Dot Cellular Automata Circuit Design Tool," IEEE TCAS-II, 2021.
- Y. Li, G. Xie, Q. Han, X. Li, G. Li, B. Zhang, and F. Peng, "Field-coupled nanocomputing placement and routing with genetic and A* algorithms," IEEE TCAS-I, 2022.
- Y. Li, F. Peng, X. Tong, R. Zhu, Q. Han and G. Xie, "iFCN: An Automated RTL-to-Device Framework for Molecular Field-Coupled Nanocomputing Circuits," IEEE TCAS-I, 2025.

