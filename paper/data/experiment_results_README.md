# Experiment data provenance

> **Superseded legality audit (2026-07-20).** The 2026-07-18 checker enforced
> assigned endpoint directions and fanin/fanout separation, but did not reject
> two fanins sharing one input side.  Consequently the CSV, Table 1, comparison
> curve, and the displayed `c17` `72 -> 48` contraction must be regenerated
> before publication.  Under the corrected exclusive-input-port checker, the
> seed-1 `c17` pair is `8 x 9 = 72` to `8 x 8 = 64`, while the corrected
> `1bitAdderAOIG` pair is `9 x 13 = 117` to `9 x 11 = 99`.  The implementation
> now refuses to export any trial with this defect.

`small_compaction_results.csv` retains the old baseline columns but replaces every proposed value with the 2026-07-18 rerun after hard fanin/fanout endpoint-port enforcement. Paired seed-1 summaries are under `/tmp/ifcn-port-rerun/kc0` and `/tmp/ifcn-port-rerun/kc8`; no retry-based seed selection was used. A result is legal only when routing, endpoint-port, and 2DDWave checks all pass.

The `accepted_cuts` field is reproducible from the paired endpoints as

```text
(W_Kc=0 - W_Kc=8) + (H_Kc=0 - H_Kc=8)
```

because `compact_layout` accepts one unit row/column contraction per iteration. The CSV deliberately retains all 17 attempted cases. Ten pairs are legal at both endpoints and seven are not; among the legal pairs, four improve both compaction and graph-baseline area and six remain larger than graph drawing. Table 1 displays the four positive legal pairs, while the text discloses the complete outcome counts.

`large_fiction_results.csv` transcribes the proposed node counts, dimensions, areas, and end-to-end times from the large-circuit results in `paper/rebuttal.tex` (commented rows near lines 702--711), and the matched `fiction` wiring-reduction data from the former Table 2 in `paper/main.tex`. The rewritten table deliberately omits runtime because the `fiction` values are reduction-only while the proposed values are end-to-end.

Computed columns use

```text
compaction_gain = 1 - area_Kc=8 / area_Kc=0
graph_gain      = 1 - area_Kc=8 / area_graph
raw_gain        = 1 - area_ours / area_fiction
normalized_gain = 1 - (area_ours / nodes_ours)
                      / (area_fiction / nodes_fiction)
```

The corrected arithmetic mean over the ten legal pairs is 32.7271% compaction gain. Over the four positive legal pairs displayed in Table 1, mean compaction gain is 26.9731%, mean graph-baseline gain is 45.9579%, and mean runtime is 3.4825 s. The prior large-circuit means are preserved only as archive arithmetic: those layouts have not been rerun under the hard endpoint-port checker and are excluded from efficacy claims.

Displayed runtimes are the corrected `K_c=8` end-to-end runs (roughly 3.3--3.6 s for the four shown pairs).
