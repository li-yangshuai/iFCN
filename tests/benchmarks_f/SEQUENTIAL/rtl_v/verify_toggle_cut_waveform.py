#!/usr/bin/env python3
"""Independently verify the mapped toggle next-state cone: d = not q."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def read_vectors(path: Path) -> tuple[list[str], list[list[int]]]:
    lines = [line.strip() for line in path.read_text().splitlines() if line.strip()]
    if len(lines) < 2:
        raise ValueError("vector table is empty")
    names = [field.strip() for field in lines[0].split(",")]
    rows: list[list[int]] = []
    for line in lines[1:]:
        row = [int(field.strip()) for field in line.split(",")]
        if len(row) != len(names) or any(value not in (0, 1) for value in row):
            raise ValueError(f"invalid vector row: {line}")
        rows.append(row)
    return names, rows


def read_traces(path: Path) -> dict[str, list[float]]:
    text = path.read_text()
    traces: dict[str, list[float]] = {}
    pattern = re.compile(
        r"\[TRACE\]\s+data_labels=([^\n]+).*?"
        r"\[TRACE_DATA\]\s+(.*?)\s+\[#TRACE_DATA\]",
        re.DOTALL,
    )
    for match in pattern.finditer(text):
        traces[match.group(1).strip()] = [
            float(value) for value in match.group(2).split()
        ]
    if not traces:
        raise ValueError("RST file contains no traces")
    return traces


def verify(
    rst: Path,
    vectors: Path,
    threshold: float,
    initiation_interval_epochs: int = 4,
) -> dict[str, object]:
    names, rows = read_vectors(vectors)
    traces = read_traces(rst)
    if names != ["q"]:
        raise ValueError("toggle cut verifier requires the single vector input q")
    if "d" not in traces:
        raise ValueError("RST file has no output trace named d")

    output = traces["d"]
    if len(output) % len(rows) != 0:
        raise ValueError("sample count is not divisible by the vector count")
    samples_per_vector = len(output) // len(rows)
    if samples_per_vector < 8:
        raise ValueError("too few samples per input vector")

    if initiation_interval_epochs <= 0 or initiation_interval_epochs % 4 != 0:
        raise ValueError("initiation interval must be a positive multiple of four")
    logical_periods = initiation_interval_epochs // 4
    # The physical-cell clock solve may require more than one four-phase
    # period.  Scale the established switching/release window with the actual
    # solved II instead of silently assuming the former coarse-grid II=4.
    minimum_delay = (3 * samples_per_vector * logical_periods) // 4
    maximum_delay = min(
        len(output) - 1,
        (3 * samples_per_vector * logical_periods) // 2,
    )
    candidates: list[tuple[float, int, int, int]] = []
    for delay in range(minimum_delay, maximum_delay + 1):
        matched = 0
        stable = 0
        for sample, polarization in enumerate(output):
            source_sample = sample - delay
            if source_sample < 0 or abs(polarization) < threshold:
                continue
            source_row = min(source_sample // samples_per_vector, len(rows) - 1)
            expected_sign = 1.0 if rows[source_row][0] == 0 else -1.0
            stable += 1
            matched += int(polarization * expected_sign > 0.0)
        agreement = matched / stable if stable else 0.0
        candidates.append((agreement, stable, -abs(delay - samples_per_vector), delay))

    agreement, stable, _, delay = max(candidates)
    minimum_stable = len(output) // 4
    if agreement != 1.0 or stable < minimum_stable:
        raise AssertionError(
            f"mapped NOT failed: agreement={agreement:.6f}, "
            f"stable={stable}, required={minimum_stable}, delay={delay}"
        )
    if {row[0] for row in rows} != {0, 1}:
        raise AssertionError("vector table does not exercise both q values")

    return {
        "status": "pass",
        "function": "d = not q",
        "samples": len(output),
        "samples_per_vector": samples_per_vector,
        "initiation_interval_epochs": initiation_interval_epochs,
        "delay_samples": delay,
        "stable_samples": stable,
        "stable_sign_agreement": agreement,
        "threshold": threshold,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rst", type=Path)
    parser.add_argument("vectors", type=Path)
    parser.add_argument("--threshold", type=float, default=0.8)
    parser.add_argument("--layout-report", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    initiation_interval_epochs = 4
    if args.layout_report:
        layout_report = json.loads(args.layout_report.read_text())
        initiation_interval_epochs = int(layout_report["initiation_interval"])
    report = verify(
        args.rst,
        args.vectors,
        args.threshold,
        initiation_interval_epochs,
    )
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
