# Random-clock Compact Graph Draw figure notes

## Algorithm panels represented in `main.pdf`

- **A — Graph preparation.** Parse and DRC-optimize the exact
  `paper_2ddwave_crossing_demo.v` netlist, levelize and order its eight nodes,
  then form a compact
  candidate pool from scaled, anisotropic X/Y, and fixed-layer seeds.
- **B — Adaptive routing.** The shown `routing_conflict` snapshot is a real
  partial route from the default Compact Graph run. The initial candidate
  naturally blocks edges `5→7`, `2→5`, and `1→5`; failed edges are promoted
  before routing-order retries. Persistent conflicts reject the candidate and
  feed back to X/Y relaxation or seed switching. The recovered candidate
  routes all `10/10` nets.
- **C — Phase-aware optimization.** Legal routes enter the anchor phase
  solver: routes are grouped, shared/crossing cells become anchors, bounded
  search assigns them, and remaining cells use phase hold or one-step advance
  modulo four. One-cell compaction trials reroute and rephase transactionally;
  legal improvements commit, while failures roll back.
- **D — Clock encoding.** The selected result is mapped to the native
  cell-level layout, encoded with four clock phases, packed into clock tiles,
  and exported through iFCN's native layered 3-D view.

All embedded circuit drawings are native iFCN SVG or LaTeX outputs. Only the
stage-1 logic drawing uses the colored 2DDWave layered-graph convention.
Placement, routing, phase, and compaction panels retain the original iFCN
LaTeX colors: white nodes, blue routes, and gray clock phases. The gate-level
and cell-level layouts use the same top-to-bottom signal direction; the
algorithm does not rotate or reflect the completed layout during final export.

## Recommended paper caption

**Random-clock Compact Graph Draw flow for a compact crossing demonstrator.**
Unlike the previous single-scale Graphviz placement, the proposed flow
searches anisotropically quantized Graphviz and fixed-layer compact candidates.
Blocked routes first trigger failed-edge prioritization and routing-order
retries; persistent conflicts feed back to X/Y relaxation or seed switching.
Fixed-layer candidates use phase-aware A*, whereas legal Graphviz routes enter
anchor-based phase assignment.
Legality-first candidate ranking and transactional cut--reroute--rephase
compaction replace scale-only area control. The selected layout is mapped
without final rotation, clock encoded, and exported as the native cell-level
and layered 3-D structures.
