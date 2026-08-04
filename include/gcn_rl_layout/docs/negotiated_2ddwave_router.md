# Constrained-force placement and negotiated 2DDWave routing

## Scope

This is the replacement architecture for the fixed-clock Normal Graph flow. It
does not train or load a GCN. The input is a combinational layered DAG and the
output must satisfy all of the following hard constraints:

- every route step is either right `(1, 0)` or down `(0, 1)`;
- the two fanins of a gate use distinct top/left input ports;
- every fanout tree leaves its source through one shared right/down port;
- gates and illegal wire overlaps never share a tile;
- every accepted path follows the fixed 2DDWave phase progression;
- a crossover is used only when the selected cell-level library explicitly
  supports an orthogonal L1/L3 crossing.

The bounded L/Z negotiated core is available explicitly through
`IFCN_NORMAL_ROUTER=negotiated`. Auto mode selects it through 128 effective
edges and retains the proven
monotone direct/DP router for larger graphs until sparse track assignment is complete.
GateLevelMapping has a crossover layer for one bounded local intersection: one
straight horizontal source tree and one straight vertical source tree may share
an internal gate-level route tile. Same-source fanout branches may share their
continuous common trunk, but they may not split and later rejoin. Parallel
inter-net overlap, a bend at the shared tile, repeated crossings between the
same source pair, gate endpoints, and three-net intersections are illegal.

This legality tightening intentionally exposes some older TOY results (notably
the current RCA2 placement) as infeasible because they depended on parallel or
turning inter-net overlap. Those cases require more track capacity or placement
repair; they must not be made to "pass" by relaxing crossover semantics.

## Stage A: topology and constrained-force placement

1. Compute logic ranks and use OGDF only as a fast within-rank initializer.
2. Optimize continuous horizontal coordinates with a one-dimensional force
   model per rank. Attraction shortens incident nets; local repulsion separates
   gates; a congestion potential pushes gates away from routing hot spots.
3. After every force step, project the coordinates back onto the hard feasible
   set: stable rank order, minimum gate spacing, and `x(dst) >= x(src)` for every
   edge. This projection is mandatory; a free force-directed drawing is not a
   valid 2DDWave placement.
4. Solve the top/left fanin-port assignment as a two-port matching problem and
   choose one shared output direction for every fanout tree.

The force objective is only a proposal mechanism. Coordinates are accepted by
exact geometry checks, never by a learned or approximate legality score.

## Stage B: bounded-candidate monotone Manhattan routing

For each source--sink pair, generate a small deterministic path set instead of
scanning the complete source--sink rectangle:

- the two direct L paths;
- vertical--horizontal--vertical Z paths on the best candidate rows;
- horizontal--vertical--horizontal Z paths on the best candidate columns;
- branches from an already committed same-source trunk.

Candidate tracks are selected from endpoint-adjacent tracks and the lowest-cost
rows/columns in a congestion prefix-sum table. With at most `K` tracks per edge,
routing work is proportional to the generated path length, not rectangle area.

## Stage C: negotiated congestion

The first routing pass may temporarily share wire resources. A tile cost is

`cost = length + bend_cost + present_overflow + historical_overflow + crossover_cost`.

After a pass, construct a conflict graph and rip up only nets touching conflict
tiles. Increase the historical cost of those tiles and reroute the affected
nets. This prevents early short nets from permanently blocking long nets, which
is the main failure mode of the current sequential C432 run.

The intended control loop is:

```text
P <- project(force_place(layered_DAG))
A <- match_distinct_top_left_inputs(P)
H[cell] <- 0
repeat
    route affected source trees with bounded L/Z candidates
    C <- classify_overlaps_and_endpoint_violations()
    if C is empty and exact_2DDWave_check() then return LEGAL
    H[cells(C)] <- H[cells(C)] + history_increment
    rip_up(source_trees_touching(C))
    if no improvement then escalate_hot_region(C)
until resource limit
return INFEASIBLE_WITH_CERTIFICATE
```

The unit of rip-up is a source tree, not one fanout edge. This preserves the
single shared output direction and prevents two branches of the same logical
fanout from being optimized inconsistently.

Conflict classes are handled differently:

| Conflict | Resolution |
|---|---|
| Same-source shared prefix | legal and charged once |
| Same-source branches split then rejoin | illegal non-tree fanout; reroute the complete source tree |
| Gate versus wire | hard obstacle; choose another track or move the gate |
| Two unrelated parallel wires | illegal; reroute or insert capacity |
| Two orthogonal wire segments | optional legal L1/L3 crossover |
| Bend inside a two-net overlap | illegal; reroute or insert capacity |
| Same source pair crosses more than once | illegal weave; reroute the source trees |
| Gate endpoint in an overlap | illegal; rematch or reroute |
| More than two nets on one tile | overflow; insert capacity or reroute |
| Reused gate input port | rematch top/left ports, then reroute incident nets |

## Stage D: deterministic escalation

If negotiated routing reaches a fixed point with conflicts remaining:

1. rematch the affected gate ports;
2. move a small gate window using the congestion force and project it back to
   the 2DDWave feasible set;
3. insert one row or column through the minimum-cost cut of the hot region;
4. use an L1/L3 crossover for a legal orthogonal two-net intersection when the
   target library supports it;
5. otherwise return an explicit infeasibility certificate containing the cut,
   nets, ports, and blocked tiles.

Every escalation is transactional. A candidate is committed only when failed
nets, port violations, illegal overlaps, and 2DDWave phase conflicts all become
zero or improve lexicographically without corrupting the last legal state.

## Large-circuit policy

- Never invoke GCN training from an OGDF routing repair.
- Above 256 edges, cap negotiation at four passes, use six L/Z tracks, disable
  compound detours, and spend the saved time on congestion-cut placement.
- Disable exhaustive post-route cut probing above a configurable graph-size
  threshold; use only cuts suggested by congestion hot spots.
- Stop on lack of improvement, not only on a fixed iteration limit.
- Export per-stage timing, overflow count, crossover count, failed-net count,
  and the best legal area so a timeout can be diagnosed precisely.
