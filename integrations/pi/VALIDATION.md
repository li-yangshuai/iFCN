# 首版接入验证

当前交付状态见 [本轮交付与证据](DELIVERY.md)：三个后端、逻辑与端子检查、冻结基准、真实 LLM 对照和最新完整流程验证。以下保留首版历史记录，其通过数量与检查范围不能作为最新版本成绩。

验证日期：2026-09-05。环境：WSL Ubuntu 24.04，Pi SDK 0.84.2，Node 22.22.2。

后续同日已补充 TOY/MAJ 全清单、其他原生算法及两次真实 LLM 实测，见 [投稿基础现场审计](PUBLICATION_AUDIT.md)。以下保留首版 SDK smoke 的原始范围与结果。

- 8 项自动测试通过：布尔改写真值表、非法/时序输入拒绝、拓扑排序、P&R 失败拦截、负能耗诊断、输入快照和产物哈希、并发取消恢复、超时恢复。
- 扩展 TypeScript 严格类型检查通过。
- 使用本机 Pi SDK 加载安装后的扩展，四个工具注册成功，无加载错误。
- 用四个工具实际完成 `integrations/pi/examples/xor2.v` 的 parse → pnr → map → simulate → energy，没有调用付费 LLM。
- Pi 的标准资源加载器在 `pi-web-ui` 与 `pi-web-ui/amedac-pi-web` 两个已信任目录下自动发现扩展，无重复注册或加载错误。

运行 ID：`5022166115f143d0aa3c8264707276b6`。

运行目录：`/home/lys/projects/github/iFCN/output/pi-runs/5022166115f143d0aa3c8264707276b6/`。

完整验证结果：`/home/lys/projects/github/iFCN/output/pi-validation/sdk-smoke.json`。

| 项目 | 实测结果 |
|---|---|
| 门级布局 | 3 × 4 = 12 tiles |
| 器件布局 | 79 QCA 元胞 |
| 布线/时钟模板 | 无失败边，四相 2DDWave 合法性检查通过 |
| Bistable | 512 输出样本，baseline/accelerated 最大差异 0 |
| Coherence | 3005 输出样本，baseline/accelerated 最大差异 0 |
| 标准能耗 | 步长 1e-17 s，时钟周期 1e-11 s，7 个统计周期 |
| 平均 bath 能量 | 0.0289886 eV/统计周期 |
| bath 能量换算功率 | 4.6444857572372394e-10 W，仅为模型结果 |

不能把以上结果解释为完整物理功能或功耗签核。当前仿真一致性检查比较两个数值实现，没有将器件波形与 Verilog 真值表对齐。能耗时间步长收敛尚未测试，标准报告还保留 `total_error_eV=0.179718`，需要进一步检查数值误差和能量平衡；不要直接引用换算功率作为经过验证的功耗指标。

另一个真实检查中，preview 配置的 bath 能量为负。现已增加数值诊断：负 bath 能量会将整体状态标为 `completed_with_warnings`，耗散功率字段返回 null。默认配置已改为 standard。

安装状态：两个 Pi Web 目录原有信任为 true；iFCN 项目原有信任为 false，未修改其信任决定。在 iFCN 工作目录使用时，应在 Pi 界面完成信任后重新加载。
