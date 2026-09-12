# 2DDWave carry-demo figure notes

## Circuit

The circuit implements the carry output of a one-bit full adder using only
AND/OR/inverter gates:

\[
C_{\mathrm{out}}=(y\land z)\lor\left(x\land(y\oplus z)\right).
\]

It has 9 parsed nodes and 11 edges, compared with 12 nodes and 16 edges in
`1bitAdderAOIG.v`.

## Node labels

- `0`: input `x`
- `1`: input `y`
- `2`: input `z`
- `4`: `y OR z`
- `5`: `y AND z`
- `6`: `NOT (y AND z)`
- `7`: `y XOR z`
- `8`: `x AND (y XOR z)`
- `3`: output `carry`

## Six figure stages

1. Initial layered placement.
2. Placement after fanin/fanout port reservation.
3. Initial routing attempt with seven failed edges highlighted.
4. Expanded placement for conflict repair.
5. Completed conflict-free routing.
6. Final layout after one compaction pass (four accepted cuts).

Suggested caption:

> Evolution of the fixed-clock 2DDWave placement-and-routing flow on a compact
> AOIG carry circuit: initial placement, port reservation, failed initial
> routing, conflict-driven expansion, legal routing, and final compaction.
