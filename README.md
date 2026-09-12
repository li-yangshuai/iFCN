<div align="center">

# iFCN

**场耦合纳米电路的设计、布局布线与仿真工具**

编辑电路，生成版图，观察时钟与三维结构，运行物理仿真和能量分析。

**简体中文** · [English](README.en.md)

[安装启动](#quickstart) · [功能与操作](#xor-walkthrough) · [仿真](#simulation) · [导出](#export)

</div>

---

iFCN 提供 Qt 桌面编辑器，将 Verilog 逻辑、电路布局、QCA 单元映射和仿真结果放在同一个工作流程中。提供三类组合电路布局布线：规则时钟启发式、固定 2DDWave 图绘制、不规则时钟图绘制；另有独立的时序电路布局布线流程。

<a id="quickstart"></a>
## 安装与启动

Ubuntu / Debian 环境安装依赖并构建：

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
  qtbase5-dev libqt5svg5-dev libboost-all-dev graphviz libgraphviz-dev \
  python3 python3-dev python3-venv

git clone https://github.com/li-yangshuai/iFCN.git
cd iFCN
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/fcnx_gui
```

<a id="optional"></a>
<details>
<summary>启用经典 Python 2DDWave 布局</summary>

首次使用此后端时，安装 NumPy、Matplotlib、NetworkX、SciPy 和 pybind11，并在同一个构建目录启用扩展：

```bash
bash include/layout_backend/scripts/setup_python_env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DIFCN_BUILD_LAYOUT_BINDINGS=ON \
  -DPython3_EXECUTABLE="$PWD/include/layout_backend/myenv/bin/python" \
  -Dpybind11_DIR="$(include/layout_backend/myenv/bin/python -m pybind11 --cmakedir)"
cmake --build build -j2

export IFCN_LAYOUT_PYTHON="$PWD/include/layout_backend/myenv/bin/python"
export IFCN_LAYOUT_BINDINGS_DIR="$PWD/build/python/lib"
./build/fcnx_gui
```

</details>

<a id="xor-walkthrough"></a>
## 功能与操作

以下截图来自两输入 XOR 的固定 2DDWave 布局。示例已通过组合电路 DRC，并在 Bistable、Coherence 中核对了四组慢输入的稳定输出。

### 1. 打开或编辑电路

选择 **File → Open** 打开 `.ifcn` 电路；也可导入 `.qca`，或打开 Verilog 源码开始自动布局。多标签页可同时查看不同设计。

在源码编辑区修改输入、输出和逻辑表达式；在单元画布中使用 **Select / Insert / Drag** 选择、添加和移动单元。支持复制、粘贴、撤销与重做。

![Verilog 源码编辑界面](docs/images/xor2/01-source.png)

### 2. 解析并查看逻辑关系

在源码编辑器中点击 **Generate**，选择布局方式并生成电路。通过逻辑图检查输入、逻辑节点与输出的连接；下面的逻辑图来自实际解析结果。

![实际解析结果生成的逻辑图](docs/images/xor2/02-logic.png)

<a id="algorithms"></a>
### 3. 选择布局布线方式

点击工具栏 **Irregular-Clock Graph P&R** 开始布局，或点击旁边的箭头选择其他方式：

![实际布局布线算法菜单](docs/images/xor2/03-algorithms.png)

| 算法类别与界面选项 | 如何使用 |
| --- | --- |
| 规则时钟启发式 · **Heuristic P&R** | 选择 USE、RES 或 2DDWave，设置网格和搜索参数，运行遗传搜索与 A* 布线 |
| 固定 2DDWave 图绘制 · **2DDWave Fixed-Clock P&R** | 启用上面的 Python 后端后，生成固定四相位布局并收缩 |
| 不规则时钟图绘制 · **Irregular-Clock Graph P&R** | 载入网表，设置相位数、搜索次数和时间预算；自动比较布局候选并收缩，优先选择通过 DRC 的最小面积 |

运行时观察进度和检查结果；成功后在原理图中查看路径和时钟相位。布局器会检查布线完整性、交叉和相位约束；失败时按提示调整输入或搜索设置。

直接打开 [examples](examples/) 中按算法分类的 `.ifcn` 可查看实际结果。规则时钟示例来自 TOY，不规则时钟示例来自 TOY 和 MAJ。[示例验证表](docs/examples.md) 列出每个电路的来源和检查结果；`__drc_only` 表示物理输出检查失败，不能作为功能正确的电路使用。

![布局布线与时钟相位原理图](docs/images/xor2/03-routing.png)

### 4. 查看器件映射与时钟

布局完成后，在主画布查看映射得到的 QCA 单元。使用 **Clock0–Clock3** 设置单元相位，**Clock Grid** 显示或隐藏时钟网格，**Encode** 查看时钟区域编码。

勾选 **IO Contract** 可收缩输入输出连线，取消勾选恢复原布局。层控件用于切换、添加和删除单元层。

IO Contract 处理器件的输入输出连线；布局 compact 则优化时钟块和门的位置，两者是不同功能。

![QCA 单元画布与时钟控件](docs/images/xor2/04-device.png)

### 5. 观察三维结构

点击 **3D** 打开分层结构视图，检查跨层连接、单元位置和相位分布。二维画布与三维视图对应同一个电路。

![分层三维结构视图](docs/images/xor2/05-structure.png)

<a id="simulation"></a>
### 6. 运行 Bistable 仿真

在 **Simulation → Start Bistable Simulation** 中设置参数并运行；需要加速版本时选择 **Start Accelerated Bistable Simulation**。仿真结束后，在波形窗口查看各输入和输出的极化变化。

使用 **Start Bistable With Selective Simulation** 可以指定输入向量表。仿真使用当前画布中的电路，包括尚未保存的单元修改。

在输出时钟的保持阶段读取结果：极化接近 +1 / −1 分别表示 1 / 0，释放阶段接近 0 不表示稳定逻辑值。

![Bistable 极化波形窗口](docs/images/xor2/06-waveform.png)

### 7. 运行 Coherence 仿真

选择 **Simulation → Start Coherence Simulation**，设置时间步长和仿真时长；也可选择 **Start Accelerated Coherence Simulation** 或 Selective 输入模式。

波形窗口按存储样本显示轨迹，可以对照输入与输出检查电路响应。

![Coherence 极化波形窗口](docs/images/xor2/06-coherence.png)

### 8. 阅读能量结果

选择 **Simulation → Energy Analysis**，设置时钟和运行参数。运行后查看逐周期能量及残差；残差较大时，调整时间步长和仿真窗口后重新检查。

![实际能量报告的周期统计与残差](docs/images/xor2/07-energy.png)

### 9. 查看时序电路

通过 **File → Open** 打开 [反馈示例](examples/sequential/cyclic/toggle_ff.ifcn)，查看反馈线路和各时钟区域；[寄存器切分示例](examples/sequential/register_cut/toggle_ff.ifcn) 展示独立的 D/Q 边界。需要从 RTL 生成时，使用[时序布局命令](docs/algorithm-availability.md)。

时序流程保留跨周期距离，并求解全局时钟和启动间隔。当前示例通过结构、映射和时钟检查，完整状态器件的多周期物理行为仍待验证。

![实际时序反馈版图与时钟区域](docs/images/sequential-feedback.png)

<a id="export"></a>
## 保存与导出

- **File → Save / Save As**：保存可继续编辑的电路。
- **View → Save cell-level layout**：导出 SVG 或裁切 PDF，用于报告与论文。
- **View** 中的截图操作：保存界面画面；逻辑图和分层结构也提供导出功能。

![单元版图导出与截图菜单](docs/images/xor2/08-export.png)

不规则时钟布局的界面和命令行共用一个算法入口，自动完成候选搜索、时钟分配与 compact。时序电路另提供寄存器切分、周期反馈布局与全局时钟求解。

详细说明：[算法运行结果](docs/algorithm-availability.md) · [经典布局后端](include/layout_backend/README.md) · [时序设计](docs/sequential-design.md) · [验证记录](docs/validation.md)

<div align="center">

[返回顶部](#ifcn) · [English](README.en.md) · [MIT License](LICENSE)

</div>
