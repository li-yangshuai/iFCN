# IFCN 时钟相位编码 / Clock-phase encoding

[简体中文](../README.md) · [English](../README.en.md)

规则与不规则时钟使用同一种存储格式。布局算法先分配实际相位，保存时编码；
读取时按下面的公式解码，不根据坐标重新生成时钟。文件中的相位编号为 0–3，
三相时钟只使用 0–2。

Regular and irregular clocks share one storage format. Placement assigns phases;
serialization encodes them, and loading decodes the stored values rather than
regenerating clocks from coordinates. File phases are 0–3, or 0–2 for three-phase clocks.

```text
#phase map
#phase codec: phase_count=4, block_size=4, encoding=packed_hex_2bit_row_major
tile(0,0):0xe4394e93;
#phase map
```

每个 `tile(tx,ty)` 保存一个 **4 × 4** 相位块，共 32 位。每一行占一个字节，
第一行位于十六进制字符串最左边；同一行中第 0 列使用最低两位。

Each `tile(tx,ty)` stores a **4 × 4** block in 32 bits. Each row occupies one byte,
with the first row on the left of the hexadecimal string. Column zero uses the
lowest two bits of its row byte.

块内坐标为 `(u,v)`，范围均为 0–3；全局坐标为 `(4tx+u, 4ty+v)`：

For local coordinates `(u,v)` in 0–3 and global coordinates `(4tx+u, 4ty+v)`:

```text
shift = 8 × (3 − v) + 2 × u
code  = OR of (phase(u,v) << shift) over all 16 positions
phase(u,v) = (code >> shift) & 0x3
```

上面的 `0xe4394e93` 解码为 / The example decodes to:

```text
0 1 2 3
1 2 3 0
2 3 0 1
3 0 1 2
```

边缘块中未分配相位的位置填 0；规则时钟写入器会保留版图范围内完整的固定
模板，包括空白网格位置。这不会新增器件或改变布线。节点与路径
坐标仍使用原有网格单位。读取器继续兼容旧的 `(x,y):phase;` 展开格式及已有的
旧块尺寸，非法编码不能静默截断或替换为默认相位。

Unassigned positions in boundary blocks are padded with zero. Regular-clock
writers preserve the full fixed template within the layout bounds, including
empty grid positions. This adds no devices and changes no routes. Coordinates retain
their original grid units. Readers remain compatible with legacy expanded
`(x,y):phase;` maps and existing legacy block sizes. Invalid encodings must not
be silently truncated or replaced with default phases.

Python 导出候选的 `phase_map_scope` 默认是 `occupied`：保留节点与路由使用的
相位，其余位置补 0。显式选择 `board` 时，候选必须提供完整外接矩形中的每个
相位，写入器逐值保存；外接矩形之外的边缘补齐仍为 0。规则模板完整导出使用
`board`。这是导出候选的选项，不是新增的 IFCN 文件语法。

Python export candidates default to `phase_map_scope=occupied`, preserving phases
used by nodes and routes and padding other positions with zero. Explicit `board`
scope requires every phase inside the bounding rectangle and writes those values;
boundary padding outside that rectangle remains zero. Full regular templates use
`board`. This is an export-candidate option, not additional IFCN file syntax.

示例库现有 **91 个文件**均使用此编码。原 90 个文件完成了 180 次转换前后
映射检查，QCA 数据逐字节一致，原审查证据与转换前哈希继续保留。新增 GA
MUX41 由编码写入器直接生成，其元数据标题规范化与空白位置的规则模板补全
也已验证 QCA 数据不变，不混入原 90 个文件的转换统计。
哈希与来源记录在[案例结果表](examples.csv)中；存储等价不替代
[物理功能验证](examples.md)。

All **91 example files** use this encoding. The original 90 files passed 180
before/after mapping checks with byte-identical QCA data; their original audit
evidence and pre-encoding hashes are retained. The added GA MUX41 was generated
by the packed writer directly. Its title normalization and regular-template
completion at unused positions also preserve QCA bytes and are recorded
separately from the original 90-file conversion. Hashes and provenance are in the
[case table](examples.csv); storage equivalence does not replace
[physical functional validation](examples.md).

其中，展示用的 [2DDWave MUX41](../examples/regular_2ddwave/TOY/mux41.ifcn)
随后单独补齐了 **13 × 11** 范围内的规则时钟背景：保留全部 **97 个已占用
位置**和既有非零相位，补全 36 个原为 0 的空白位置及两个缺失的空块，范围外
仍补 0。节点、路径与其他元数据未变；当前映射器转换前后的 **737 个 QCA
元胞逐字节一致**。此项空白模板审计独立记录在案例结果表中，不改写原 90 个
文件的转换证据或转换前哈希，也不代表新增物理验证。

The displayed [2DDWave MUX41](../examples/regular_2ddwave/TOY/mux41.ifcn)
subsequently received a separate completion of its **13 × 11** regular-clock
background. All **97 occupied positions** and all existing nonzero phases were
preserved; 36 unused zero-valued positions and two missing empty blocks were
completed, with zero padding outside the bounds. Nodes, routes, and other
metadata are unchanged. The current mapper produced **737 QCA cells with
byte-identical data before and after**. This separate template audit is recorded
in the case table, retains the original 90-file conversion evidence and
pre-encoding hashes, and does not add physical validation.
