# Bistable 与 Coherence-vector QCA 仿真的严格等价稀疏编译加速

英文 SCI 题目候选：

> **Strictly Equivalent Sparse-Compiled Acceleration of Bistable and Coherence-Vector QCA Simulation**

本文档只讨论 Bistable 和 Coherence-vector 两个物理仿真器。加速不依赖训练模型，不替换物理方程，也不通过减少采样点或增大时间步换速度。核心是把第六章的“时钟分区、局部耦合、重复结构”转化为一次性物理内核编译。

## 总体框架

1. 对布局建立空间桶，只检查可能落入作用半径的候选元胞对。
2. 每对候选仍执行与基准相同的三维精确距离判断和扭结能计算。
3. 邻居按基准布局遍历序稳定排序，写入行内有序的 CSR 数组。
4. Bistable 使用连续索引和确定性 Gauss–Seidel 调度，保持响应函数、收敛阈值和最大迭代数不变。
5. Coherence-vector 预计算四相时钟，复用 `hypot`/`tanh` 公共子表达式，但保持稳态初始化、时间步和 Euler/既有分量式 Runge–Kutta 路径不变。
6. 正确性比较通过后才执行性能统计；任何一个电路不一致都必须先定位，不能只看输出波形是否相似。

完整 TikZ 图、方程、证明和 SCI 摘要位于 [clock_zone_simulation.tex](clock_zone_simulation.tex)。

## 有序稀疏物理图

对元胞 `i` 与 `j`，有效边条件为

```text
sqrt((xi-xj)^2 + (yi-yj)^2 + ((layer_i-layer_j)*layer_sep)^2) <= radius_effect
```

有效元胞对的扭结能仍由相同四量子点静电模型计算。编译后使用：

```text
row_ptr[0..N]
col_idx[0..M-1]
kink[0..M-1]
```

空间桶的发现顺序不能直接写入 CSR。每一行必须恢复为基准设计遍历顺序，使邻居集合、扭结能和浮点累加顺序都与基准一致。桶索引只删除必然超出半径的候选，不删除任何有效物理耦合。

建图的典型成本从全对全的 `O(N^2)` 降到 `O(N*b + M)`，其中 `b` 是相邻桶的平均候选数。极端高密度布局仍可能退化为 `O(N^2)`，但不会影响正确性。

## Bistable 加速内核

局部场、归一化变量和极化响应为

```text
H_i = sum_j(Ek_ij * P_j)
u_i = H_i / (2 * Gamma[clock_i, sample])
P_i = f(u_i)
```

`f` 完全保持原分段实现：大正值饱和到 `1`，大负值饱和到 `-1`，接近零时直接返回 `u`，其他情况使用 `u/sqrt(1+u*u)`。

加速版不改成 Jacobi，也不使用超松弛。给定随机种子产生的层内顺序被显式化为确定性调度表，基准与加速后端共享同一顺序。一个元胞更新后立即被本轮后续元胞读取，所以仍是 Gauss–Seidel 语义。

运行期渐近成本仍为 `O(T*I*M)`。收益来自：

- CSR 连续访问取代分散的元胞指针和多个小向量；
- 状态使用连续索引直接寻址；
- 热循环不再重建邻居和调度容器；
- 四相时钟直接查表。

因此论文需要分别报告图编译时间、纯仿真时间和端到端时间。小电路只运行一次时，编译成本可能抵消收益。

## Coherence-vector 加速内核

定义

```text
E_i   = sum_j(Ek_ij * P_j)
Omega = hypot(2*Gamma, E_i) / hbar
theta = tanh(hbar*Omega / (2*kB*T))
```

稳态状态为

```text
lambda_x_ss =  2*Gamma/(hbar*Omega) * theta
lambda_y_ss =  0
lambda_z_ss = -E_i/(hbar*Omega) * theta
```

动态阶段保持现有三个相干矢量微分方程及 `P_i=-lambda_z` 映射。对四个时钟区和每个时间样本预先缓存 `Gamma`、`2*Gamma` 及相关系数；同一元胞步中的三个斜率共享只依赖 `E_i`、`Gamma` 和温度的 `Omega`、`theta`。这减少昂贵数学函数的重复求值，但不改变积分方法或时间网格。

运行期渐近成本仍为 `O(T*s*M)`，其中 Euler 的 `s=1`，既有分量式 Runge–Kutta 路径的 `s=4`。这一路径是项目原有的 x/y/z 各分量阶段更新，并不是完整耦合 ODE 的标准 RK4；本文严格复刻现有语义，不宣称修正了积分方法。本文也不把自适应时间步混入“严格等价”版本；它可以作为另一个可控误差方向单独研究。

## 为什么可以证明等价

Bistable 可对“采样点、固定点轮次、调度位置”做字典序归纳：

1. 精确半径判断和稳定排序得到相同邻居集合与行内顺序。
2. 若此前状态相同，当前乘加的操作数和顺序相同，局部场与响应分支就相同。
3. 当前写回状态和稳定标志相同，下一元胞继续相同。
4. 因而每轮停止条件、每个采样点的迭代数和完整输出都相同。

Coherence-vector 可对时间步归纳：相同状态通过有序 CSR 产生相同耦合能，通过四相缓存取得相同时钟值，公共表达式只是复用同一函数值，所以相同 Euler 或既有分量式 Runge–Kutta 路径产生相同增量。

位级等价依赖相同 CPU、编译器、数学库和浮点选项。`-ffast-math`、FMA 收缩或并行规约重排可能破坏位级一致。跨平台实验应预先注册数值容差，不能看到结果后再改阈值。

## 未保存布局无需手工保存

布局布线结果首先存在于内存。旧流程把已有 `.qca` 路径当作唯一仿真输入，因此新建或修改后的布局必须先保存，否则会报错或误读旧文件。

GUI 统一入口应按以下流程工作：

```text
内存 QCADesign
  ├─ 文件存在且与内存一致：使用当前 .qca
  └─ 未保存或已修改：现有 QCA writer -> 唯一临时 .qca 快照
                                      -> 同一解析/仿真入口
                                      -> RAII 自动清理
```

临时快照不改变正式工程路径、脏标记或“另存为”语义。它只为当前仿真提供一致输入。回归测试应覆盖：新建未命名设计、刚完成布局布线、保存后再次修改、多层布局和自定义输入输出名称。写出失败必须显示明确错误，不能静默仿真旧文件。

## 当前验证状态

这些是开发阶段事实，不是最终论文统计：

| 对象 | 规模/配置 | 正确性 | 性能 |
|---|---|---|---|
| iFCN Bistable smoke | 36/36；256 samples；3+1 次 | 逻辑/置信一致率 1；MAE/RMSE/max = 0 | suite 2.050483x；geo 1.872999x，CI [1.821397, 1.932575] |
| iFCN Coherence smoke | 36/36；Euler 4096 步；3+1 次 | 逻辑/置信一致率 1；MAE/RMSE/max = 0 | suite 2.199192x；geo 2.187867x，CI [2.162699, 2.216032] |
| hfut-sim Bistable preliminary | 25/25；12800 samples；5+1 次 | 601600 输出样本、327922 稳定样本；全部误差 0 | suite 1.946164x；geo 1.809084x，CI [1.659344, 1.955664]；median 1.906435x |
| hfut-sim Coherence preliminary | 25/25；Euler 40960 步；5+1 次 | 148097 输出样本、93822 稳定样本；全部误差 0 | suite 2.256771x；geo 2.031323x，CI [1.884036, 2.167151]；median 2.133035x |
| 既有分量式 Runge–Kutta 交叉检查 | 6 个电路 | 位级不一致数 = 0 | Release 示例约 1.58–3.04x |
| 完整重复统计 | 固定 CPU、Release、至少 30 次 | `RESULT_PENDING` | `RESULT_PENDING` |

本轮共覆盖 61 个唯一 QCA 版图。环境为 GCC 13.3、`-O3` Release、Ryzen 9 8945HX；置信区间使用 5000 次 bootstrap。原始数据位于 `experiments/physical_ifcn_all36_smoke` 和 `experiments/physical_hfut_all25_preliminary`。

hfut Coherence 预实验使用 `time_step=1e-16`、`duration=4.096e-12`，即 40960 个内部步，而不是默认约 700000 步的长时配置。它用于等价回归和早期性能筛查，不能替代正式长时实验。正式实验还应报告元胞数、边数、样本数、平均固定点迭代数、图编译时间、纯仿真时间、端到端时间、峰值内存、中位数、IQR 和 95% 置信区间。Bistable 与 Coherence-vector 分表；Euler 与既有分量式 Runge–Kutta 路径也分表。

## SCI 创新点

1. 面向执行轨迹等价的稀疏物理编译：不只保证邻居集合相同，还恢复邻居、浮点累加和元胞更新顺序。
2. 一个有序 CSR 物理图服务 Bistable 与 Coherence-vector 两个后端，各自数值语义不变。
3. QCA 四相时钟缓存与相干矢量公共核融合，减少动态仿真中的超越函数开销。
4. 正确性门控的基准方法：逐位/逐样本验证先于计时，拒绝用视觉相似代替物理一致性。
5. 通过临时 QCA 快照打通布局布线与仿真，无需用户手工保存。

建议消融顺序为：空间桶；空间桶加有序 CSR；再加确定性调度；再加四相缓存；最后加入 Coherence 公共核融合。

## 局限

- 小电路或单次运行可能无法摊薄编译成本。
- 目前不改变时间步或跳过事件，Coherence-vector 的渐近时间复杂度没有降低。
- Bistable 为保持 Gauss–Seidel 轨迹，不能直接并行更新相邻元胞。
- 几何、作用半径、层间距或介电参数变化后必须重新编译图。
- 61 个布局仍是开发验证；至少 30 次重复、默认长时 Coherence、完整消融和复现包仍为 `RESULT_PENDING`。

## English abstract candidate

> Bistable and coherence-vector simulation of quantum-dot cellular automata (QCA) repeatedly traverses a radius-limited electrostatic interaction graph, while conventional implementations incur quadratic graph construction, pointer-heavy neighborhood access, and redundant clock and transcendental-function evaluations. This paper presents a strictly equivalent sparse-compiled acceleration framework for both physical engines. An order-preserving spatial-bucket compiler enumerates exactly the cell pairs admitted by the original three-dimensional radius test and stores their kink energies in a contiguous compressed sparse row representation. The bistable backend retains the deterministic Gauss–Seidel schedule, nonlinear polarization response, and convergence criterion. The coherence-vector backend retains steady-state initialization and the selected Euler or existing component-wise Runge–Kutta path, while caching the four clock phases and fusing common `hypot` and `tanh` expressions. We establish trace equivalence by induction over neighbor accumulation, cell-update order, fixed-point iterations, and time samples. Development tests cover 61 unique QCA layouts with zero logic and numerical errors. On the 25-circuit hfut-sim preliminary protocol, the bistable and Euler coherence-vector suite speedups are 1.9462x and 2.2568x, respectively. The coherence experiment uses a shortened non-default 40,960-step protocol; at least 30 repeated runs and the default long-duration study remain `RESULT_PENDING`. An automatic temporary-QCA snapshot also enables direct simulation of unsaved routed layouts without changing project-file semantics.
