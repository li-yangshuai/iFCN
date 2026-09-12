#!/usr/bin/env python3
"""Validate and aggregate repeated runs of fiction_gold_subset."""

from __future__ import annotations

import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--runs-dir",
        type=Path,
        default=Path("build/artifacts/external_baselines/fiction_v0.7.0/gold_subset"),
    )
    parser.add_argument("--expected-runs", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    files = sorted(args.runs_dir.glob("run_*.csv"))
    if len(files) != args.expected_runs:
        raise SystemExit(f"expected {args.expected_runs} runs, found {len(files)}")

    rows: list[dict[str, object]] = []
    expected_benchmarks: set[str] | None = None
    invariants: dict[str, tuple[str, ...]] = {}
    for repetition, path in enumerate(files, start=1):
        with path.open(newline="", encoding="utf-8") as handle:
            current = list(csv.DictReader(handle))
        names = {row["benchmark"] for row in current}
        if len(names) != len(current):
            raise SystemExit(f"{path}: duplicate benchmark")
        if expected_benchmarks is None:
            expected_benchmarks = names
        elif names != expected_benchmarks:
            raise SystemExit(f"{path}: benchmark set differs from first run")

        for source in current:
            name = source["benchmark"]
            invariant = tuple(
                source[column]
                for column in (
                    "mode",
                    "cost",
                    "timeout_ms",
                    "status",
                    "timeout_boundary",
                    "width_tiles",
                    "height_tiles",
                    "area_tiles",
                    "gates",
                    "wires",
                    "crossings",
                    "equivalence",
                )
            )
            if name in invariants and invariants[name] != invariant:
                raise SystemExit(f"{path}: non-runtime result changed for {name}")
            invariants[name] = invariant
            width = int(source["width_tiles"])
            height = int(source["height_tiles"])
            area = int(source["area_tiles"])
            if width * height != area:
                raise SystemExit(f"{path}: inconsistent area for {name}")
            row: dict[str, object] = {"repetition": repetition}
            row.update(source)
            row["runtime_pnr_s"] = float(source["runtime_pnr_s"])
            rows.append(row)

    groups: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        groups[str(row["benchmark"])].append(row)

    summaries: list[dict[str, object]] = []
    for name in sorted(groups):
        group = groups[name]
        runtimes = [float(row["runtime_pnr_s"]) for row in group]
        first = group[0]
        summaries.append(
            {
                "benchmark": name,
                "mode": first["mode"],
                "cost": first["cost"],
                "timeout_ms": int(str(first["timeout_ms"])),
                "runs": len(group),
                "pass_runs": sum(row["status"] == "PASS" for row in group),
                "timeout_boundary_runs": sum(str(row["timeout_boundary"]) in {"1", "true"} for row in group),
                "strong_equivalence_runs": sum(row["equivalence"] == "STRONG" for row in group),
                "width_tiles": int(str(first["width_tiles"])),
                "height_tiles": int(str(first["height_tiles"])),
                "area_tiles": int(str(first["area_tiles"])),
                "gates": int(str(first["gates"])),
                "wires": int(str(first["wires"])),
                "crossings": int(str(first["crossings"])),
                "runtime_mean_s": statistics.fmean(runtimes),
                "runtime_median_s": statistics.median(runtimes),
                "runtime_stdev_s": statistics.stdev(runtimes) if len(runtimes) > 1 else 0.0,
                "runtime_min_s": min(runtimes),
                "runtime_max_s": max(runtimes),
            }
        )

    with (args.runs_dir / "raw_results.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    with (args.runs_dir / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summaries[0]))
        writer.writeheader()
        writer.writerows(summaries)

    valid = all(
        summary["pass_runs"] == len(files)
        and summary["strong_equivalence_runs"] == len(files)
        and summary["timeout_boundary_runs"] == 0
        for summary in summaries
    )
    payload = {
        "scope": "official fiction GOLD on a small shared combinational MNT subset",
        "comparison_class": "external combinational P&R context; not sequential head-to-head",
        "mode": "HIGH_EFFICIENCY",
        "cost": "AREA",
        "timeout_ms": 60000,
        "repetitions": len(files),
        "benchmarks": len(groups),
        "raw_measurements": len(rows),
        "all_pass_strong_equivalent_without_timeout": valid,
    }
    (args.runs_dir / "summary.json").write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
