# Sequential RTL examples

This directory contains small, self-contained inputs for the experimental iFCN
sequential design flow. They are not accepted by the legacy `Parse` front-end.

The first v0 example is [`counter2_sync`](counter2_sync/): a two-bit modulo-4
counter with a positive-edge clock, synchronous active-high reset, and enable.
It includes RTL, explicit constraints, a guarded Yosys script, a target SeqIR
example, a self-checking testbench, and a golden trace.

The physical-design micro fixture is
[`toggle1_random_clock`](toggle1_random_clock/): a one-bit feedback loop for the
geometry-first random-clock flow.  Its direct route is provably too short for a
four-epoch recurrence; a phase-blind dogleg repair admits a checked post-route
phase/epoch solution with `II=4`.

The maintained flow contract, implementation map, commands, experiment
results, and known limitations are documented in the repository-level
[`handoff.md`](../../../handoff.md).

Generated JSON, logs, waveforms, IFCN files, and images must be written under a
build directory rather than committed beside these source fixtures.
