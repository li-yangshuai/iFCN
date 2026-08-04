# 2DDWave XOR-demo figure notes

## Circuit

The circuit implements a two-input XOR using only AND/OR/inverter gates:

\[
\mathrm{out}=(a\lor b)\land\neg(a\land b).
\]

The source netlist has 7 nodes and 8 edges. After parser compaction, the routed
graph has 6 nodes and 7 edges.

## Node labels

- `0`: input `a`
- `1`: input `b`
- `3`: `a AND b`
- `4`: `a OR b`
- `5`: `NOT (a AND b)`
- `2`: output `out`

## Six figure stages

1. Initial layered placement.
2. Placement after fanin/fanout port reservation.
3. Initial routing attempt with four failed edges highlighted.
4. Expanded placement for conflict repair.
5. Completed conflict-free routing.
6. Final layout after one compaction pass (two accepted cuts).

Suggested caption:

> Evolution of the fixed-clock 2DDWave placement-and-routing flow on a compact
> AOIG XOR circuit: initial placement, port reservation, failed initial
> routing, conflict-driven expansion, legal routing, and final compaction.
