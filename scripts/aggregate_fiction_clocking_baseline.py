#!/usr/bin/env python3
"""Aggregate repeated fiction determine_clocking experiment JSON files.

The official fiction experiment writes one JSON data set named
``clock number assignment.json``.  The external-baseline runner archives one
copy per repetition as ``run_XX/results.json``; this script validates those
copies and emits tidy raw and summary tables without changing the upstream
measurements.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--runs-dir",
        type=Path,
        default=Path("build/artifacts/external_baselines/fiction_v0.7.0/walter2024"),
    )
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--expected-layouts", type=int, default=39)
    parser.add_argument("--expected-runs", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = args.output_dir or args.runs_dir
    run_files = sorted(args.runs_dir.glob("run_*/results.json"))
    if not run_files:
        raise SystemExit(f"no run_*/results.json files below {args.runs_dir}")
    if len(run_files) != args.expected_runs:
        raise SystemExit(f"expected {args.expected_runs} runs, found {len(run_files)}")

    raw_rows: list[dict[str, object]] = []
    versions: set[str] = set()
    expected_keys: set[tuple[str, str]] | None = None
    invariant_geometry: dict[tuple[str, str], tuple[int, ...]] = {}

    for repetition, path in enumerate(run_files, start=1):
        payload = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(payload, list) or len(payload) != 1:
            raise SystemExit(f"{path}: expected one fiction data set")
        dataset = payload[0]
        version = str(dataset["version"])
        versions.add(version)
        entries = dataset["entries"]
        if len(entries) != args.expected_layouts:
            raise SystemExit(
                f"{path}: expected {args.expected_layouts} layouts, got {len(entries)}"
            )

        keys: set[tuple[str, str]] = set()
        for entry in entries:
            benchmark = str(entry["benchmark"])
            scheme = str(entry["clocking scheme"])
            key = (benchmark, scheme)
            if key in keys:
                raise SystemExit(f"{path}: duplicate result {key}")
            keys.add(key)
            geometry = (
                int(entry["inputs"]),
                int(entry["outputs"]),
                int(entry["width [tiles]"]),
                int(entry["height [tiles]"]),
                int(entry["area [tiles]"]),
            )
            if key in invariant_geometry and invariant_geometry[key] != geometry:
                raise SystemExit(f"{path}: geometry changed for {key}")
            invariant_geometry[key] = geometry
            if geometry[2] * geometry[3] != geometry[4]:
                raise SystemExit(f"{path}: inconsistent area for {key}")

            raw_rows.append(
                {
                    "repetition": repetition,
                    "fiction_version": version,
                    "benchmark": benchmark,
                    "benchmark_function": benchmark.split("_ONE_", 1)[0],
                    "clocking_scheme": scheme,
                    "inputs": geometry[0],
                    "outputs": geometry[1],
                    "width_tiles": geometry[2],
                    "height_tiles": geometry[3],
                    "area_tiles": geometry[4],
                    "runtime_clocking_s": float(entry["runtime clocking [s]"]),
                    "equivalent": bool(entry["equivalent"]),
                }
            )

        if expected_keys is None:
            expected_keys = keys
        elif keys != expected_keys:
            raise SystemExit(f"{path}: result-key set differs from the first run")

    if len(versions) != 1:
        raise SystemExit(f"mixed fiction versions: {sorted(versions)}")

    groups: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
    for row in raw_rows:
        groups[(str(row["benchmark"]), str(row["clocking_scheme"]))].append(row)

    summary_rows: list[dict[str, object]] = []
    for key in sorted(groups):
        rows = groups[key]
        runtimes = [float(row["runtime_clocking_s"]) for row in rows]
        first = rows[0]
        summary_rows.append(
            {
                "benchmark": first["benchmark"],
                "benchmark_function": first["benchmark_function"],
                "clocking_scheme": first["clocking_scheme"],
                "inputs": first["inputs"],
                "outputs": first["outputs"],
                "width_tiles": first["width_tiles"],
                "height_tiles": first["height_tiles"],
                "area_tiles": first["area_tiles"],
                "runs": len(rows),
                "equivalent_runs": sum(bool(row["equivalent"]) for row in rows),
                "runtime_mean_s": statistics.fmean(runtimes),
                "runtime_median_s": statistics.median(runtimes),
                "runtime_stdev_s": statistics.stdev(runtimes) if len(runtimes) > 1 else 0.0,
                "runtime_min_s": min(runtimes),
                "runtime_p05_s": percentile(runtimes, 0.05),
                "runtime_p95_s": percentile(runtimes, 0.95),
                "runtime_max_s": max(runtimes),
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    for filename, rows in (("raw_results.csv", raw_rows), ("summary.csv", summary_rows)):
        with (output_dir / filename).open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    all_equivalent = all(bool(row["equivalent"]) for row in raw_rows)
    summary = {
        "scope": "official Walter et al. IEEE NANO 2024 combinational versatility layouts",
        "comparison_class": "external geometry/clock-assignment context; not sequential head-to-head",
        "fiction_revision_short": next(iter(versions)),
        "repetitions": len(run_files),
        "layouts_per_repetition": len(groups),
        "raw_measurements": len(raw_rows),
        "all_equivalent": all_equivalent,
        "equivalent_measurements": sum(bool(row["equivalent"]) for row in raw_rows),
        "result_files": [str(path) for path in run_files],
    }
    (output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if all_equivalent else 1


if __name__ == "__main__":
    raise SystemExit(main())
