#!/usr/bin/env python3
"""Emit a machine-readable fairness audit for external FCN baselines.

The audit intentionally rejects a lossy FGL conversion as a sequential
head-to-head interface.  It still permits geometry/runtime context on shared
combinational benchmarks.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_tokens(path: Path, tokens: tuple[str, ...]) -> None:
    text = path.read_text(encoding="utf-8")
    missing = [token for token in tokens if token not in text]
    if missing:
        raise SystemExit(f"{path}: missing audited source tokens {missing}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fiction-source",
        type=Path,
        default=Path("build/tools/external/fiction-v0.7.0"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/artifacts/external_baselines/fiction_v0.7.0"),
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path.cwd().resolve()
    fiction = args.fiction_source.resolve()
    output_dir = args.output_dir.resolve()
    evidence = {
        "ifcn_cyclic_export": root / "src/app/ifcn_paper_cyclic_pnr.cpp",
        "ifcn_global_solver": root / "include/autopr/sequential/globalPhaseSolver.cpp",
        "fiction_determine_clocking": fiction
        / "include/fiction/algorithms/physical_design/determine_clocking.hpp",
        "fiction_gold": fiction
        / "include/fiction/algorithms/physical_design/graph_oriented_layout_design.hpp",
        "fiction_fgl_reader": fiction / "include/fiction/io/read_fgl_layout.hpp",
    }
    missing = [str(path) for path in evidence.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing audited source files: {missing}")

    require_tokens(
        evidence["ifcn_cyclic_export"],
        ("iteration_distance", "initiation_interval", "epoch_variable"),
    )
    require_tokens(
        evidence["ifcn_global_solver"],
        ("occurrenceEpoch", "initiationInterval", "iterationDistance"),
    )
    require_tokens(
        evidence["fiction_determine_clocking"],
        ("number_of_clocks", "incoming_data_flow<false>", "% number_of_clocks"),
    )
    require_tokens(evidence["fiction_gold"], ("twoddwave_clocking<Lyt>()",))

    common = {
        "clock_assignment_after_layout": "no",
        "sequential_state_contract": "no",
        "iteration_distance": "no",
        "absolute_epoch": "no",
        "initiation_interval": "no",
        "fair_sequential_head_to_head": "no",
    }
    rows = [
        {
            "method": "iFCN cyclic sequential P&R",
            "release_or_paper": "current workspace",
            "role": "target method",
            "input_contract": "sequential RTL/SeqIR with state and feedback arcs",
            "placement_and_routing": "yes",
            "clock_assignment_after_layout": "yes",
            "sequential_state_contract": "yes",
            "iteration_distance": "yes",
            "absolute_epoch": "yes",
            "initiation_interval": "yes",
            "fair_shared_combinational_geometry": "yes",
            "fair_sequential_head_to_head": "yes",
            "permitted_comparison": "sequential P&R/clock/mapping/simulation under the iFCN contract",
            "exclusion_reason": "",
        },
        {
            **common,
            "method": "fiction determine_clocking / Walter 2024",
            "release_or_paper": "fiction v0.7.0; IEEE NANO 2024",
            "role": "external clock-assignment context",
            "input_contract": "already placed/routed FGL gate-level layout",
            "placement_and_routing": "no",
            "clock_assignment_after_layout": "yes",
            "fair_shared_combinational_geometry": "yes",
            "permitted_comparison": "layout dimensions, SAT clock-assignment success, same-host runtime",
            "exclusion_reason": (
                "local modulo clock numbers do not encode register boundaries, iteration "
                "distance, absolute epochs, or II"
            ),
        },
        {
            **common,
            "method": "fiction GOLD / A* is Born",
            "release_or_paper": "fiction v0.7.0; IEEE NANO 2024",
            "role": "external combinational P&R context",
            "input_contract": "combinational technology network",
            "placement_and_routing": "yes",
            "fair_shared_combinational_geometry": "yes",
            "permitted_comparison": (
                "area/wires/crossings/runtime on identical combinational Boolean functions"
            ),
            "exclusion_reason": (
                "emits 2DDWave combinational layouts without state, feedback-iteration, "
                "absolute-epoch, or II semantics"
            ),
        },
        {
            **common,
            "method": "MNT Bench v0.3.8",
            "release_or_paper": "official benchmark/layout library",
            "role": "dataset, not an algorithm",
            "input_contract": "combinational Boolean networks and pre-generated layouts",
            "placement_and_routing": "not_applicable",
            "fair_shared_combinational_geometry": "yes",
            "permitted_comparison": "shared combinational functions and reference geometries",
            "exclusion_reason": (
                "the selected Walter-2024 corpus contains no sequential timing contract"
            ),
        },
    ]

    if any(
        row["fair_sequential_head_to_head"] == "yes" and row["role"] != "target method"
        for row in rows
    ):
        raise SystemExit("an external baseline was incorrectly marked sequential-head-to-head")

    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / "comparison_scope.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)

    json_path = output_dir / "comparison_scope.json"
    payload = {
        "decision": (
            "No reproducible external sequential head-to-head is available from the "
            "audited fiction/MNT interfaces. Use them only as combinational geometry "
            "and clock-assignment context."
        ),
        "fgl_conversion_policy": (
            "Do not convert iFCN cyclic output to FGL for a sequential comparison: "
            "the conversion would erase state boundaries, iteration distance, absolute "
            "epoch, and initiation interval."
        ),
        "methods": rows,
        "source_evidence": {
            name: {
                "path": str(path.relative_to(root) if path.is_relative_to(root) else path),
                "sha256": sha256(path),
            }
            for name, path in evidence.items()
        },
        "primary_sources": {
            "fiction": "https://github.com/cda-tum/fiction",
            "mnt_bench": "https://github.com/cda-tum/mnt-bench",
            "walter_2024": "https://doi.org/10.1109/NANO61778.2024.10628908",
            "a_star_is_born": "https://doi.org/10.1109/NANO61778.2024.10628808",
        },
    }
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {csv_path} and {json_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
