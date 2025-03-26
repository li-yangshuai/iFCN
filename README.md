# *IFCN*:  An Open-source EDA Tool for Field-coupled Nanocomputing (*FCN*)

*IFCN* is an open source EDA tool for Field-Coupled Nanocomputing (FCN) circuits, which is implemented using C++and based on the cross platform QT framework. As the systematic upgrade of [Spars tool chain]([Spars: A Full Flow Quantum-Dot Cellular Automata Circuit Design Tool | IEEE Journals & Magazine | IEEE Xplore](https://ieeexplore.ieee.org/document/9265404)), supports two design workflows: manual design and automated design. In addition, it provides simulation, energy analysis and multiple output/export functions. The visual design interface is realized through QT framework.

## Features

- **Manual Design Flow**: Seamlessly integrates with *QCADesigner* for direct import and manipulation of `.qca` files.
    
- **Automated Design Flow**: Converts RTL-level abstractions into detailed physical layouts for FCN circuits.
    
- **Simulation and Energy Analysis**: Supports bistable and coherence vector simulation models and provides real-time power consumption analysis.
    
- **Flexible Output and Export Options**: Provides `.qca`, `.rst`, and `.tex` formats for academic and engineering applications.
## Design Workflows

*IFCN* tool is implemented in C++based on cross platform QT framework, and its workflow is divided into manual design and automatic design: Figure []
### Manual Design Flow

The manual design mode in *iFCN* enables intuitive graphical interaction for:

- Manually placing cells, optimizing routing paths, and adjust layouts.
    
- Distinguishing between input/output cells, polarization cells, and crossovers.
    
- Selecting predefined or user-defined clocking schemes (e.g., 2DDWave, USE, RES).
    
- The clock phase is automatically allocated according to the cell layout to simplify the clock management.

### Automated Design Flow

*IFCN* uses a series of innovative technologies to automate the design process:

- RTL-level optimization includes *Node Layering*, *Multi-Fanout Optimization*, *Inverter Hiding* and *Insertion of redundant nodes*, which will simplify the complexity of the input FCN circuit structure. Figure []
    
- The Morton code quadtree is used for space management, and the two-dimensional grid in the layout process is mapped to the one-dimensional Morton sequence. Figure []
    
- Enhanced A* routing algorithm supports multi-path multiplexing and improves routing efficiency. Figure []
    
- A [two-level heuristic P&R framework paper]([Field-Coupled Nanocomputing Placement and Routing With Genetic and A* Algorithms | IEEE Journals & Magazine | IEEE Xplore](https://ieeexplore.ieee.org/document/9866102)) based on genetic algorithm (GA) and A * routing algorithm under the regular clock scheme,  can systematically evaluate and iteratively optimize the layout quality through fitness functions including area, distribution balance, crossover avoidance and critical path optimization. Figure []
    
- Adaptive mapping, transforming abstract gate level circuit P&R results into detailed, physically realizable cell level P&R results of FCN circuits []

### Example
The following table shows the layout of the *IFCN* gui in manual design mode and automatic design mode. The automatic design includes layout of the mux4:1 circuit under the USE clocking scheme and layout of the parity circuit under the irregular clocking scheme. Table []

## Simulation and Energy Analysis

*IFCN* integrates a modular C++simulation engine, supports [bistable and coherent vector simulation models]([Automatic object model generation for nanoelectronics using C++ meta programming - Peng - 2019 - Electronics Letters - Wiley Online Library](https://ietresearch.onlinelibrary.wiley.com/doi/full/10.1049/el.2019.1861)), and provides real-time waveform analysis through a graphical gui. Figure []

## Output and Export Capabilities

*IFCN* supports multiple output formats for scientific research and engineering applications:

- **`.qca`**: Ensures compatibility with *QCADesigner*.
    
- **`.rst`**: Provides simulation results for subsequent verification.
    
- **`.tex`**: Exports layouts as vector graphics using the TikZ library for academic publications.
## Requirements

Use this command to install all necessary libraries.
```sh
sudo apt-get install git g++ cmake libboost-all-dev graphviz python3 python3-dev libreadline-dev pkg-config
sudo apt install libqt5svg5 libqt5svg5-dev
```
## How to Compile

```sh
git clone --recursive https://github.com/li-yangshuai/iFCN.git
cd iFCN
mkdir build
cd build
cmake ..
make
./fcnx_gui
```
