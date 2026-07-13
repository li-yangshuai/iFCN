# 通用随机时钟 GCN+RL：工程审计、实现与接入状态

> 2026-07 更新：文档后半部分保留了第一阶段审计背景。当前实现已经加入 IFCN
> 离线学习、拓扑检索记忆、GRU recurrent policy、learned route priorities、
> clock-aligned start、严格随机时钟 exact 验证，以及桌面 GUI 推理/导出入口
> `scripts/gui_universal_agent_runner.py`。旧版逐电路 PPO 仍作为 legacy baseline。

## 结论

旧主线“单电路 GCN warm start + 定长 MLP-PPO 局部压缩 + 相位感知启发式布线”
现在仅作为 baseline。主线已经是跨电路动态图策略、持久检索记忆和随机时钟 exact
反馈；模型结构已具备跨规模推理能力，但泛化成功率仍取决于训练覆盖与 router 能力。

第一阶段新增两个独立、向后兼容的基础模块：

- `src/algorithm/src/stochastic_clock.py`
  - 先采样、后冻结的空间时钟场；
  - 保存绝对 stage `tau`，phase 由 `tau mod phase_count` 得到；
  - 提供可因果传播的随机时钟带、4x4 packed phase 投影和多场景 CVaR 聚合。
- `src/algorithm/src/universal_graph_policy.py`
  - 有向图消息传递；
  - 对环境实时生成的 ragged action candidates 打分；
  - 参数维度与节点数、层数、动作数无关；
  - 可直接适配现有 `LayoutCompactionEnv`，不改动当前几何动作与 legality mask。
- `src/algorithm/src/universal_graph_ppo.py`
  - 跨电路 ragged rollout；
  - truncation value bootstrap、clipped value loss、target-KL early stop；
  - exact reward 在首次 update 前写入真正产生 episode-best 的动作。
- `src/algorithm/main/train_universal_graph_ppo.py`
  - balanced multi-circuit sampling；
  - 每个 episode 采样并冻结 causal clock field；
  - 多场 exact routing 的 mean+CVaR 反馈与可复用 checkpoint。
- `src/algorithm/main/evaluate_universal_graph_ppo.py`
  - 显式选择 held-out 电路并检测训练集重叠；
  - 在相同冻结 clock fields 上成对比较 policy 与 warm-start baseline；
  - 分开报告逐电路/总体 success rate、CVaR、policy-better rate 和原始场景记录。

这些模块不会自动替换当前 GUI 入口。当前
`evaluate_layout_candidate` 已可通过候选中的 `clock_field` 安装冻结 phase projection，
通用 CLI 已能执行多电路场景批训练，独立 evaluator 也能在 Python 侧检查路径的
absolute-stage transition；C++ router 内生 stage 约束、subprocess 场传输、GUI 接入和
held-out 大规模实验仍须按下文完成，才能给出可信的最终泛化结论。

## 审计证据

### 当前 PPO 不是通用模型

`train_layout_ppo.py` 将所有全局、逐层、逐节点特征展平，再接固定输入/输出 MLP。
action list 也按当前层数和节点数展开。因此每个电路产生不同的 `obs_dim/action_dim`，
checkpoint 不能直接跨电路加载。

仓库里曾运行过一个已缺失源码、只剩 `.pyc` 和 checkpoint 的 universal 版本。读取
checkpoint 可见：

```text
obs_dim    = 1103
action_dim = 1409
schema     = 31 global + 16*11 layer + 64*14 node
```

它仍是 16 个 layer buckets、64 个 node buckets 的定长 MLP。小电路会出现多个 bucket
映射到同一实体的动作别名；超过桶上限后，部分实体不能被单点动作直接选择。

### 当前“随机时钟”不是冻结空间场

`--clock-domain-randomization` 每个 episode 只采样：

```text
phase_cycle, padding, max_same_phase
```

其中 padding 是 router search window，不是时钟物理状态。棋盘没有安装随机 phase map；
未赋相位格点被路由器视为可接受任意 phase，路径成功后才写入 phase。这属于“布线和
相位联合生成”，不是“在给定随机时钟场上布局布线”。

C++ `RandomClock` 虽然可生成随机 4x4 编码，却没有被 `MapChessboard::getPhase()` 使用。
而且独立均匀随机 phase 不保证有向因果通道。已有实验也显示 raw template 在早期训练
中 exact legal count 为 0；受约束 axis template 明显更可路由。

### 已有 14/14 不能当作 policy 泛化率

历史 random-clock checkpoint 在参与训练的同一批 14 个 TOY 电路上保存每个电路的
历史最佳 exact 结果，summary 因而可显示 14/14。独立的 policy sampled exact rerank
结果更接近：

```text
policy only        8~10 / 14
policy + rerank       9 / 14（部分实验）
policy + finite rescue 14 / 14
```

rescue 是有价值的求解器组成，但报告时必须和 policy pass@1 分开。现有结果也没有
held-out circuit family，因此尚不能证明拓扑泛化。

## 随机时钟问题定义

### 区分三类随机性

1. 空间随机时钟场：每个空间区域的 stage/phase 不同，本阶段处理该问题。
2. router domain randomization：搜索边界、代价、扩展预算等算法超参数。
3. 时间 jitter：仿真波形的幅值、相位和频率扰动，应在空间 DRC 通过后单独评估。

三者不能都压缩成 `(phase_cycle, padding, max_same_phase)`。

### ClockField

首版使用绝对时钟级次：

```text
tau(x, y) = directed_primary(x, y) + band_offset(secondary(x, y))
phase(x,y) = tau(x,y) mod P
```

随机带的相邻 `band_offset` 增量限制在 `{0, 1}`。因此配置的主传播方向每步严格
`tau += 1`，次方向每步 `tau += 0 or 1`。axis 和 diagonal 是它的两个确定性特例。

仅保存 phase 会把相隔一个或多个完整周期的区域混为同一 clock zone，所以训练状态、
ClockDRC 和未来 IFCN sidecar 都应保留 `tau`。当前 packed IFCN 只能保存 phase；新增模块
因此同时返回完整 `ClockField` 和兼容旧棋盘的 packed phase projection。

`raw` IID phase 只用于 adversarial stress，不作为主训练分布。

## 通用图策略

```text
node/static + placement + local clock
                  |
           Directed GNN
             /         \
       graph pool     target-node pool
             \         /
       [action type, delta, target features]
                  |
           candidate scorer -> ragged logits

graph pool + clock context -> value
```

环境仍负责生成可解释的动作：gap、layer shift、block shift、segment shift、node shift。
每个动作通过其目标节点集合做 pooling，所以 action head 不需要固定槽位。未来加入
net-order、rip-up、corridor 等动作时，只需增加 action type 和 target membership。

当前适配器生成的节点特征包含：

- gate type；
- fanin/fanout degree；
- 归一化 layer/rank；
- 相对布局中心的坐标（整体平移不改变策略输入）；
- I/O 标记；
- GCN embedding 的一维排序先验；
- 节点所在位置的 phase one-hot、相对 stage 和是否已赋时钟场。

有向边消息另外包含逻辑跨层距离、当前 `dx/dy/Manhattan` 跨度、端点 phase/stage
差，以及在“每格只能 hold 或前进一级”约束下端点是否可达。这样 actor 不只看到全局
失败计数，也能在动作发生前定位随机时钟场中的高风险连接。

下一阶段 exact router 还应回传 edge-level 的 routed/failed、拥塞、长度、弯折、端口冲突、
stage transition 和 reconvergence skew，解决当前策略只知道“失败几条边”却不知道失败位置的
部分可观测问题。

## 训练目标

同一个 placement 必须在多个冻结的 `ClockField` 上分别 exact route。每个场得到：

```text
legal, failed_edges, direction_violations, clock_violations,
area, wirelength, latency, runtime
```

先按 legality-first 比较：

```text
1 - success_rate
CVaR_0.9(violations)
mean violations
CVaR_0.9(cost)
mean cost
```

训练的标量 critic target 可用：

```text
mean(loss) + lambda * CVaR_0.9(loss)
```

只有 `Pr(exact legal)` 达到阈值后再重点比较面积、线长、能耗和延迟。不能用“从 K 个
clock seeds 中挑一个最好结果”替代 K-field robust success。

## 推荐接入顺序

1. 给 exact-eval payload、cache key、result metadata 增加 `clock_spec/seed/field_hash`；
   当前 in-process exact path 已携带 `field_hash`，subprocess payload 尚待扩展。
2. `create_board_with_positions` 已能安装冻结 phase projection；下一步让 Python/C++ A*
   显式读取 absolute stage，并由独立 ClockDRC 检查倒退、跨级跳跃和重汇合偏斜。
3. 增加独立 ClockDRC；旧 dynamic phase 模式保留为 `dynamic_legacy` baseline。
4. PPO 流程已修正：exact reward 在首次 PPO update 前写入产生 episode-best 的
   transition；时间上限作为 truncation 做 value bootstrap。
5. 通用 CLI 已用 `UniversalGraphPolicy` 跨多电路、多场景收集 rollout 后统一更新；
   下一步增加 PyG batching 提升大批量吞吐。
6. evaluator 已支持显式 held-out circuit 和 held-out clock seeds，并可用
   `--require-unseen` 检查训练集重叠；仍需建立固定的 held-out family/size 数据清单。
7. 固定报告 policy pass@1、policy pass@K、policy+rescue、heuristic+rescue。

## 当前验证

```bash
include/gcn_rl_layout/myenv/bin/python -m unittest discover \
  -s include/gcn_rl_layout/tests -p 'test_*.py' -v
```

当前回归套件共 63 个单元测试，除以下基础项外，还覆盖 IFCN 数据集去重与拓扑隔离、
离线模仿学习、检索记忆、GRU/PPO ragged update、exact reward credit assignment、
learned route priorities、clock-aligned start 和 GUI runner 的严格导出协议：

- stochastic-band 因果约束与 seed 可复现性；
- 4x4 packed phase 编解码一致性；
- raw field 明确标记为 non-causal；
- mean + CVaR 鲁棒目标；
- 同一图策略接受不同节点规模；
- 节点重编号/排列不改变 action logits；
- 整体平移不改变节点几何特征；
- 前向、反向梯度有限；
- 现有 `LayoutCompactionEnv` 动态动作适配。
- 两种不同图/动作规模在同一次 PPO update 中训练；
- delayed exact feedback credit assignment 与 truncation bootstrap。

真实工程冒烟还验证了 `xor2.v`：冻结 axis field 下 7/7 nets 完成布线，所有路径相邻
stage delta 均为 0 或 +1，棋盘 phase 与 `ClockField` 投影一致。

双电路训练冒烟验证了同一 checkpoint 在 `xor2.v`（6 nodes）和 `xnor2.v`（9 nodes）
间共享，checkpoint 不含固定 action-dimension policy head；两个电路的冻结 axis field
exact routing 均为 legal。

GUI 推理冒烟使用 V4 best-exact checkpoint、134 条检索记忆和 `mux21.v`：在冻结
`stochastic-bands` 场上得到 `4 × 7 = 28` 的合法版图，failed edge、方向违规和
clock violation 均为 0，并成功生成 `.ifcn/.svg/.tex/encoded.ifcn/summary.json`。
关闭 clock alignment 与同拓扑检索的负向分支没有找到合法解时返回退出码 2，且不会
导出或加载非法 `.ifcn`。
