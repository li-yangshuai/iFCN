iFCN for macOS 15 or later — Apple Silicon (arm64)

Drag iFCN.app to Applications, then launch it. Qt, Graphviz, and their native
runtime dependencies are included; Homebrew is not required on the target Mac.
This build has an ad-hoc integrity signature only. It is not signed with an
Apple Developer ID or notarized. macOS may require approval in System Settings
> Privacy & Security when opening an app downloaded from the Internet.

Open an IFCN circuit using File > Open. The 91 examples, bilingual manual,
validation table, and licenses are inside the app at Contents/Resources.
In Finder, right-click iFCN.app and select Show Package Contents to reach them.
Examples marked __drc_only have known failed physical-output checks; see
Contents/Resources/docs/examples.md for the limitations of every example.

Included: native heuristic and irregular-clock placement/routing, editing,
mapping, compact operations, simulation, visualization, and native command-line
tools in Contents/MacOS. For example:

  /Applications/iFCN.app/Contents/MacOS/ifcn_mapping_metrics circuit.ifcn

The optional classical Python 2DDWave backend and the external Yosys/Z3 RTL
toolchain are not embedded in this macOS package. Build the Python backend from
the matching source release using the instructions in README.en.md. Native
algorithms and viewing the supplied fixed-clock/sequential examples work without
Python, Yosys, or Z3.

macOS 15 及更新版本，Apple Silicon（arm64）

将 iFCN.app 拖入 Applications 后启动。Qt、Graphviz 与本地运行库已包含，
目标机器无需安装 Homebrew。本包仅有本地完整性签名，没有 Apple Developer ID
签名或公证；首次运行时，系统可能要求在“系统设置 > 隐私与安全性”批准打开。

通过 File > Open 打开 .ifcn。91 个电路示例、中英文说明、验证表和许可证位于
应用内部 Contents/Resources；在 Finder 中右键应用，选择“显示包内容”查看。
__drc_only 示例的物理输出检查失败，所有案例限制见 docs/examples.md。

本包包含本地启发式与不规则时钟布局布线、编辑、映射、收缩、仿真及命令行工具。
可选经典 Python 2DDWave 后端、外部 Yosys/Z3 工具链需按同版本源码说明另行安装；
本地算法以及查看固定时钟、时序示例不依赖这些工具。
