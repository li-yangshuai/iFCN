#!/usr/bin/env python3
"""Run the reproducible cold-spatial/all-pairs/reuse QCA ablation matrix."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--benchmark-executable", type=Path,
                        default=Path("build-release/ifcn_physical_benchmark"))
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--model", choices=("bistable", "coherence", "both"),
                        default="both")
    parser.add_argument("--repetitions", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--bootstrap-resamples", type=int, default=5000)
    parser.add_argument("--cpu-affinity",
                        help="Linux taskset CPU list forwarded to the wrapper")
    parser.add_argument("--samples", type=int)
    parser.add_argument("--duration", type=float)
    parser.add_argument("--time-step", type=float)
    parser.add_argument("--numeric-method", choices=("euler", "rk4"))
    parser.add_argument("--verify-internal-state", action="store_true")
    parser.add_argument("--include-coherence-kernel-ablations", action="store_true",
                        help="also disable clock/input caches and fusion individually")
    args = parser.parse_args()
    if args.repetitions <= 0 or args.warmup < 0:
        parser.error("repetitions must be positive and warmup non-negative")
    return args


def sum_column(path: Path, model: str, column: str) -> float:
    with path.open(encoding="utf-8", newline="") as source:
        return sum(float(row[column]) for row in csv.DictReader(source)
                   if row["model"] == model)


def main() -> int:
    args = arguments()
    script = Path(__file__).with_name("benchmark_physical_simulators.py")
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

    configurations: list[tuple[str, str, list[str]]] = [
        ("spatial", "spatial", []),
        ("all_pairs", "all-pairs", []),
        ("reuse", "reuse", []),
    ]
    if args.include_coherence_kernel_ablations:
        configurations.extend((
            ("no_clock_cache", "spatial", ["--disable-clock-cache"]),
            ("no_input_cache", "spatial", ["--disable-input-cache"]),
            ("no_generator_caches", "spatial",
             ["--disable-clock-cache", "--disable-input-cache"]),
            ("no_fusion", "spatial", ["--disable-fusion"]),
            ("all_kernel_optimizations_disabled", "spatial", [
                "--disable-clock-cache", "--disable-input-cache",
                "--disable-fusion",
            ]),
        ))

    summaries: dict[str, Any] = {}
    for name, graph_mode, ablation_flags in configurations:
        configuration_model = "coherence" if ablation_flags else args.model
        command = [
            sys.executable, str(script),
            *[str(path) for path in args.inputs],
            "--benchmark-executable", str(args.benchmark_executable),
            "--output-directory", str(output / name),
            "--model", configuration_model,
            "--graph-mode", graph_mode,
            "--repetitions", str(args.repetitions),
            "--warmup", str(args.warmup),
            "--bootstrap-resamples", str(args.bootstrap_resamples),
            "--require-equivalent",
            "--fail-fast",
            *ablation_flags,
            *optional,
        ]
        print(f"running configuration: {name}", flush=True)
        subprocess.run(command, check=True)
        summaries[name] = json.loads(
            (output / name / "summary.json").read_text(encoding="utf-8"))

    rows: list[dict[str, Any]] = []
    selected_models = ("bistable", "coherence")
    for name, graph_mode, _ in configurations:
        for model in selected_models:
            values = summaries[name]["models"].get(model)
            if values is None:
                continue
            interval = values["bootstrap_95_percent_ci"][
                "geometric_mean_speedup"]
            suite_interval = values["bootstrap_95_percent_ci"][
                "suite_speedup"]
            rows.append({
                "configuration": name,
                "graph_mode": graph_mode,
                "model": model,
                "circuits": values["circuits"],
                "suite_speedup": values["suite_speedup"],
                "suite_speedup_ci_low": (
                    suite_interval[0] if suite_interval else math.nan),
                "suite_speedup_ci_high": (
                    suite_interval[1] if suite_interval else math.nan),
                "geometric_mean_speedup": values["geometric_mean_speedup"],
                "geometric_mean_ci_low": interval[0] if interval else math.nan,
                "geometric_mean_ci_high": interval[1] if interval else math.nan,
                "median_speedup": values["median_speedup"],
                "speedup_q1": values["speedup_q1"],
                "speedup_q3": values["speedup_q3"],
                "minimum_speedup": values["minimum_speedup"],
                "maximum_speedup": values["maximum_speedup"],
                "circuits_faster": values["circuits_faster_than_baseline"],
                "circuits_slower": values["circuits_slower_than_baseline"],
                "sign_test_p_value": values["two_sided_sign_test_p_value"],
                "minimum_total_cells": values["minimum_total_cells"],
                "maximum_total_cells": values["maximum_total_cells"],
                "maximum_peak_process_rss_kib":
                    values["maximum_peak_process_rss_kib"],
                "all_internal_state_equivalent":
                    values["all_internal_state_equivalent"],
                "internal_state_frames": values["internal_state_frames"],
                "internal_state_values": values["internal_state_values"],
                "interaction_graph_sum_seconds": sum_column(
                    output / name / "results.csv",
                    model, "interaction_graph_seconds"),
                "precompile_sum_seconds": sum_column(
                    output / name / "results.csv",
                    model, "graph_precompile_seconds"),
            })

    with (output / "ablation_summary.csv").open(
            "w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    combined = {
        "schema_version": 2,
        "repetitions": args.repetitions,
        "warmup": args.warmup,
        "verify_internal_state": args.verify_internal_state,
        "cpu_affinity": args.cpu_affinity,
        "rows": rows,
    }
    (output / "ablation_summary.json").write_text(
        json.dumps(combined, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"combined summary: {output / 'ablation_summary.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ablation matrix failed: {error}", file=sys.stderr)
        raise SystemExit(2)
