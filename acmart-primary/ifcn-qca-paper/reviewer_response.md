# Reviewer-style questions and implemented responses

This document records which review risks were closed by code/experiments and
which remain genuine limitations. “Partially resolved” is intentional: an
unavailable external simulator or an unrun 30-repetition production matrix is
not replaced with an extrapolated claim.

## 1. What is new beyond QCADesigner, the 2019 object-model paper, and iFCN?

**Resolved.** The paper now identifies Peng et al. (Electronics Letters 2019,
DOI `10.1049/el.2019.1861`) as the direct predecessor. Its C++ metaprogrammed
QCA/NML object model, dynamic properties, reimplemented Bistable engine,
QCADesigner XOR comparison, and reported average 10x gain are explicitly
treated as prior work. The new scope is ordered physical-interaction
compilation, epsilon-independent geometry, packed Bistable and coherence
kernels, bounded caches/fusion, exact full-trajectory checking, and the new
experiments. The current 3.982x/4.378x effects use the generic-object engine as
their denominator and are not multiplied by the prior 10x result.

Evidence: `paper.tex`, Table 1 and “Relation to the Prior Object-Model
Algorithm”; `references.bib` entry `peng2019`.

## 2. Is the performance comparison against a fair baseline?

**Resolved for the incremental software claim.** Baseline and candidate share
the same parser, options, seed, time grid, compiler flags, and process. They are
run in paired, alternating AB/BA order; compilation is included in end-to-end
time. A frozen pre-optimization checkout is also compiled independently by
`run_legacy_simon_crosscheck.py`.

**Not fully resolved for QCADesigner/QCADesigner-E.** No executable is
installed in this environment and GTK2 development dependencies are absent.
The paper therefore cites the predecessor's QCADesigner comparison but does
not claim a fresh external-tool validation.

## 3. Can a 128-bit non-cryptographic digest prove exact equivalence?

**Resolved.** It is no longer the decision oracle. The untimed verifier retains
both complete IEEE-754 word sequences and compares them directly. The digest
is only an artifact identifier, and the JSON schema says
`comparison=exact_ieee754_word_sequence`.

Evidence: `include/simon/SimulationTrace.hpp` and
`src/app/ifcn_physical_benchmark.cpp`.

## 4. How is the kink-energy refactoring independently checked?

**Resolved at the implementation level; physical external validity remains
open.** `OrderedInteractionGraphUnitTest.cpp` compares every spatial coupling,
neighbor order, numerator, and materialized energy with the all-pairs/direct
16-dot formula at epsilon values 6.5, 9.7, 12.9, 16.1, and 20.0. It covers the
1172-cell design. This guards against a shared graph-construction error, but it
does not replace comparison with fabricated-device data or a current external
physics tool.

## 5. Are three repetitions and shortened grids sufficient?

**Resolved for the short-grid primary endpoint.** The primary study now has 25
circuits, two warm-ups, and 30 paired repetitions. It reports suite and
geometric effects, median/IQR/range, 10,000-resample circuit bootstrap CIs, and
a two-sided sign test. All 25 circuits are faster for both models.

**Partially resolved for production grids.** A 1172-cell probe runs 12,800
Bistable samples and 40,961 coherence steps, but only once. It is labeled a
non-inferential scaling probe, not a 30-repetition production result.

## 6. Is spatial pruning actually responsible for the speedup?

**Resolved.** The all-pairs candidate still obtains 3.884x/4.314x, while the
spatial candidate obtains 3.962x/4.342x in the ten-repetition matrix. Thus
packed execution is the dominant mechanism and spatial construction mainly
reduces initialization. Spatial buckets perform only 5.63%/7.93% of possible
pair checks. This negative result is reported rather than hidden.

## 7. Is graph reuse tested with real epsilon simulations?

**Resolved.** Five epsilon values are simulated, not merely rematerialized, on
six size-stratified circuits with ten repetitions. Both cold and reuse modes
run at every point, every output/internal trace is exact, and one graph compile
per circuit is included in the amortized endpoint. Measured sweep speedups are
4.056x and 4.461x.

Evidence: `experiments/physical_review_response_20260717/epsilon_sweep_short10/epsilon_sweep_summary.json`.

## 8. Is the Runge--Kutta path validated or mislabeled?

**Partially resolved.** Euler and the existing component-wise Runge--Kutta path
both pass exact output and full-internal-state tests. The manuscript explicitly
does not call the latter a fully coupled classical RK4 method. It is not part
of the 30-repetition timing matrix; a fully coupled RK4 model-quality study
remains future work.

## 9. What is the memory cost and what happens when caches do not fit?

**Partially resolved.** Peak process RSS is 67.7 MiB for the production probe
without trace retention and 410.9 MiB during complete-word verification. A
configurable cache budget defaults to 512 MiB; zero-budget tests prove safe
fallback to generators with an identical trace. A candidate-only RSS delta is
not available because both implementations execute in one process.

## 10. Does equivalence survive another compiler?

**Resolved for one additional build, not universally.** GCC 9.4 and Clang 10
Release builds pass all nine tests. On the 1172-cell case, baseline and
accelerated waveform SHA-256 values match within and across compilers for both
engines. Different architectures, math libraries, and standard libraries
remain outside this evidence.

Evidence: `experiments/physical_review_response_20260717/cross_compiler/`.

## 11. Are timing controls adequate?

**Mostly resolved.** Benchmark children are pinned to CPU 0, use two warm-ups
and alternating AB/BA paired order, and retain all 30 raw times. Compiler,
Release/fast-math state, CPU, kernel, and requested affinity are recorded.
WSL2 exposes no scaling-governor record, and hardware counters were not
collected; both are stated as limitations.

## 12. Does this overlap with the published iFCN pipeline?

**Resolved.** The paper says iFCN covers RTL optimization, placement, routing,
clocking, and mapping. This work begins after a cell layout is parsed. It also
separately credits the 2019 object-model paper for the closer software
lineage.

## 13. Are negative and fallback cases tested?

**Resolved.** Tests include a missing design, graph invalidation by radius and
coordinate changes, cold recompilation after an incompatible cache, a zero
cache budget, spatial/all-pairs modes, and all coherence optimizations
disabled. Aggregation fails closed on parsing, signal compatibility, output,
state, or repetition failure.

## 14. Is exact geometry validation robust to reparsing?

**Resolved for deterministic parsing.** Reparsing the same file is accepted;
changing epsilon alone is accepted; changing a center coordinate, interaction
radius, or layer separation is rejected. Exact comparison is conservative:
serialization that changes a floating-point coordinate may produce a safe
false miss and recompile, never a false cache hit.

## 15. Is there a complete component ablation?

**Resolved.** The ten-repetition matrix contains spatial, all-pairs, reuse, no
clock cache, no input cache, no generator caches, no fusion, and all kernel
optimizations disabled. Input caching is nearly neutral; clock caching and
fusion are material; the packed graph alone retains 2.586x.

Evidence: `experiments/physical_review_response_20260717/ablation_short10/ablation_summary.json`.

## 16. What did the frozen legacy comparison reveal?

**Resolved and reported as a negative result.** The pre-optimization Bistable
engine seeds from `random_device` and uses a last-visited-cell convergence
assignment, so it is not a deterministic exact oracle. Coherence is within
3.3e-9 on XOR and byte-identical on the largest circuit; Bistable logic varies
on XOR but agrees on the largest case in the recorded run. This evidence
motivates the deterministic seeded baseline and prevents an overstated
cross-version claim.

Evidence: `experiments/physical_review_response_20260717/legacy_crosscheck/legacy_crosscheck.json`.

## 17. Is this an academic contribution or only an engineering optimization?

**Resolved in the manuscript framing.** The paper now defines
discrete-trace-preserving compilation as an optimization problem under an
IEEE-754 trajectory constraint. It proves ordered-topology preservation,
material-parameter separability, and recurrence-level trajectory equivalence.
Four falsifiable hypotheses connect these claims to exact-state tests,
candidate-count measurements, controlled kernel ablations, and real dielectric
sweeps. Two TikZ figures expose the parametric compiler and the canonical-order
algorithm. These formal and empirical claims replace a feature-by-feature
implementation narrative; limitations still distinguish software equivalence
from external physical validity.

## Remaining work before a strong journal submission

1. Install and script QCADesigner/QCADesigner-E for a fresh, multi-circuit
   external physical comparison.
2. Run the production grids with 30 paired repetitions rather than one
   largest-circuit probe.
3. Add public suites from independent sources and a second hardware/OS family.
4. Record candidate-only memory and hardware counters.
5. Treat a fully coupled RK4 solver and material-calibrated epsilon ranges as
   separate physical-model extensions.
