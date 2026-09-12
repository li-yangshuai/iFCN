# iFCN Pi 主智能体

已完成的功能、真实测试结果及论文证据边界见 [本轮交付与证据](DELIVERY.md)。

把现有 iFCN 组合电路流程交给一个可在 Pi Web 中选择的主智能体。Pi 扩展注册四个真实工具，智能体通过结构化参数调用已有设计程序。无需修改 Pi Web 的会话、模型或界面代码。

```text
Pi Web 的 ifcn 主智能体（或 Pi CLI 的 LLM）
  → ifcn_capabilities / ifcn_run / ifcn_status / ifcn_artifact
  → scripts/ifcn_agent.py：后台作业、状态、取消、检查点、JSON
  → Verilog 检查和规范化
  → CircuitParser / 三个可选 P&R 后端 / 有预算的失败回退
  → 有限输入的源代码/DAG穷举等价与器件端子完整性检查
  → ifcn_mapping_metrics / ifcn_energy_analysis --qca-only
  → ifcn_physical_benchmark
  → ifcn_energy_analysis
  → 独立目录中的 DAG、IFCN、QCA、波形、能耗报告、result.json 与 manifest
```

## 加载到 Pi Web

在 WSL 内执行（下面的路径以当前机器为例）：

```bash
cd /home/lys/projects/github/iFCN
python3 integrations/pi/install.py \
  /home/lys/projects/github/iFCN \
  /home/lys/projects/github/pi-web-ui \
  /home/lys/projects/github/pi-web-ui/amedac-pi-web
```

安装器只写各目录下的 `.pi/agents/ifcn.md`、`.pi/extensions/ifcn/` 和安装清单，不改变已有模型、认证或其他扩展设置。重复执行可升级安装器自己写出的文件；遇到用户修改过的同名文件会保留并报错。

在 Pi Web 新建会话，工作目录选上面任一项目目录，在主 Agent 选择器中选择 **ifcn**。项目扩展遵循 Pi 自身的项目信任设置；若尚未信任，在 Pi 界面中完成项目信任后重新加载。已有会话可 `/reload`，但主智能体配置建议在新会话选择。不要关闭全部工具，否则 iFCN 工具也会被禁用。

可以直接说：

> 使用 /home/lys/projects/github/iFCN/integrations/pi/examples/xor2.v，完成 DAG、门级布局布线、器件版图、Bistable 仿真和标准能耗分析，解释检查结果并给出产物路径。

或先检查再继续：

> 先把这个 Verilog 解析到 DAG，让我查看结构。
> 使用刚才的 run_id，继续到器件版图。

也可以指定后端或比较算法：

> 使用 tests/benchmarks_pi/v1/MAJ/1bitAdderMaj.v，选择 compact，运行到器件映射，读取结果并说明检查范围。
> 对同一输入，分别使用 normal_2ddwave、compact 和 june_random，保持相同 seed 和时限，报告所有成功和失败；比较通过检查的布局面积。
> 时钟方案不限，使用 auto 在布局失败后尝试其他后端，读取 pnr_attempts.json 解释最终选择。

Pi CLI 原生会自动发现项目扩展，LLM 可直接识别工具描述。`.pi/agents/*.md` 的主智能体选择是本工程 Pi Web 提供的能力。CLI 使用同一主智能体提示词时，可把 `agent.md` 去掉 YAML 头部后通过 Pi 的 `--system-prompt` 加载；仅注册工具可 `pi -e /home/lys/projects/github/iFCN/integrations/pi/extension.ts`。

## 工具合同

| 工具 | 用途 |
|---|---|
| `ifcn_capabilities` | 支持范围、后端路径、可执行文件检查；`deep=true` 另测 Python/native 导入 |
| `ifcn_run` | `start` 新建后台作业、`resume` 继续、`cancel` 请求取消 |
| `ifcn_status` | 状态、日志尾部、参数、指标、产物；支持最多 20 秒等待 |
| `ifcn_artifact` | 按 manifest 中的名称读取文本产物并验证 SHA-256 |

`until` 可以是 `parse`、`pnr`、`map`、`simulate`、`energy`，自动执行前序依赖。每个请求只有一个后台工作进程；同一运行目录加锁，避免重复执行。取消会停止当前后端进程组。关闭聊天不会自动取消已提交的作业。

每次运行在 `output/pi-runs/<run_id>/` 下保存原始输入快照、规范化输入、参数、各阶段命令与日志、产物哈希。失败或取消后可 resume，复用已完成且文件未变化的阶段；改变参数需新建运行。新的阶段尝试写入新目录，不把旧文件误认为本次结果。

完成状态说明：`completed` 只表示所请求阶段执行完成。`completed_with_warnings` 表示流程执行完成但存在已识别的数值异常。任何状态都不自动代表原 RTL 到器件物理功能已签核。

## 独立命令行

```bash
python3 scripts/ifcn_agent.py doctor --deep
python3 scripts/ifcn_agent.py start integrations/pi/examples/xor2.v --until energy
python3 scripts/ifcn_agent.py status <run_id>
python3 scripts/ifcn_agent.py resume <run_id> --until energy
python3 scripts/ifcn_agent.py read <run_id> energy.json
python3 scripts/ifcn_agent.py cancel <run_id>
```

start 可选 `--model bistable|coherence|both`、`--energy-profile standard|preview`、`--vectors /path/input.vt`、`--samples 512`、`--seed 1`、`--timeout 600`（每个后端命令的秒数）。输入表按原后端合同解释，具体刺激和完整物理参数保存在仿真/能耗报告中。energy 阶段另运行 coherence 能耗算法，与 simulate 的模型选择独立。

`--algorithm normal_2ddwave|compact|june_random|auto` 选择布局后端，默认 `normal_2ddwave` 保持旧运行行为。`auto` 的 P&R 共享一个 `--timeout` 时限，在尚未尝试的可用后端间分配剩余时间，按 normal → compact → june 顺序在首次得到通过检查的布局时停止。它不是面积最优选择，不会覆盖指定算法，也不会在用户取消后回退。每次尝试的目录、预算、失败和选择记录在 `pnr_attempts.json`。

| 后端 ID | 实际实现 | 时钟 |
|---|---|---|
| normal_2ddwave | Graphviz/sifting、固定模板路由和压缩 | 四相 2DDWave |
| compact | 原生 Compact Graph、自适应扩张和压缩 | 四相不规则分配 |
| june_random | 原生 Graphviz、四方向布线及事后相位分配 | 四相不规则分配 |

`result.json` 汇总最终阶段、指标、验证范围和来源文件的真实路径。LLM 必须把后端统计与自己实际读取的内容分开表述；列出文件并不等于已检查文件内容。

候选通过几何检查后，`logic.json` 对11个及以下输入穷举比较原始标量Verilog与实际布线DAG。信息不足或不等价会拒绝该候选；超过限额明确记录未执行。另生成器件探测文件并通过 `interface.json` 检查输入/输出端子数量，防止原算法删除输出后仍被报为成功。`auto` 只接受通过这些门槛的候选。map阶段复用并复核该器件文件；旧运行恢复到pnr及以后时也补验相应条件。若旧P&R没有器件接口证据，仅恢复到pnr会被拒绝，需继续到map生成并核验器件产物。

默认使用 `build-release` 的映射、仿真、能耗程序及 `include/gcn_rl_layout/myenv/bin/python`。Compact/June 还需构建 `ifcn_combinational_pnr` 目标。迁移机器可设置 `IFCN_BUILD_DIR`、`IFCN_PYTHON`、`IFCN_NATIVE_PNR`、`IFCN_RUNS_DIR`；Pi 扩展也支持 `IFCN_ROOT`。capabilities 单独报告每种算法的可用状态，无需把敏感模型配置复制进工程。

## 当前边界

- 输入为单模块标量组合 assign 网表，支持 `~ & | ^`、括号与标量 ANSI/non-ANSI 端口。总线、常量、参数化/层次实例、行为 RTL 尚未接综合器。不能以“支持 Verilog”推断成支持任意 RTL。
- 三种可选后端均调用已有算法。遗传、GCN/PPO 和记忆策略仍标为未接入；当前不提供三相时钟、训练或任意策略调用。
- parse 阶段保存输入的优化 DAG；P&R 后保存最终实际使用的 `routed_dag.json`。现有后端可能对奇偶校验电路做穷举验证过的规范化，实际布线拓扑因此可以不同。
- `layout_legal`、`clock_legal` 与 `validation.passed` 只对应报告列明的结构/时钟检查。不规则时钟的 `clock_template_ok=null` 表示不适用 2DDWave 模板。映射阶段复用现有映射/交叉检查。
- 仿真调用现有 baseline/accelerated 成对比较，并要求输出非空且数值完全一致。**这不等于验证了器件波形符合原 Verilog 的真值表。** 达到求解迭代上限会以警告报告，即使两实现一致也不声称已收敛。
- 标准能耗步长为 `1e-17 s`；快速 preview 为 `2e-15 s`，当前 XOR 示例中快速配置会出现负 bath 耗散能量，工具将其标为异常，耗散功率字段返回 null。
- `average_dissipated_power_W` 按平均每周期 bath 能量换算；`average_bath_clock_power_W` 是有符号 bath+clock 能量平衡诊断量，不是总耗散功率。能耗仍需时间步长收敛及功能验证，不能直接用于签核或节能宣传。
- 时序适配在 capabilities 中保留，当前明确为 `supported=false`。后续需接入 Yosys/SeqIR、真实反馈 P&R、tile phase/epoch/II 验证与独立物理验证；遵守 `handoff.md` 中 coarse clock tile 的语义。

## 验证

```bash
python3 -m unittest discover -s tests -p test_ifcn_agent.py -v
# Pi SDK 加载与真实工具执行（使用 Pi Web 的 Node 22 环境）
node integrations/pi/smoke.mjs /home/lys/projects/github/pi-web-ui/amedac-pi-web
```

前者检查布尔改写真值表、非法输入、P&R 失败、数值异常、任务取消/超时/恢复和产物防篡改。后者用本地已安装的 Pi SDK 载入扩展，并通过真实工具完成 XOR 流程，不需要调用付费模型。
