#!/usr/bin/env python3
"""Join Walter et al. 2024 Table I with the pinned same-host rerun.

Runtime ratios are intentionally not computed: the paper and this rerun use
different hosts, and the paper reports time only to two decimal places.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


BENCHMARKS = [
    "mux21",
    "xnor2",
    "par_gen",
    "t",
    "t_5",
    "par_check",
    "clpl",
    "newtag",
    "majority_5_r1",
    "xor5_r1",
    "cm82a_5",
    "xor5Maj",
    "parity",
]

TABLE_I_DIMENSIONS = {
    "2DDWAVE": [
        (4, 3),
        (3, 6),
        (7, 9),
        (8, 8),
        (8, 8),
        (9, 9),
        (6, 20),
        (11, 11),
        (11, 11),
        (14, 14),
        (25, 25),
        (30, 43),
        (48, 48),
    ],
    "USE": [
        (5, 5),
        (6, 6),
        (9, 9),
        (10, 10),
        (10, 10),
        (11, 11),
        (15, 15),
        (13, 13),
        (13, 13),
        (16, 16),
        (35, 35),
        (45, 45),
        (70, 70),
    ],
    "RES": [
        (5, 5),
        (6, 6),
        (11, 11),
        (12, 12),
        (12, 12),
        (15, 15),
        (18, 18),
        (15, 15),
        (15, 15),
        (20, 20),
        (50, 50),
        (75, 75),
        (110, 110),
    ],
}

TABLE_I_NONZERO_RUNTIME = {
    ("2DDWAVE", "xor5Maj"): 0.02,
    ("2DDWAVE", "parity"): 0.04,
    ("USE", "cm82a_5"): 0.03,
    ("USE", "xor5Maj"): 0.07,
    ("USE", "parity"): 0.42,
    ("RES", "cm82a_5"): 0.07,
    ("RES", "xor5Maj"): 0.33,
    ("RES", "parity"): 1.48,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reproduced",
        type=Path,
        default=Path(
            "build/artifacts/external_baselines/fiction_v0.7.0/walter2024/summary.csv"
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "build/artifacts/external_baselines/fiction_v0.7.0/walter2024/"
            "reported_vs_reproduced.csv"
        ),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with args.reproduced.open(newline="", encoding="utf-8") as handle:
        reproduced = {
            (row["clocking_scheme"], row["benchmark_function"]): row
            for row in csv.DictReader(handle)
        }

    rows: list[dict[str, object]] = []
    for scheme, dimensions in TABLE_I_DIMENSIONS.items():
        for benchmark, (width, height) in zip(BENCHMARKS, dimensions, strict=True):
            key = (scheme, benchmark)
            if key not in reproduced:
                raise SystemExit(f"missing reproduced row {key}")
            actual = reproduced[key]
            reported_area = width * height
            area_match = int(actual["area_tiles"]) == reported_area
            dimension_match = (
                int(actual["width_tiles"]) == width and int(actual["height_tiles"]) == height
            ) or (
                int(actual["width_tiles"]) == height and int(actual["height_tiles"]) == width
            )
            rows.append(
                {
                    "benchmark": benchmark,
                    "clocking_scheme": scheme,
                    "reported_width_tiles": width,
                    "reported_height_tiles": height,
                    "reported_area_tiles": reported_area,
                    "reported_runtime_s_2dp": TABLE_I_NONZERO_RUNTIME.get(key, 0.0),
                    "reproduced_width_tiles": int(actual["width_tiles"]),
                    "reproduced_height_tiles": int(actual["height_tiles"]),
                    "reproduced_area_tiles": int(actual["area_tiles"]),
                    "reproduced_runtime_mean_s": float(actual["runtime_mean_s"]),
                    "reproduced_runtime_median_s": float(actual["runtime_median_s"]),
                    "area_match": area_match,
                    "dimensions_match_allowing_rotation": dimension_match,
                    "runtime_ratio_reported": "NA_DIFFERENT_HOST_AND_REPORTED_ROUNDING",
                    "source": "Walter et al. IEEE NANO 2024 Table I",
                }
            )

    if not all(row["area_match"] and row["dimensions_match_allowing_rotation"] for row in rows):
        raise SystemExit("one or more Table I geometries did not reproduce")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} matched Table I rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
