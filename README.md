<p align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326123426428.png" width="150"/>
</p>

<h1 align="center">iFCN: Automated Design Platform for Molecular FCN Circuits</h1>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/C++-17-blue.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Framework-Qt-green.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Python-3.8+-blue.svg" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Developed%20By-HFUT%20iFCN%20Lab-orange" /></a>
</p>

---

*iFCN* is an automated design platform for **Molecular Field-Coupled Nanocomputing (MolFCN)** circuits, developed by the **iFCN** Lab at the School of Microelectronics, Hefei University of Technology (HFUT), China.

The platform adopts a hybrid architecture that integrates **C++**, **Qt**, and **Python**, supporting both **manual** and **automatic** design flows. It enables researchers and engineers to construct, visualize, and analyze MolFCN circuits through a **clock-aware and layout-driven workflow**.

---

# 👨‍💻 Author 

### **Yangshuai Li**  
🎓 *Ph.D. Candidate*  
🏫 School of Microelectronics, Hefei University of Technology (HFUT)  
🔧 Lead developer of **iFCN-EDA**  
📧 2023010123@mail.hfut.edu.cn
- Designed automated placement & routing algorithms  
- Built the UI framework  
- Maintains and integrates the full codebase  



### 🧑‍💻 Core Contributors (since 2018)

| Name               | Title / Affiliation                            | Contribution                                                                 | Email                        |
|--------------------|--------------------------------------------------|------------------------------------------------------------------------------|------------------------------|
| **Prof. Guangjun Xie** | Professor, HFUT     | Founder of iFCN Lab, academic advisor; guidance on FCN design methodology | gjxie8005@hfut.edu.cn       |
| **Dr. Fei Peng**   | Lecturer, HBFU              | Initiator of iFCN-EDA; designed logic synthesis algorithm, placement and routing algorithm, and simulation engine | fpeng1985@126.com           |
| **Xiansheng Tong** | M.Sc. Candidate, HFUT                           | Developed gate-to-cell level mapping algorithm for placement and routing results                            | 2023171256@mail.hfut.edu.cn|
| **Rongjie Zhu**    | M.Sc. Candidate, HFUT                           | Designed UI interface for simulation module                                  | 2023110949@mail.hfut.edu.cn  |
| **JiaChen Gao**    | M.Sc. Candidate, HFUT                           | ---                                 | --- |
| **XiangHan Wang**    | M.Sc. Candidate, HFUT                           | ---                                 | --- |

👥 Others Contributors (graduated): 
Zhengjie Xiao, Dong Xu, Rui Kuang, GaiSheng Li, Bing Zhang, Qian Han

---


# 🌱 MolQCA Fundamentals

Molecular Field-Coupled Nanocomputing (MolFCN) is a promising post-CMOS computing paradigm with ultra-low power consumption and high integration density. Molecular QCA (MolQCA) forms its physical foundation.

| ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326141423048.png) | ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326141607113.png) |
|:-------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------:|
| 🧱 MolQCA cell types and clocking schemes                                                   | 📐 Standard cell library and corresponding layouts                                          |

<!-- <p align="center"><b>Figure 1:</b> Left: MolQCA cell types and clocking schemes. Right: Standard cell library and corresponding layouts.</p> -->


---


#  🎮 GUI Showcase
| ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/Fig.12(a).png) | ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/Fig.12(b).png) |
|:-----------------------------------------------------------------------------------:|:-----------------------------------------------------------------------------------:|
| 🖱️ Manual Layout Mode                                                              | ⚙️ Automatic Layout Mode                                                           |

### 😲Manual circuit design
| ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/operate1.gif) | ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/operate2.gif) |
|:-----------------------------------------------------------------------------------:|:-----------------------------------------------------------------------------------:|
| 🔹 Basic circuit design and simulation | 🔸Support Clocking schemes                                         |





### 🔥 Automated placement & routing in two scenarios

To meet different design needs, iFCN-EDA supports two distinct placement and routing strategies:

| Scenario                    | Clock Scheme   | Area         | Routing Speed | Suitable For                               |
|-----------------------------|----------------|--------------|---------------|--------------------------------------------|
| 🔹 **Regular Clocking**     | Structured     |  Smaller    |  Moderate    | Small-scale circuits with simple topology  |
| 🔸 **Random Clocking**      | Irregular      |  Larger     |  Faster       | Medium-to-large and complex circuit designs |


| ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/pr1.gif) | ![](https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/pr2.gif) |
|:-----------------------------------------------------------------------------:|:-----------------------------------------------------------------------------:|
| 🔹 Regular Clocking: Small Area, Slower Routing                               | 🔸 Random Clocking: Larger Area, Faster Routing                              |

<!-- <p align="center"><b>Figure 3:</b> Two scenarios of placement & routing algorithms.</p> -->


### 😎Compatible with QCADesigner 
iFCN-EDA is developed to support **parallel usage with QCADesigner**, enabling seamless comparison, co-validation, and integration of layout and simulation results.
<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/operate5.gif" width="700"/>
  <p><b>Figure 4:</b> It is also compatible with QCADesigner.</p>
</div>



---

# 🛠️ Workflow 

### 🕹️Design flow

<p align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/framework.png" width="400"/>
</p>
<p align="center"><b>Figure 5:</b> <i>iFCN</i> framework.</p>



- **Manual Flow**: Integrated with *QCADesigner* for `.qca` file import and editing  
- **Automatic Flow**: Converts RTL-level circuits to physical layouts  
- **Simulation & Energy Analysis**: Supports bistable/coherence vector models and real-time energy visualization  
- **Output Support**: `.qca`, `.rst`, `.tex` formats for downstream use



### 🔥 Core technologies

*iFCN* integrates several advanced techniques:

- RTL optimization: Node layering, inverter hiding, redundant node insertion
- Space management: Morton-code based **quadtree**
- Routing: Enhanced **multi-path A\*** algorithm
- P&R framework: Two-level hybrid using **Genetic Algorithm + A\***
- Placement: **Graph-drawing-based**, adaptive to topology and clock
- Adaptive mapping: Converts logical layout to **cell-level design**

<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326135020426.png" width="800"/>
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/20250326135035291.png" width="800"/>
  <p><b>Figure 7:</b> End-to-end design flow of *iFCN* from Verilog to simulation analysis.</p>
</div>


### 📊 Simulation and energy analysis

*iFCN* includes a modular C++ simulation engine supporting both **bistable** and **coherence vector** models. Read more in our [published paper](https://ietresearch.onlinelibrary.wiley.com/doi/full/10.1049/el.2019.1861).

<div align="center">
  <img src="https://raw.githubusercontent.com/li-yangshuai/iFCN/master/image/simulation.png" width="500"/>
  <p><b>Figure 8:</b> Simulation GUI with waveform view, logic trace, and clock visualization.</p>
</div>

---

# 📦 Installation Guide

### Environment

> ⚠️ This project is under active development and does not yet have a release version.

- OS: Ubuntu 20.04 (recommended under WSL2 on Windows)
- Requires: Qt (5 or 6), LaTeX, C++17, and basic Linux dependencies

### Required Packages

```bash
sudo apt update && sudo apt upgrade -y

# Essential tools
sudo apt install -y build-essential cmake git wget curl unzip

# Qt environment
sudo apt install -y qt5-default qtcreator

# LaTeX
sudo apt install -y texlive-full

# C++, boost, graph, Python
sudo apt install -y git g++ cmake pkg-config libboost-all-dev graphviz python3 python3-dev libreadline-dev 
```

### Compile and Run

```bash
git clone --recursive https://github.com/li-yangshuai/iFCN.git
cd iFCN
mkdir build && cd build
cmake ..
make
./fcnx_gui
```


# 📖 Citation
**If you think our work is useful, please kindly ⭐️ star this repository or cite our work in your research.
📬 For any software-related questions or issues, feel free to contact me via the email provided at the top.**

📌 Layout and Routing Algorithms & Framework:

- F. Peng, Y. Zhang, R. Kuang and G. Xie,"Spars: A Full Flow Quantum-Dot Cellular Automata Circuit Design Tool,"IEEE Transactions on Circuits and Systems II: Express Briefs, vol. 68, no. 4, pp. 1233–1237, 2021.[DOI:10.1109/TCSII.2020.3039532](10.1109/TCSII.2020.3039532)


- Y. Li, G. Xie, Q. Han, X. Li, G. Li, B. Zhang, and F. Peng, "Field-coupled nanocomputing placement and routing with genetic and A* algorithms,"IEEE Transactions on Circuits and Systems I: Regular Papers, vol. 69, no. 11, pp. 4619–4631, 2022.[DOI:10.1109/TCSI.2022.3197450](10.1109/TCSI.2022.3197450)
- 李杨帅 , 彭斐 , 韩倩 , 李小帅 , 解光军. 一种针对QCA电路自动布局布线的混合策略研究[J]. 电子学报, 2023, 51(3): 666-674. https://doi.org/10.12263/DZXB.20211212

📌 Simulation Engine:

- F. Peng, Z. Xiao, D. Xu, J. Huang, and G. Xie, "Automatic object model generation for nanoelectronics using C++ meta programming,"Electronics Letters, vol. 55, pp. 1286–1288, 2019. [DOI:https://doi.org/10.1049/el.2019.1861](https://doi.org/10.1049/el.2019.1861).