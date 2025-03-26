<p align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326123426428.png" width="150"/>
</p>

<h1 align="center">iFCN: Automated Design Platform for Molecular FCN Circuits</h1>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/C++-17-blue.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Framework-QT-green.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Python-3.8+-blue.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Developed%20By-HFUT%20iFCN%20Lab-orange" /></a>
</p>

---

**iFCN** is an automated design platform for Molecular Field-Coupled Nanocomputing (MolFCN) circuits, developed by the iFCN Lab at the School of Microelectronics, Hefei University of Technology (HFUT), China.

It adopts a hybrid programming architecture that combines **C++**, **Qt**, and **Python**, and supports both **manual** and **automatic** design modes. The platform enables researchers and engineers to efficiently construct, visualize, and analyze MolFCN circuits through a clock-aware and layout-driven workflow.


---

## 📬 Contact Us

- 🧑‍🏫 **Academic Cooperation**  
  📧 gjxie8005@hfut.edu.cn  
  📧 fpeng1985@126.com
  📧 2023010123@mail.hfut.edu.cn


- 🛠️ **Software Installation / Bug Reports / Algorithm Issues**  
  📧 2023010123@mail.hfut.edu.cn




---

## MolQCA Fundamentals
Molecular Field-Coupled Nanocomputing(MolFCN) has emerged as a promising post-CMOS computing paradigm,characterized by ultra-low power consumption and high integration density. Molecular quantum-dot cellular automata (MolQCA) serves as the foundational paradigm for MolFCN.

<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326141423048.png" width="400" style="margin: 10px;" />
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326141607113.png" width="350" style="margin: 10px;"/>
  <p><b>Figure 1:</b> 
    (Left) (a) Different states in 4-site and 6-site MolQCA cells. 
    (b) The four phases of the clocking mechanism. 
    (c) 2DDWave clock scheme. 
    (d) USE clock scheme. <br/>
    (Right) A sample MolQCA standard cell library, including: standard inverter, inverter with two fanouts, directional inverter, 3-input majority gate, AND gate, and OR gate. 
    The top row shows the logical symbols, and the bottom row shows their corresponding 5×5 cell-based layouts.
  </p>
</div>




## Design Workflows

<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/Fig.12(a).png" width="400" style="margin: 10px;"/>
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/Fig.12(b).png" width="340" style="margin: 10px;"/>
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/Fig.12(c).png" width="340" style="margin: 10px;"/>
  <p><b>Figure 2:</b> iFCN graphical user interface (GUI) in different design scenarios:  
  (a) Manual routing mode.  
  (b) Layout of the 4-to-1 multiplexer (MUX 4:1) circuit under the USE clocking scheme.  
  (c) Layout of the parity circuit under an irregular clocking scheme.
  </p>
</div>




- **Manual Design Flow**: Seamlessly integrates with *QCADesigner* for direct import and manipulation of `.qca` files.
    
- **Automated Design Flow**: Converts RTL-level abstractions into detailed physical layouts for FCN circuits.
    
- **Simulation and Energy Analysis**: Supports bistable and coherence vector simulation models and provides real-time power consumption analysis.
    
- **Flexible Output and Export Options**: Provides `.qca`, `.rst`, and `.tex` formats for academic and engineering applications.



### Automated P\&R Flow

*IFCN* uses a series of innovative technologies to automate the design process:

- RTL-level optimization includes *Node Layering*, *Multi-Fanout Optimization*, *Inverter Hiding* and *Insertion of redundant nodes*.
- The Morton code quadtree is used for space management. 
- Enhanced A* routing algorithm supports multi-path multiplexing. 
- A two-level heuristic P&R framework based on genetic algorithm (GA) and A * routing algorithm under the regular clock scheme.([A more detailed introduction](https://ieeexplore.ieee.org/document/9866102))
- A hierarchical graph drawing-based placement algorithm that decouples placement from predefined clock phases, enabling adaptive clock-phase assignments tailored to circuit topology. 
- Adaptive mapping, transforming abstract gate level circuit P&R results into detailed, physically realizable cell level results. 

<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326135020426.png" width="700" style="margin: 10px;"/>
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326135035291.png" width="700" style="margin: 10px;"/>
  <p><b>Figure 3:</b> End-to-end design flow of iFCN.  
  The system takes Verilog hardware description as input and automatically completes parsing, netlist generation, clocking assignment, placement, routing, and simulation analysis. 
  </p>
</div>



## Simulation and Energy Analysis

*iFCN* integrates a self-developed, modular C++ simulation engine that supports both [bistable and coherent vector simulation models](https://ietresearch.onlinelibrary.wiley.com/doi/full/10.1049/el.2019.1861). The engine is designed specifically for MolQCA circuits and enables real-time waveform analysis through an intuitive graphical GUI.


<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/screen3.png" width="500" style="margin: 10px;"/>
  <p><b>Figure 4:</b> Simulation interface of iFCN.  
  The GUI provides waveform visualization, simulation control, and real-time logic value tracing to verify circuit behavior under the assigned clocking scheme.
  </p>
</div>


# Installation Guide

## Environment

Since the algorithm is still under development and the software has not yet released an official version, it must be installed and run in a Linux environment. The recommended setup is Ubuntu 20.04 with WSL2.

## Requirements

The following components need to be installed:

- Qt development environment (e.g., Qt5 or Qt6)
- LaTeX compiler (e.g., TeX Live)
- Basic libraries and dependencies (see below)

## Recommended Packages

```bash
sudo apt update && sudo apt upgrade -y

# Essential tools
sudo apt install -y build-essential cmake git wget curl unzip

# Qt environment
sudo apt install -y qt5-default qtcreator

# C++、boost and graph libraries
sudo apt-get install git g++ cmake libboost-all-dev graphviz python3 python3-dev libreadline-dev pkg-config libqt5svg5 libqt5svg5-dev
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
