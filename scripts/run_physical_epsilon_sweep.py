#!/usr/bin/env python3
"""Run real dielectric-constant simulations and summarize graph reuse.

Every epsilon value is simulated by both the baseline and accelerated kernels.
The reuse configuration exercises graph validation/materialization with a
precompiled parameter-independent topology.  Because each wrapper invocation
is a separate process, the reported amortized sweep composes the measured
kernel times with one median graph-compilation cost per circuit; it does not
pretend that an in-memory object survived between processes.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--benchmark-executable", type=Path,
                        default=Path("build-release/ifcn_physical_benchmark"))
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--epsilon-values", nargs="+", type=float,
                        default=(6.5, 9.7, 12.9, 16.1, 20.0))
    parser.add_argument("--model", choices=("bistable", "coherence", "both"),
                        default="both")
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--bootstrap-resamples", type=int, default=5000)
    parser.add_argument("--samples", type=int)
    parser.add_argument("--duration", type=float)
    parser.add_argument("--time-step", type=float)
    parser.add_argument("--numeric-method", choices=("euler", "rk4"))
    parser.add_argument("--cpu-affinity")
    parser.add_argument("--verify-internal-state", action="store_true")
    args = parser.parse_args()
    if args.repetitions <= 0 or args.warmup < 0:
        parser.error("repetitions must be positive and warmup non-negative")
    if not args.epsilon_values or any(value <= 0.0 or not math.isfinite(value)
                                      for value in args.epsilon_values):
        parser.error("epsilon values must be finite and positive")
    if len(set(args.epsilon_values)) != len(args.epsilon_values):
        parser.error("epsilon values must be unique")
    return args


def epsilon_label(value: float) -> str:
    return format(value, ".8g").replace("-", "m").replace(".", "p")


def load_rows(path: Path, epsilon: float, mode: str) -> list[dict[str, Any]]:
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    for row in rows:
        row["epsilon_r"] = epsilon
        row["sweep_graph_mode"] = mode
    return rows


def numeric(row: dict[str, Any], name: str) -> float:
    return float(row[name])


def compose_model(rows: list[dict[str, Any]], model: str) -> dict[str, Any]:
    selected = [row for row in rows if row["model"] == model]
    reuse = [row for row in selected if row["sweep_graph_mode"] == "reuse"]
    cold = [row for row in selected if row["sweep_graph_mode"] == "spatial"]
    circuits = sorted({row["circuit"] for row in reuse})
    epsilon_values = sorted({numeric(row, "epsilon_r") for row in reuse})

    baseline_seconds = sum(numeric(row, "reference_median_seconds")
                           for row in reuse)
    reused_kernel_seconds = sum(numeric(row, "candidate_median_seconds")
                                for row in reuse)
    cold_candidate_seconds = sum(numeric(row, "candidate_median_seconds")
                                 for row in cold)
    one_compile_per_circuit = 0.0
    for circuit in circuits:
        costs = [numeric(row, "graph_precompile_seconds") for row in reuse
                 if row["circuit"] == circuit]
        one_compile_per_circuit += statistics.median(costs)
    amortized_seconds = reused_kernel_seconds + one_compile_per_circuit

    return {
        "circuits": len(circuits),
        "epsilon_values": epsilon_values,
        "real_simulation_pairs": len(reuse),
        "all_outputs_exact": all(numeric(row, "max_absolute_error") == 0.0
                                 for row in selected),
        "all_internal_state_equivalent": all(
            row["internal_state_checked"].lower() != "true" or
            row["internal_state_equivalent"].lower() == "true"
            for row in selected),
        "baseline_sweep_seconds": baseline_seconds,
        "cold_spatial_candidate_seconds": cold_candidate_seconds,
        "reused_kernel_seconds": reused_kernel_seconds,
        "one_graph_compile_per_circuit_seconds": one_compile_per_circuit,
        "composed_amortized_reuse_seconds": amortized_seconds,
        "cold_spatial_sweep_speedup": (
            baseline_seconds / cold_candidate_seconds
            if cold_candidate_seconds > 0.0 else math.nan),
        "amortized_reuse_sweep_speedup": (
            baseline_seconds / amortized_seconds
            if amortized_seconds > 0.0 else math.nan),
        "precompile_fraction_of_amortized_candidate": (
            one_compile_per_circuit / amortized_seconds
            if amortized_seconds > 0.0 else math.nan),
    }


def main() -> int:
    args = arguments()
    wrapper = Path(__file__).with_name("benchmark_physical_simulators.py")
    output = args.output_directory.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)

    optional: list[str] = []
    for name, flag in (("samples", "--samples"),
                       ("duration", "--duration"),
                       ("time_step", "--time-step"),
                       ("numeric_method", "--numeric-method")):
        value = getattr(args, name)
        if value is not None:
            optional.extend((flag, str(value)))
    if args.verify_internal_state:
        optional.append("--verify-internal-state")
    if args.cpu_affinity:
        optional.extend(("--cpu-affinity", args.cpu_affinity))

    all_rows: list[dict[str, Any]] = []
    run_summaries: list[dict[str, Any]] = []
    for epsilon in args.epsilon_values:
        for mode in ("spatial", "reuse"):
            run_output = output / f"epsilon_{epsilon_label(epsilon)}_{mode}"
            command = [
                sys.executable, str(wrapper),
                *[str(path) for path in args.inputs],
                "--benchmark-executable", str(args.benchmark_executable),
                "--output-directory", str(run_output),
                "--model", args.model,
                "--graph-mode", mode,
                "--epsilon-r", str(epsilon),
                "--repetitions", str(args.repetitions),
                "--warmup", str(args.warmup),
                "--bootstrap-resamples", str(args.bootstrap_resamples),
                "--require-equivalent", "--fail-fast",
                *optional,
            ]
            print(f"epsilon={epsilon:g}, graph_mode={mode}", flush=True)
            subprocess.run(command, check=True)
            summary = json.loads((run_output / "summary.json").read_text(
                encoding="utf-8"))
            run_summaries.append({
                "epsilon_r": epsilon,
                "graph_mode": mode,
                "models": summary["models"],
            })
            all_rows.extend(load_rows(run_output / "results.csv", epsilon, mode))

    combined_csv = output / "epsilon_sweep_results.csv"
    with combined_csv.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(all_rows[0]))
        writer.writeheader()
        writer.writerows(all_rows)

    models = {
        model: compose_model(all_rows, model)
        for model in ("bistable", "coherence")
        if any(row["model"] == model for row in all_rows)
    }
    combined = {
        "schema_version": 1,
        "method_note": (
            "All epsilon points were simulated. Amortized reuse composes "
            "measured reused-kernel times with one median measured graph "
            "compilation per circuit; wrapper processes do not share memory."),
        "repetitions": args.repetitions,
        "warmup": args.warmup,
        "verify_internal_state": args.verify_internal_state,
        "runs": run_summaries,
        "models": models,
    }
    summary_path = output / "epsilon_sweep_summary.json"
    summary_path.write_text(json.dumps(combined, indent=2, ensure_ascii=False) + "\n",
                            encoding="utf-8")
    print(f"combined results: {combined_csv}")
    print(f"combined summary: {summary_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"epsilon sweep failed: {error}", file=sys.stderr)
        raise SystemExit(2)
