# 1bitAdderAOIG paired compaction artifact

> **Invalidated on 2026-07-20.** The archived `Kc8` result lets two fanins of
> the same gate enter through one physical side.  It was accepted by the old
> set-based port checker, which collapsed duplicate input directions.  Do not
> use the `6 x 11`, 295-cell result or its derived figures as legal data.  A
> corrected exclusive-input-port rerun gives `9 x 13 = 117` before compaction
> and `9 x 11 = 99` after compaction (388 to 352 mapped cells); the archived
> files below are retained only to reproduce the discovered defect until the
> paper figures are regenerated.

These files archive the exact seed-1 normal-graph-draw result used in the paper's
case study.

- `1bitAdderAOIG_Kc0.ifcn`: legal gate-level layout before compaction,
  \(11\times11=121\) tiles.
- `1bitAdderAOIG_Kc8.ifcn`: legal layout after five accepted column
  contractions, \(6\times11=66\) tiles.
- `*_encoded.ifcn` and `*_summary.json`: encoded layouts and run summaries.
- `stages/route_closed_Kc0.tex`: recorded fully routed pre-compaction state.
- `stages/compacted_5cuts.tex`: recorded state after the fifth contraction.

The cell-level panel is generated from `1bitAdderAOIG_Kc8.ifcn` with the same
mapping and rendering path as **iFCN → Save Cell Level Layout**:

```bash
QT_QPA_PLATFORM=offscreen \
IFCN_AUTO_MAP_FILE=paper/data/1bitAdderAOIG/1bitAdderAOIG_Kc8.ifcn \
IFCN_AUTO_EXPORT_CELL_LAYOUT=paper/figures/Fig10/adder_cell_level_layout.pdf \
build/fcnx_gui
```

This mapping reports 295 mapped cells and eight cross segments. The figure's
gate and crossover details are vector crops from that exported PDF: normal route
cells are on L1, vertical transition cells span L1--L3, and X-shaped crossover
cells occupy L3.

The 4/16 and 10/16 panels in the paper are display replays of prefixes from the
recorded 16-net route order. They are not separate optimization runs and do not
claim intermediate failures.
