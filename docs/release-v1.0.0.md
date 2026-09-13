# iFCN 1.0.0

[简体中文](../README.md) · [English](../README.en.md) · [Downloads](https://github.com/li-yangshuai/iFCN/releases/tag/v1.0.0)

首个公开发行版：电路编辑、组合与时序布局布线、QCA 映射、Bistable / Coherence 仿真和能量分析。

The first public release includes circuit editing, combinational and sequential layout flows, QCA mapping, Bistable / Coherence simulation, and energy analysis.

## 下载与安装 / Download and install

| 系统 / System | 文件 / Download | 使用 / Launch |
| --- | --- | --- |
| Windows 10 / 11, x86_64 | `iFCN-1.0.0-windows-x86_64-setup.exe` 或 / or `.zip` | 运行安装器，或完整解压 ZIP 后双击 `iFCN.exe` / Run the installer, or extract the entire ZIP and open `iFCN.exe` |
| macOS 15+, Apple Silicon | `iFCN-1.0.0-macos-arm64.dmg` | 打开磁盘映像，将 iFCN 拖到 Applications / Open the disk image and drag iFCN to Applications |
| Ubuntu 24.04, x86_64 | `iFCN-1.0.0-ubuntu24.04-x86_64.tar.gz` | 安装下面的运行依赖，解压后运行 `./ifcn` / Install the runtime dependencies below, extract, and run `./ifcn` |
| 电路示例 / Circuit examples | `iFCN-1.0.0-examples.zip` | 91 个按算法分类的 `.ifcn` / 91 `.ifcn` files grouped by algorithm |
| 完整源码 / Complete source | `iFCN-1.0.0-source.tar.gz` | 包含全部构建、测试及打包源码 / Includes build, test and packaging sources |

下载同一 Release 的 `SHA256SUMS` 可核对文件完整性。Windows 和 macOS 包尚未进行商业代码签名或 Apple 公证；系统可能显示未识别开发者提示。只使用本仓库 Release 的文件。macOS Intel 用户请从源码构建，此次 macOS 二进制面向 Apple Silicon。

Use `SHA256SUMS` from the same release to verify downloads. Windows and macOS packages are not commercially code-signed or Apple-notarized, so the operating system may show an unidentified-developer prompt. Use files from this repository's release. The macOS binary targets Apple Silicon; Intel Mac users need a source build.

Ubuntu 24.04：

```bash
sudo apt update
sudo apt install libqt5widgets5t64 libqt5printsupport5t64 libqt5svg5 \
  libgvc6 graphviz libpython3.12t64 python3 python3-numpy python3-scipy \
  python3-matplotlib python3-networkx
tar -xzf iFCN-1.0.0-ubuntu24.04-x86_64.tar.gz
cd iFCN-1.0.0-ubuntu24.04-x86_64
./ifcn
```

Linux 包使用系统 Qt、Graphviz 和 Python 3.12，不是任意发行版通用的静态程序。不要只复制单个可执行文件，保留整个解压目录。

The Linux package uses system Qt, Graphviz and Python 3.12 and is not a universal static Linux binary. Keep the extracted directory intact.

## 功能范围 / Feature coverage

三个平台均提供原生规则时钟启发式、不规则时钟图绘制、器件映射、编辑与可视化、物理仿真，以及时序布局命令行工具。不规则布局共用一个入口，并统一检查 DRC 和全局时钟约束。

All three platforms provide native regular-clock heuristics, irregular-clock graph placement, mapping, editing and visualization, physical simulation, and sequential command-line tools. Irregular placement uses one search entry with shared DRC and global clock checks.

固定 2DDWave 图绘制使用 Python 后端：Windows 包内含运行环境；Linux 包含扩展与后端，使用上述系统 Python 依赖。macOS 应用包提供原生功能，固定 2DDWave 需要按[源码安装说明](../README.md#optional)另行构建，未内置 Python 环境。Yosys RTL 导入和外部 Z3 求解也需要单独安装对应工具。

Fixed-2DDWave graph placement uses Python: the Windows package includes its runtime; Linux includes the extension and backend and uses the Python dependencies above. The macOS app provides the native features; fixed-2DDWave requires a separate [source setup](../README.en.md#optional) and does not include Python. Yosys RTL import and external Z3 solving also require those optional tools.

开发构建继续保留全部原有测试与命令行目标；发布包包含运行所需文件。测试没有从工程删除。

Development builds retain all existing tests and command-line targets. Release packages contain runtime files; tests remain in the source project.

## 示例与已知限制 / Examples and known limits

通过 **File → Open** 打开 `examples` 中的 `.ifcn`；macOS 应用内的示例也可通过 Finder 的“显示包内容 → Contents → Resources”找到，或使用独立示例包。

Use **File → Open** to load `.ifcn` examples. On macOS, examples are also available through Finder's **Show Package Contents → Contents → Resources**, or in the separate examples archive.

- 保存的 89 个组合电路通过 DRC 与 16,456 组源逻辑向量比对；另有 2 个时序结构示例。
- 40 个“电路 × 算法”组合在记录的搜索预算内没有合法布局，未伪造输出文件。
- 5 个物理输出失败的案例用 `__drc_only` 标记，不能作为功能正确的器件使用。
- 已保存案例的物理验证分为：31 个穷举通过、7 个抽样通过、5 个失败、46 个未运行、2 个时序未表征。

The 89 saved combinational layouts pass DRC and 16,456 source-logic comparisons, alongside two sequential structural examples. Forty circuit/algorithm combinations have no legal result within the recorded search budget. Five known physical-output failures are marked `__drc_only`. Saved cases comprise 31 exhaustive physical passes, seven sampled passes, five failures, 46 not simulated and two sequential cases whose state behavior remains uncharacterized.

[完整案例结果 / Full results](examples.md) · [算法说明 / Algorithms](algorithm-availability.md) · [验证记录 / Validation](validation.md)

## 许可 / Licensing

iFCN 原始源码保留 MIT 许可。GUI 包含 QCustomPlot，组合后的 GUI 二进制按 GPL-3.0-or-later 分发；完整对应源码随本 Release 提供。平台依赖及其声明随对应包保留，见 [NOTICE](../NOTICE)。

iFCN's original source retains its MIT license. The GUI includes QCustomPlot and the combined GUI binary is distributed under GPL-3.0-or-later, with complete corresponding source in this release. Platform dependency notices are retained with their packages; see [NOTICE](../NOTICE).
