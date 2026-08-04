# iFCN 开发交接

更新时间：2026-07-23  
分支：`version1.1`  
基准提交：`44fd211`  
状态：工作区有大量尚未提交的代码、实验产物和论文文件，接手前不要执行 `git reset`、`git clean` 或覆盖生成目录。

## 1. 当前目标

工程现在需要维护两套严格隔离的布局布线模型：

1. **固定时钟 2DDWave：Normal Graph Draw**
   - 不使用 GCN；OGDF 只负责固定逻辑层内的交叉排序。
   - 当前只支持 TOY/AOIG 一类非择多门网表，**不支持原生 MAJ（majority/择多门）网表**；不要把 `tests/benchmarks_f/MAJ/` 下的文件直接送入 2DDWave，也不要把这种输入不受支持导致的失败归因于布线器。
   - 连线只允许向右或向下。
   - 同一逻辑门的多个扇入必须使用不同输入方向（上/左）。
   - 同一源的扇出树必须从同一输出方向（右/下）离开。
   - 最终 LaTeX 相位图必须覆盖版图矩形中的全部 2DDWave 网格。

2. **随机时钟：Stochastic Compact Graph P&R**
   - 同样不依赖 GCN，但不能复用固定 2DDWave 的单向曼哈顿路由器。
   - 该流程用于 MAJ/择多门版本网表，并原生处理 majority gate。
   - 使用四方向迷宫搜索，并在搜索状态中同步分配相位。
   - 压缩时必须重新布线并重新验证相位和端口方向。
   - 该模型不得改变或干扰固定 2DDWave 流程。
   - 主 UI 只展示 `Heuristic P&R`、`2DDWave Fixed-Clock P&R` 和 `Compact Graph Draw P&R` 三个布局布线入口；June restore、Legacy Graphviz preview 与 GCN+RL 后端代码仍保留用于历史回归，但不再出现在 UI。

此外，主界面提供一套独立的、面向任意已加载 cell-level 版图的 **IO Contract** 可逆预览算法。

## 2. 不可破坏的物理约束

- 一个门的两个扇入不能从同一方向进入。
- 一个多扇出源的分支应共享同一扇出端口/主干，不能分别占用相反方向。
- 交叉线边缘的第 1 层（layer 0）元胞可以作为 IO 收缩终点。
- 只允许显式支持的两网 L1/L3 交叉；门端点重叠、三网共点和非法平行重叠均不能被接受。
- 随机时钟路径的相位必须保持“相同或向前一个相位”的传播关系，并满足最大连续同相位长度。
- 失败或只完成部分布线的结果不得导出成合法 `.ifcn`、`.qca` 或相位 LaTeX。
- UI 中的 Occupied Area 使用 **5×5 clock grid 的跨度**计算：`W × H`；W/H 不是 cell 数量。
- IO 收缩只是勾选预览：取消勾选恢复原版图；优化结果必须另存为新的 `.qca`，不能覆盖源文件。

## 3. 已完成内容

### 3.1 固定 2DDWave / Normal Graph Draw

- 默认层内排序改为 OGDF Sugiyama/Barycenter 外部适配器；找不到 OGDF 可执行文件时自动退回确定性 barycenter 排序。
- 引入 bounded L/Z candidate + negotiated congestion 的单向曼哈顿路由器。
- 小中型图自动使用 negotiated router；超过 128 条有效边时仍使用原有 direct/DP 后端，避免大图候选爆炸。
- 补充扇入/扇出端口方向约束和最终端口验证。
- 支持两网交叉层映射，禁止三网交叉和端点交叉。
- 同源 fanout 分支可以共享连续的树状元胞主干，但分叉后不能再次汇合；异源重叠只允许一个局部 H/V 正交直穿点。异源平行重叠、任一方在交点转弯、同一 source pair 多次交叉均为硬冲突。
- 导出前同时检查布线完整性、端口方向和完整 2DDWave 模板；检查失败时拒绝保存非法结果。
- Normal Graph UI 已明确标为“2DDWave Fixed-Clock P&R”，并显示固定时钟、排序器、路由后端和导出选项。

关键文件：

- `include/gcn_rl_layout/src/algorithm/src/normalGraphDraw.py`
- `include/gcn_rl_layout/src/algorithm/src/negotiated_router.py`
- `include/gcn_rl_layout/src/chessboard/RightDownAStar.cpp`
- `include/gcn_rl_layout/src/chessboard/RightDownAStar.h`
- `include/gcn_rl_layout/src/chessboard/MapChessboard.hpp`
- `include/gcn_rl_layout/src/algorithm/main/test_normal_graph_draw.py`
- `include/gcn_rl_layout/docs/negotiated_2ddwave_router.md`
- `src/controllers/GateLevelMapping.cpp`

#### 2026-07-21 `par_gen` 交叉线严重映射错误

- 已复现旧输出：`b→w4` 与 `a→w6` 在 gate-grid `(1,1)~(1,3)` 连续异源共线重叠；这不是同源扇出共享。
- Python negotiated router 原先对“恰好两个 source tree”无条件判合法，虽然已经计算 H/V/B orientation 却没有使用。C++ `crossline_mapping()` 随后把桥接后的整个 unit segment 抬到 L3，生成了 12-cell、跨两个 5×5 tile、带两次转向的错误交叉层。
- producer 已收紧为：同源连续公共前缀共享继续合法，但 fanout 分叉后重汇会作为 `fanout-reconvergence` 冲突整树 rip-up；两源必须分别是纯 H 与纯 V，并且同一 source pair 最多相交一次。平行段、bend、混合方向和三源重叠进入 negotiated conflict/rip-up，不再导出“假合法” IFCN。
- `NormalGraphDraw.validate_route_overlap_legality()` 在每次完整布线后独立复核 route ownership，因此 legacy/direct-DP 后端也不能绕过相同契约。
- C++ mapper 新增 `Mapping::validate_crossovers()` 最终防线。每段交叉层必须限制在单个 5×5 tile 内、元胞唯一且 4-connected，最多一次转向并在 tile 边界结束；旧坏 IFCN 会被明确拒绝，不能再静默显示错误版图。单 tile 内旧版 7-cell L 结构仍兼容。
- 修复后 `TOY/par_gen.v`、seed 1：16/16 routes、port/template 全通过；5 段 crossover 全是单 tile 内的 5-cell 直线；mapping 为 339 cells（未收缩）/330 cells（IO contraction），面积从旧假合法的 `7×10=70` 变为严格合法的 `8×10=80`，线长仍为 66。
- 修复输出仍实际共享 4 个同源 fanout 主干元胞：gate-grid `(0,1)`、`(2,1)`、`(4,1)`、`(6,6)`；这些共享不会进入 crossover layer。剩余 5 个局部交叉点均来自不同 source tree，不能通过共用元胞替代，否则会把两个逻辑信号物理短接。
- 代表性验证 `xor2`、`mux41`、`1bitAdderAOIG` 继续严格通过。`RCA2` 在新规则下会拒绝旧流程依赖的非法 overlap，当前 seed 1/2 尚找不到完整严格合法结果；应改进 placement/track capacity，不能重新放宽 crossover 规则。
- 旧生成目录中的 `par_gen_normal_graph_draw.ifcn` 不会自动改写，必须重新运行 Normal Graph Draw 才能得到修复后的版图。
- rejected compaction cut 现在同时回滚 `last_negotiated_metrics/conflicts/failed_pairs`；summary 不再把一次失败的试探性 reroute（例如 15/16）误报成最终已恢复的 16/16 布线状态。

新增回归：

- `include/gcn_rl_layout/tests/test_negotiated_router.py`
- `tests/MappingCrossoverUnitTest.cpp`

### 3.2 OGDF 适配器

- OGDF 仅作为独立进程返回固定层内的节点顺序，不链接到主 GUI 或 Python 扩展。
- 固定种子、8 次 barycenter run、transpose 开启，结果可复现。
- 环境变量 `IFCN_OGDF_ORDERER` 可以显式指定可执行文件。
- 当前主 `build` 中 `IFCN_BUILD_OGDF_ORDERER=OFF`；现有适配器位于 `build-ogdf/ifcn_ogdf_layer_order`。
- OGDF 是 GPL-2.0-or-later/GPL-3.0-or-later，分发适配器二进制时必须遵守其许可证；不要把它误写成 MIT 依赖。

关键文件：

- `include/gcn_rl_layout/ogdf/ogdf_layer_order.cpp`
- `include/gcn_rl_layout/ogdf/CMakeLists.txt`
- `include/gcn_rl_layout/ogdf/README.md`
- `include/gcn_rl_layout/scripts/compare_crossing_orderers.py`

### 3.3 随机时钟 Compact Graph

- 新建与固定 2DDWave `Astar` 相互独立的 `PhaseAwareAstar`。
- 路由状态包含坐标、相位和连续同相位计数；支持四方向绕行。
- 已加入扇入方向冲突检查、扇出主干复用、节点避让和路径相位提交。
- `CircuitGraph::sortNodesByFixedLayerOrder()` 接收 OGDF/barycenter 的固定层顺序。
- `CircuitGraph::placeAndRoutePhaseAware()` 同时完成布线和相位分配：先做端口可达性预检，搜索范围与状态数有界，失败边在下一轮提升优先级，前两轮使用确定性边序，之后只做固定种子的有限边序扰动，不再在每个格点产生指数级 phase 分支。
- 确定性相位布线每轮只从该策略指定的起始相位展开；不同路由轮次会旋转起始相位，小图 flexible compaction 仍保留全相位种子。`PhaseSolver` 按变量约束度选择分支，每次赋值只检查该变量相关约束，不再每层 DFS 节点全量扫描。
- source ownership 已进入 A* 搜索状态，而不是等全部线路完成后才发现错误：同源 fanout 可以复用连续树干；异源共点必须是直 H/直 V crossover，平行重叠、bend 和三源共点在搜索时即被拒绝。相同两棵 source tree 可以在不同坐标形成多个独立合法 crossover，但同一路径不能形成连续 overlap。
- 四方向 legacy A* 不再“节点首次到达即永久锁定”：open set 中的坐标允许被更低 `gScore` 松弛并更新父节点；同源复用主干的下一格由 hash 表直接查询，不再在 A* 热路径中反复线性扫描。六种确定性路由与后续固定种子扰动都会把上轮失败边提到前面。
- `CircuitGraph::compactPhaseAware()` 对候选横/纵切线移动门坐标，完整重布线后才接受面积更优的结果；后压缩现在有 `2.5 s / 8 cuts` 独立预算，普通图只跑确定性重布线，小图才启用有界 flexible-phase 补充，因此不会在不可压缩电路上停顿几十秒。
- 候选调度现在先跑 Graphviz 紧凑种子，再仅在固定层候选的理论下界可能更小时跑固定层候选。Graphviz 坐标会除以量化值，旧的小图调度 `/64 → … → /40` 实际是在逐步放大布局；有效边 `<=24` 的小图现改为 compact-first 调度。浅层小图从 `/96` 向下寻找端口可布的最紧尺度；深层 MAJ 小图保留 X 通道，并按 4 相位周期的倍数单独压缩 Y；窄而深的链式小图使用独立候选，避免先运行一串必失败的超紧 X 布局。所有候选都事务式重布线、重新分配四相并通过 source/crossover/IO 检查后才可替换当前最优解。
- `CircuitGraph::placeAndRouteJuneRandomClockAnisotropic()` 支持 Graphviz X/Y 独立量化。测量表明大部分版图面积来自初始门节点包围盒，A* 通常只在其外侧增加 1–2 列；因此 X/Y 独立压缩比继续提高全局 A* 预算更有效。一般稀疏图仍细扫合法量化，高拥塞图直接尝试已验证的 `X/44,Y/36` 等候选。
- 四相周期压缩有两层保护：候选 Y 量化只按一个完整周期的倍数收紧；`compactClockPhaseCycles()` 可事务式尝试删除无节点的整周期带，失败时恢复节点、线路和网格。当前代表性结果主要来自前者，后者在已紧凑样例上没有发现可继续删除的完整空带。
- Graphviz 候选循环现在无论是否已经找到合法种子都会检查总时间预算，避免一批非法紧凑尺度让 UI 长时间停顿。
- DOT 坐标网格化现在先按原始 Graphviz X 顺序处理同行节点，再用 occupancy set 解决量化碰撞；修复了旧的“按 node id 向右推”会颠倒层内次序并制造额外交叉的问题，同时将碰撞检查从 `O(V²)` 降到 `O(V log V)`。
- 紧凑候选的合法 crossover 不再因 `maxSearchCost>100` 被隐式加 6 的绕路罚分；交叉合法性仍由 source ownership 硬约束保证，但 A* 会优先保留短路径和紧凑外边界。高拥塞拓扑当前可在 `X/44,Y/36` 下完成，代表性面积为 `RCA2=1462`、`MAJ/xor5R=1682`；所有紧凑拓扑失败且图又深又宽时，才恢复七月版层间 buffer topology。
- 固定层与 Graphviz 两类布局都统一 IO 约定：primary input 在保存后的上边界，primary output 在下边界；冲突位置保持原水平顺序并确定性错开，与另外两个公开算法使用相同的 IFCN/cell-mapping 输出链。
- 远程遗留的 `TOY/xor2_graph_pr_layout` 虽记录 `5x5=25`，但两棵异源树在 `(2,0)`、`(2,1)` 连续重叠并转弯，不是合法 crossover。当前流程不会为复现该面积而重新接受短接结构；严格回归路径为 `250 -> 150`（测试包含可行性锚点的占用边界，UI 还会继续搜索紧凑候选）。
- 主界面默认 P&R 入口为 `Compact Graph Draw P&R (recommended)`；下拉菜单和 Verilog Source → Generate 均只展示 Heuristic、2DDWave、Compact Graph Draw 三项。

关键文件：

- `include/autopr/algorithms/astarwithphase.cpp`
- `include/autopr/algorithms/astarwithphase.h`
- `include/autopr/graph/circuitGraph.cpp`
- `include/autopr/graph/circuitGraph.h`
- `src/controllers/VerilogHandler.cpp`，重点关注 `fixedLayerCrossingOrder()`、`runGraphRenderForFile()`
- `tests/PhaseAwareRoutingUnitTest.cpp`
- `tests/StochasticCompactGraphUnitTest.cpp`

### 3.4 2025-06 Legacy Graphviz Graph Draw 预览

- 已确认用户记忆中的快速旧版是 `e727765`（2025-06-16）；7 月前的 merge `e068770` 与它的相关布局绘图源码相同。
- Legacy Graphviz 预览的实现和测试仍保留，但按当前 UI 精简要求不再提供菜单或 Verilog Source → Generate 入口。
- 预览复现旧版 `dot` 绘图条件：parser 逻辑层使用 `rank=same`、`rankdir=TB`、`nodesep=.6`、`ranksep=1`、正交逻辑边，并保留旧流程的 buffer 优化和跨层 redundancy node 拆分。
- Graphviz 在后台线程执行，该预览渲染器的进程内调用由全局 mutex 串行化；渲染器使用 RAII 清理 `GVC_t`、graph 和 layout，空图、非法端点、布局/渲染失败均返回错误，不再沿用旧版空图崩溃和固定写 `<input>.v.svg` 的行为。
- SVG 只在内存中生成，默认不落盘；预览窗支持 Fit、缩放、拖拽和用户显式 `Save SVG…`。
- 该窗口展示的是 **Graphviz 逻辑拓扑边**，不是旧 Morton/A* 的物理线路，也不会清空/修改当前 cell-level scene、写 `.ifcn` 或宣称 IO/相位/cell mapping 合法。

关键文件：

- `include/autopr/graph/legacyGraphvizRenderer.h`
- `include/autopr/graph/legacyGraphvizRenderer.cpp`
- `src/controllers/VerilogHandler.cpp`，重点关注 `runLegacyGraphvizGraphDrawForFile()`
- `src/ui/mainwindow/MainWindow.MenuToolbar.cpp`
- `src/ui/mainwindow/MainWindow.ViewScene.cpp`
- `tests/LegacyGraphvizRendererUnitTest.cpp`

### 3.5 2025-06 Random-Clock Graph P&R（物理布局布线）

- 历史实现定位到 `e727765`（2025-06-16；最终六月 merge 快照为 `e068770`）。旧 README 将其描述为 `Random Clocking / Irregular / Larger Area / Faster Routing / Graph-drawing-based placement`。
- 进一步核对后，用户记忆中的“七月版 Graph Draw”对应 `0c6cc1e`（2025-07-14，提交标题 `fix graphdraw alogorithm bugs`）。它沿用六月 Graphviz + Morton/A* 主链，但在 UI 的 Graph Draw 入口明确注释掉 `optimizeBufferNode()`；恢复入口现在也保持这个七月行为，以免改变节点层次和面积/布线特性。
- 独立算法链为：`Graphviz dot` 分层布局 → 六月式坐标网格化（首选 `/40`）→ irregular-clock 四方向 A* → 路由后 4 相位分配 → IFCN 保存与元胞映射。它不是只画逻辑边的 Legacy Graphviz 预览。
- 当前实现明确标为 **hardened restore / June-derived**，不是逐行复刻：节点网格化会处理坐标碰撞，先试 6 种确定性边顺序并进行有界的固定种子顺序重试，也禁止线路穿过中间门节点。这些改动用于避免六月原版静默生成短接结果。
- A* 的严格模式只允许显式同源 fanout 主干复用。异源占用只有在单个元胞内分别为直 H 与直 V 时才允许形成 crossover；连续共线、任一方转弯和三源共点都会在搜索阶段被拒绝。相同 source pair 的多个空间独立正交 crossover 合法，但同一路径上的重复/连续占用仍被拒绝。
- `CircuitGraph::placeAndRouteJuneRandomClock()` 在映射前再次按逻辑 source 身份验证：路径唯一且 4-connected；同源扇出分叉后不能重汇；异源共享点只能是 H/V 正交。不能再只依赖丢失 source 身份后的 `Mapping::validate_crossovers()`。
- 相位继续使用当前确定性、有界的 `PhaseSolver`，没有恢复六月原版最多 1,000,000 次的随机抽样循环。因此这里的“随机时钟”指 irregular/random clock field，而不是不稳定的无界随机求解。
- UI 候选为两档：原始尺度 `/40, cost=40, shuffled retries=4`；严格检查失败时使用 `/20, cost=320, retries=24` 的扩距 fallback。首档保留旧算法速度，第二档为严格合法性提供绕线空间；两档失败就明确报错，不导出部分结果。
- Graphviz、A* 与相位求解现在线程外执行；GUI 保持响应，运行期间 Place & Route 菜单和 Verilog Source 的 Generate 按钮会禁用，避免重入。
- Verilog Source 选择当前标签替换时采用事务式语义：June 设置取消、后台搜索失败或保存前验证失败不会预先清空旧版图；只有生成 IFCN 并成功进入映射流程后才替换。
- `GateLevelMapping::mappingCellItem()`、`parseGateLevelMappingFile()` 和 `MainWindow::mapIfcnFile()` 现在逐层返回成功状态；June 只有在真实 cell mapping 成功后才导出最终 SVG并弹出“完成”。
- `saveGateLevelIfcn()` 在 hidden-NOT 恢复后重复执行 crossover guard，任何未恢复 NOT route 都拒绝保存；文件通过 `QSaveFile` 原子提交，写入或 phase codec 失败不会破坏已有合法结果。
- June hardened restore 的控制器、输出链和测试仍保留，但按当前 UI 精简要求不再公开入口。若以后恢复入口，其输出目录仍为 `<stem>_june_random_clock_graph_pr/`。
- 自动回归覆盖 `TOY/xor2.v` 主路径和 `MAJ/clpl.v` fallback，并独立检查逻辑 source crossing contract、4 相位传播、route 数、节点数和 mapper crossover。大规模 IWLS 图与少数高拥塞 MAJ 图仍可能在有界候选内失败；失败时应改善 placement/track capacity，不能重新放宽交叉规则。

关键文件：

- `include/autopr/algorithms/astar.cpp`
- `include/autopr/algorithms/astar.h`
- `include/autopr/graph/circuitGraph.cpp`
- `include/autopr/graph/circuitGraph.h`
- `src/controllers/GateLevelMapping.cpp`
- `src/controllers/GateLevelMapping.h`
- `src/controllers/VerilogHandler.cpp`
- `src/controllers/VerilogHandler.h`
- `src/ui/mainwindow/MainWindow.MenuToolbar.cpp`
- `src/ui/mainwindow/MainWindow.ViewScene.cpp`
- `tests/JuneRandomClockGraphUnitTest.cpp`
- `tests/JuneRandomClockFallbackTest.cmake`
### 3.6 Cell-level IO 收缩与后压缩

- 主工具栏加入 `IO Contract` 勾选框，Tools 菜单中有同步 action；不再放在布局布线参数页中。
- 支持普通 scene 和 fast-render scene，勾选后保存完整快照，取消勾选原样恢复。
- IO 沿私有、无分支 wire stem 收缩到 5×5 网格边缘中点。
- layer-0 crossover 边缘可作为合法 IO；不再需要的 crossover 会删除，必要时把仍有效的上层线路安全降到 layer 0。
- IO 收缩后继续执行：U 形/矩形冗余线压缩、支路滑动、横纵 5×5 strip 压缩、单格偏移扇出结点居中。
- 算法多轮执行到稳定状态，因此删除交叉后新暴露的 IO stem 仍可继续收缩。
- 右下角信息面板会更新 cell 数、IO、交叉元胞、压缩行列、W/H grid span 和 `W×H` area。
- 保存逻辑强制 Save As，目标应为新的 `.qca` 文件。
- 支持无界面导出版图，供 CI 和论文图复现。

关键文件：

- `src/controllers/CellLevelIoContraction.cpp`
- `src/controllers/CellLevelIoContraction.h`
- `src/ui/mainwindow/MainWindow.IoContraction.cpp`
- `src/ui/mainwindow/MainWindow.Logic.cpp`
- `src/ui/mainwindow/MainWindow.Status.cpp`
- `src/ui/mainwindow/MainWindow.MenuToolbar.cpp`
- `src/ui/view/QCADScene.cpp`
- `tests/CellLevelIoContractionUnitTest.cpp`
- `tests/MappingIoContractionUnitTest.cpp`

### 3.7 其他 UI 与输入修复

- 左下角运行状态从无限循环改为单调前进式进度，未知进度最多自动前进到 92%。
- 状态栏识别真实存在的文件路径；按住 Ctrl 并左键点击路径会打开其所在目录。
- `.v` 解析、parser-safe AOIG 重写、非法/悬空节点处理和部分 TOY benchmark 已修正。
- 新增 cell-level PDF/SVG 精确裁剪导出以及以下无界面环境变量：

  - `IFCN_AUTO_MAP_FILE`
  - `IFCN_AUTO_EXPORT_CELL_LAYOUT`
  - `IFCN_AUTO_CONTRACT_IO=1`
  - `IFCN_AUTO_RESTORE_IO=1`
  - `IFCN_UI_SCREENSHOT`

## 4. 当前验证结果

2026-07-23 在当前工作区执行：

```bash
ctest --test-dir build --output-on-failure
```

结果：**22/22 通过**，总时间约 51.4 秒，包括 negotiated crossover legality、mapping crossover structure、simulation metrics、两套 IO contraction、phase-aware routing、Compact Graph 主路径/Graphviz fallback/buffered fallback/小图面积阈值、Legacy Graphviz renderer、June random-clock Graph P&R 主路径与 fallback、physical smoke 和两种加速仿真等价性测试。

另外对当前 `tests/benchmarks_f/MAJ/` 与 `tests/benchmarks_f/TOY/` 顶层的 **27/27** 个 Verilog 文件做了完整 Graph Draw 集成回归，全部通过路由、source ownership/crossover、IO 边界和相位检查。代表性单进程时间：`TOY/xor2 0.035 s`、`MAJ/1bitAdderMaj 0.72 s`、`TOY/mux41 1.09 s`、`MAJ/clpl 2.69 s`、`MAJ/RCA2 6.54 s`、`MAJ/xor5R 7.65 s`；buffered xor5R 保底回归约 16.1 秒。

额外直接调用了 Python 测试文件中的全部 `test_*` 函数：

- `include/gcn_rl_layout/tests/test_negotiated_router.py`
- `include/gcn_rl_layout/tests/test_right_down_port_directions.py`

结果：**15/15 通过**。系统当前没有安装 `pytest`，所以 `python3 -m pytest ...` 会报 `No module named pytest`；`test_negotiated_router.py` 现在也可直接由 Python 或 CTest 执行。

`git diff --check` 当前无 whitespace error。

新 UI 多尺度调度中的代表性严格合法候选如下（每个尺度都通过完整路由、相位、IO 和 crossover 验证）：

| 输入 | Graphviz 量化 | occupied-grid 面积 |
|---|---:|---:|
| `MAJ/1bitAdderMaj.v` | `X/88,Y/88` | 42 |
| `MAJ/xor2.v` | `X/52,Y/72` | 72 |
| `MAJ/xnor2.v` | `X/56,Y/72` | 84 |
| `TOY/mux21.v` | `X/96,Y/96` | 20 |
| `TOY/xnor2.v` | `X/40,Y/68` | 50 |
| `TOY/1bitAdderAOIG.v` | `X/64,Y/64` | 66 |
| `MAJ/clpl.v` | `X/64,Y/54` | 672 |
| `MAJ/par_gen.v` | `X/46,Y/50` | 325 |
| `MAJ/newtag.v` | `X/64,Y/62` | 315 |
| `TOY/mux41.v` | `X/48,Y/46` | 169 |
| `MAJ/RCA2.v` | `X/44,Y/36` | 1462 |
| `MAJ/xor5R.v` | `X/44,Y/36` | 1682 |

多尺度调度会在时间预算内比较合法候选，因此 UI 最终值只会保留同次搜索中更好的结果。这些数字仍不代表已全面优于旧算法，尤其 `RCA2/xor5` 仍需要更强的拥塞协商与局部压缩。

## 5. 仍需继续优化：困难 fallback 的面积

### 与 2DDWave 的比较口径

- 由于固定 2DDWave 当前不支持原生择多门，不能对同一份 MAJ 网表直接运行两种流程。
- 正确的系统级对照是：**MAJ 版本运行随机时钟 P&R，功能等价的 TOY/AOIG 版本运行固定 2DDWave P&R**。
- 这属于“功能等价、门库与网表不等价”的跨流程比较，不能表述成同网表算法对照。每次报告必须同时列出输入文件、门类型/逻辑基、优化后的节点数与边数，以及两边的完整布线、端口、相位/时钟模板 legality。
- 2DDWave 的严格合法性测试应使用 `tests/benchmarks_f/TOY/` 中对应电路；不要用 `tests/benchmarks_f/MAJ/` 的输入测试 2DDWave。

用户指出“以前的算法没有这么大”是正确的。工程中已有结果与当前结果的对照显示回退具有明显的电路相关性：

| 电路 | 旧产物面积 | 当前严格合法候选 | 结论 |
|---|---:|---:|---|
| xor2 | 65 | 72 | 已从 150、96 继续降到 72，接近旧面积 |
| mux41 | 814 | 169 | 当前更优 |
| 1bitAdderAOIG | 168 | 66 | 当前更优 |
| RCA2 | 750 | 1462 | 已从 5382、2112 继续下降，仍有回退 |
| 1bitAdderMaj | 36 | 42 | 已从 462、48 继续下降，接近旧面积 |
| xnor2（旧 Graph P&R） | 35 | 84 | 已从 161、102 继续下降，仍有差距 |

旧产物是否全部满足现在新增的严格端口检查尚未逐个复核，因此比较时必须同时报告 legality；但 `1bitAdderMaj` 和 `xnor2` 的差距过大，不能只归因于约束变严格。

### 已定位的原因

1. 多尺度 Graphviz 已取代固定层刚性大矩形作为首选，但 DOT 量化仍是全局尺度，没有逐层/逐通道的局部坐标优化。
2. `PhaseAwareAstar::isPassable()` 为每个无关门的四周设置一整圈硬 halo；这保证端口可达，但浪费了大量本可利用的布线资源。
3. `placeAndRoutePhaseAware()` 先取全图最长边生成一个全局 `effectiveSearchCost`，导致短边也能大范围绕路并撑出版图边界。
4. 早期布线提交的相位成为后续路径的硬约束，目前没有 source-tree 级 rip-up/re-route；后续网络倾向绕行而不是协商相位与拥塞。
5. `compactPhaseAware()` 已加短预算和事务回滚，解决了长停顿，但 Graphviz/buffered 结果当前不做 strip deletion，因此高拥塞成功结果仍会留下较大面积。
6. OGDF 只优化层内顺序，不会自动产生紧凑坐标；不能把“交叉数减少”等同于“面积减少”。

### 推荐修复顺序

1. **预先分配实际端口。** 对每个两扇入门做上/左二分匹配，对每个 fanout tree 选择一个共享右/下端口；只保护被分配的端口单元，不再保护门四周全部四个邻居。
2. **把相位感知搜索预算改成逐网局部预算。** 使用 `Manhattan distance + local congestion/phase slack`，不让最长边决定所有短网的搜索范围。
3. **实现 source-tree negotiated routing。** 冲突时 rip-up 同一源的整棵扇出树，更新历史拥塞，再重布线；不能逐分支独立破坏共享扇出方向。
4. **实现 Graphviz 结果的 phase-aware strip deletion。** 优先删除纯布线空行/空列，只重布受影响 source trees；候选必须事务化，完整 legality 通过后才能提交。
5. **加入面积回退保护。** 对每个 benchmark 保存“最小严格合法基线”；新候选面积更差时保留旧候选并在 summary 中报告 regression。

完成上述步骤时，不能删除现有 `PhaseAwareAstar` 的相位验证，也不能把固定 2DDWave router 接到随机时钟流程中。

## 6. 大规模电路现状

- `c432` 在当前有界随机时钟搜索下仍不能稳定完成合法布局布线；失败结果不会保存，这是正确行为。
- 大图仅尝试少量 spacing，路由内部尝试 6 种相位/边序偏移，且关闭后压缩。该策略控制了时间，但没有解决拥塞死锁。
- 固定 2DDWave 侧已有 negotiated router 设计文档，但 >128 edges 仍回退 direct/DP；大规模 sparse track assignment 尚未完成。
- 下一阶段应优先实现 congestion hot region、source-tree rip-up 和局部行列插入，而不是提高 A* 全局预算或恢复 GCN。

## 7. 论文状态

`paper/` 目录目前整体未被 Git 跟踪，包含 `main.tex`、`rebuttal.tex`、表格、数据、Figure 10 和编译产物，约 32 MB。

当前 `paper/main.tex` 仍把 GCN 描述为 inherited initializer/ranker，并引用旧的 corrected 17-circuit campaign；这与最新“固定和随机两套流程均去 GCN”的工程方向尚未完全同步。继续投稿前至少需要：

- 把新随机时钟 non-GCN Compact Graph 与固定 2DDWave Normal Graph 明确区分；
- 重新生成严格端口合法、相位合法的实验数据；
- 不把 archived large data 当作 port-certified 证据；
- 更新 Figure 10、算法伪码和实验曲线中的算法名称；
- 继续满足正文不含参考文献最多 6 页的限制。

不要直接提交 LaTeX 的 `.aux/.log/.fls/.fdb_latexmk/.synctex.gz` 等编译中间文件。

## 8. 构建与复现命令

### 主工程

```bash
cmake -S . -B build \
  -DIFCN_BUILD_TESTS=ON \
  -DIFCN_BUILD_GCN_RL_BINDINGS=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

GUI：

```bash
./build/fcnx_gui
```

### OGDF 外部排序器

使用已有 OGDF 源码：

```bash
cmake -S include/gcn_rl_layout/ogdf -B build-ogdf \
  -DOGDF_SOURCE_DIR=/path/to/ogdf \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ogdf -j2 --target ifcn_ogdf_layer_order
```

或设置：

```bash
export IFCN_OGDF_ORDERER=/absolute/path/to/ifcn_ogdf_layer_order
```

### 随机时钟快速回归

```bash
./build/stochastic_compact_graph_tests tests/benchmarks_f/TOY/xor2.v
./build/stochastic_compact_graph_tests tests/benchmarks_f/TOY/RCA2.v
./build/phase_aware_routing_tests

# 2025-06 Graphviz + A* 快速 P&R 主路径与 clpl fallback
./build/june_random_clock_graph_tests tests/benchmarks_f/TOY/xor2.v 40 40
ctest --test-dir build --output-on-failure \
  -R 'june_random_clock_graph_(regression|fallback_regression)'
```

### 固定 2DDWave 快速回归

```bash
python3 include/gcn_rl_layout/src/algorithm/main/test_normal_graph_draw.py \
  --benchmark tests/benchmarks_f/TOY/xor2.v \
  --crossing-orderer ogdf \
  --router auto \
  --skip-figures \
  --skip-latex \
  --skip-stage-snapshots
```

### 无界面 cell-level 导出

```bash
QT_QPA_PLATFORM=offscreen \
IFCN_AUTO_MAP_FILE=/absolute/input.ifcn \
IFCN_AUTO_EXPORT_CELL_LAYOUT=/absolute/output.pdf \
IFCN_AUTO_CONTRACT_IO=1 \
./build/fcnx_gui
```

## 9. 工作区清理与提交注意事项

- 当前修改跨越 C++、Python、UI、测试、benchmark 产物和论文；建议按功能拆分提交，不要一次性提交全部工作区。
- `include/gcn_rl_layout/src/algorithm/src/__pycache__/` 中已有被修改的 `.pyc`，应从功能提交中排除。
- `seed_1/`、大部分 `*_normal_graph_draw/`、`stage_tex/`、PDF/SVG/LaTeX 编译产物属于运行输出，提交前逐项确认。
- 一些 benchmark `.ifcn` 和 summary JSON 已被测试重写；除非明确要更新 golden data，否则不要顺手提交。
- `paper/` 当前未跟踪，添加前先建立适当 `.gitignore` 并确认版权/数据来源。
- 不要删除 legacy GCN+RL 的底层代码和复现实验文件；当前要求是移除 UI 入口，只保留三种公开布局布线算法。

## 10. 下一位开发者的验收标准

随机时钟面积修复完成至少应满足：

1. 现有 22 个 CTest、Python 路由测试和当前 MAJ/TOY 顶层 27 文件集成回归继续通过；
2. xor2、mux41、RCA2、1bitAdderMaj、1bitAdderAOIG、xnor2、xor5R 全部通过完整布线、IO 边界和相位检查；
3. RCA2、1bitAdderMaj、xnor2 的面积明显下降，并与逐个复核为合法的旧基线比较；
4. 任一压缩候选失败时完整回滚，不能污染最后一个合法布局；
5. c432 在规定时间预算内输出合法结果或明确的失败诊断，不能无限运行；
6. 固定 2DDWave Normal Graph 的输出和测试结果不发生回退；
7. GUI 仍能勾选/取消 IO Contract，右下角显示 grid `W×H`，且保存时强制新 `.qca`。
