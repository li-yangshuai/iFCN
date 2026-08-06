#!/usr/bin/env python3
"""Rebuild extended statistics from preserved physical-benchmark raw JSON."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import random
import statistics
from typing import Any, Sequence

import benchmark_physical_simulators as benchmark


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("result_directory", type=Path)
    parser.add_argument("--bootstrap-resamples", type=int, default=10000)
    parser.add_argument("--bootstrap-seed", type=int, default=20260717)
    return parser.parse_args()


def ranks(values: Sequence[float]) -> list[float]:
    ordered = sorted(range(len(values)), key=lambda index: values[index])
    result = [0.0] * len(values)
    position = 0
    while position < len(ordered):
        end = position + 1
        while end < len(ordered) and values[ordered[end]] == values[ordered[position]]:
            end += 1
        average_rank = 0.5 * (position + 1 + end)
        for selected in ordered[position:end]:
            result[selected] = average_rank
        position = end
    return result


def pearson(left: Sequence[float], right: Sequence[float]) -> float:
    if len(left) != len(right) or len(left) < 2:
        return math.nan
    left_mean = statistics.fmean(left)
    right_mean = statistics.fmean(right)
    numerator = sum((x - left_mean) * (y - right_mean)
                    for x, y in zip(left, right))
    left_sum = sum((x - left_mean) ** 2 for x in left)
    right_sum = sum((y - right_mean) ** 2 for y in right)
    denominator = math.sqrt(left_sum * right_sum)
    return numerator / denominator if denominator else math.nan


def timing_fields(comparison: dict[str, Any]) -> dict[str, float]:
    reference = sorted(float(value) for value in
                       comparison["reference_timing"]["raw_seconds"])
    candidate = sorted(float(value) for value in
                       comparison["candidate_timing"]["raw_seconds"])
    paired = [left / right for left, right in zip(
        comparison["reference_timing"]["raw_seconds"],
        comparison["candidate_timing"]["raw_seconds"])
              if float(right) > 0.0]
    paired.sort()
    return {
        "reference_q1_seconds": benchmark.percentile(reference, 0.25),
        "reference_q3_seconds": benchmark.percentile(reference, 0.75),
        "candidate_q1_seconds": benchmark.percentile(candidate, 0.25),
        "candidate_q3_seconds": benchmark.percentile(candidate, 0.75),
        "paired_speedup_median": statistics.median(paired) if paired else math.nan,
        "paired_speedup_q1": benchmark.percentile(paired, 0.25),
        "paired_speedup_q3": benchmark.percentile(paired, 0.75),
    }


def scaling(rows: Sequence[dict[str, Any]]) -> dict[str, Any]:
    cells = [float(row["total_cells"]) for row in rows]
    speedups = [float(row["speedup"]) for row in rows]
    candidates = sum(int(row["graph_candidate_checks"]) for row in rows)
    all_pairs = sum(int(row["graph_all_pair_checks"]) for row in rows)
    return {
        "spatial_candidate_checks": candidates,
        "all_pair_checks": all_pairs,
        "candidate_fraction": candidates / all_pairs if all_pairs else math.nan,
        "candidate_check_reduction_percent": (
            100.0 * (1.0 - candidates / all_pairs) if all_pairs else math.nan),
        "spearman_cells_vs_speedup": pearson(ranks(cells), ranks(speedups)),
        "pearson_log_cells_vs_speedup": pearson(
            [math.log(value) for value in cells], speedups),
    }


def main() -> int:
    args = arguments()
    directory = args.result_directory.expanduser().resolve()
    summary_path = directory / "summary.json"
    original_summary = json.loads(summary_path.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []
    for raw_path in sorted((directory / "raw").glob("*.json")):
        report = json.loads(raw_path.read_text(encoding="utf-8"))
        for comparison in report["comparisons"]:
            row = benchmark.flatten_comparison(
                Path(report["circuit"]), report, comparison)
            row.update(timing_fields(comparison))
            rows.append(row)
    if not rows:
        raise RuntimeError("no raw benchmark reports found")

    benchmark.write_rows(directory / "extended_results.csv", rows)
    rng = random.Random(args.bootstrap_seed)
    model_summaries: dict[str, Any] = {}
    for model in ("bistable", "coherence"):
        selected = [row for row in rows if row["model"] == model]
        if selected:
            values = benchmark.aggregate_model(
                selected, args.bootstrap_resamples, rng)
            values["scaling"] = scaling(selected)
            model_summaries[model] = values

    extended = {
        "schema_version": 1,
        "source_summary": str(summary_path),
        "raw_reports": len(list((directory / "raw").glob("*.json"))),
        "benchmark_protocol": original_summary.get("benchmark_protocol"),
        "requested_cpu_affinity": original_summary.get("requested_cpu_affinity"),
        "machine": original_summary.get("machine"),
        "models": model_summaries,
    }
    path = directory / "extended_summary.json"
    path.write_text(json.dumps(extended, indent=2) + "\n", encoding="utf-8")
    print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
