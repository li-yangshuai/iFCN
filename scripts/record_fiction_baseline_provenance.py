#!/usr/bin/env python3
"""Record source, data, license, and host provenance for fiction baselines."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path


def command(*args: str, cwd: Path | None = None) -> str:
    result = subprocess.run(
        args, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    )
    return result.stdout.strip()


def optional_command(*args: str, cwd: Path | None = None) -> str:
    try:
        return command(*args, cwd=cwd)
    except (OSError, subprocess.CalledProcessError) as error:
        return f"unavailable: {error}"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def hash_record(path: Path, root: Path) -> dict[str, str | int]:
    return {
        "path": str(path.relative_to(root) if path.is_relative_to(root) else path),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fiction-source",
        type=Path,
        default=Path("build/tools/external/fiction-v0.7.0"),
    )
    parser.add_argument(
        "--mntbench-source",
        type=Path,
        default=Path("build/tools/external/mnt-bench-v0.3.8"),
    )
    parser.add_argument(
        "--dataset-zip",
        type=Path,
        default=Path(
            "build/artifacts/external_baselines/mntbench_v0.3.8/"
            "walter2024_versatility_mntbench.zip"
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(
            "build/artifacts/external_baselines/fiction_v0.7.0/walter2024/provenance.json"
        ),
    )
    parser.add_argument(
        "--gold-output",
        type=Path,
        default=Path(
            "build/artifacts/external_baselines/fiction_v0.7.0/gold_subset/provenance.json"
        ),
    )
    parser.add_argument("--repetitions", type=int, default=10)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path.cwd().resolve()
    fiction = args.fiction_source.resolve()
    mntbench = args.mntbench_source.resolve()
    dataset_zip = args.dataset_zip.resolve()
    output = args.output.resolve()
    gold_output = args.gold_output.resolve()

    required = [
        fiction / "LICENSE.txt",
        fiction / "cmake/Dependencies.cmake",
        fiction / "vendors/CMakeLists.txt",
        fiction / "include/fiction/algorithms/physical_design/determine_clocking.hpp",
        fiction
        / "experiments/clock_number_assignment/clock_number_assignment_versatility.cpp",
        mntbench / "LICENSE",
        dataset_zip,
        root / "scripts/run_fiction_external_baselines.py",
        root / "scripts/record_fiction_baseline_provenance.py",
        root / "scripts/aggregate_fiction_clocking_baseline.py",
        root / "scripts/build_walter2024_reported_comparison.py",
        root / "scripts/build_external_baseline_scope.py",
        output.parent / "raw_results.csv",
        output.parent / "summary.csv",
        output.parent / "summary.json",
        output.parent / "reported_vs_reproduced.csv",
        output.parent.parent / "comparison_scope.csv",
        output.parent.parent / "comparison_scope.json",
    ]
    gold_required = [
        fiction
        / "include/fiction/algorithms/physical_design/graph_oriented_layout_design.hpp",
        root / "scripts/run_fiction_external_baselines.py",
        root / "scripts/record_fiction_baseline_provenance.py",
        root / "scripts/external_baselines/fiction_gold_subset.cpp",
        root / "scripts/aggregate_fiction_gold_baseline.py",
        output.parent.parent / "comparison_scope.csv",
        output.parent.parent / "comparison_scope.json",
        gold_output.parent / "summary.csv",
        gold_output.parent / "summary.json",
    ]
    required.extend(path for path in gold_required if path not in required)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit(f"missing provenance inputs: {missing}")

    source_status = optional_command("git", "status", "--short", cwd=fiction)
    source_diff_names = optional_command("git", "diff", "--name-only", cwd=fiction).splitlines()
    algorithm_path = "include/fiction/algorithms/physical_design/determine_clocking.hpp"
    experiment_path = (
        "experiments/clock_number_assignment/clock_number_assignment_versatility.cpp"
    )
    payload = {
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "scope": {
            "paper": "Walter, Drewniok, Wille, IEEE NANO 2024",
            "doi": "https://doi.org/10.1109/NANO61778.2024.10628908",
            "paper_pdf": (
                "https://www.cda.cit.tum.de/files/eda/"
                "2024_ieee_nano_clock_number_assignment.pdf"
            ),
            "algorithm_docs": (
                "https://fiction.readthedocs.io/en/v0.7.0/algorithms/"
                "determine_clocking.html"
            ),
            "classification": (
                "combinational MNT layout clock-number reassignment context; "
                "not a sequential cyclic P&R head-to-head"
            ),
            "reason_not_head_to_head": (
                "fiction FGL and determine_clocking encode local modulo clock numbers but "
                "no register boundary, iteration distance, absolute epoch, or initiation interval"
            ),
        },
        "fiction": {
            "repository": "https://github.com/cda-tum/fiction.git",
            "requested_release": "v0.7.0",
            "commit": command("git", "rev-parse", "HEAD", cwd=fiction),
            "describe": optional_command("git", "describe", "--tags", "--always", cwd=fiction),
            "license": "MIT",
            "license_file": hash_record(fiction / "LICENSE.txt", root),
            "source_status": source_status,
            "locally_modified_tracked_files": source_diff_names,
            "algorithm_source_modified": algorithm_path in source_diff_names,
            "official_experiment_source_modified": experiment_path in source_diff_names,
            "build_only_overlay": {
                "description": (
                    "FetchContent URLs were pinned and the parallel-hashmap include roots "
                    "were repaired; algorithm and experiment sources are unchanged"
                ),
                "dependency_pins": {
                    "nlohmann_json": "v3.12.0",
                    "pybind11": "v3.0.1",
                    "parallel_hashmap": "v2.0.0",
                    "tinyxml2": "11.0.0",
                    "alice": "6b7f941ca44f38226f5e2545224fa1194940cd73",
                    "mockturtle": "b856d3e0028d3578ed6739d2885c4931db8bb837",
                },
            },
        },
        "mntbench": {
            "repository": "https://github.com/cda-tum/mnt-bench.git",
            "requested_release": "v0.3.8",
            "commit": command("git", "rev-parse", "HEAD", cwd=mntbench),
            "describe": optional_command("git", "describe", "--tags", "--always", cwd=mntbench),
            "license": "MIT",
            "license_file": hash_record(mntbench / "LICENSE", root),
            "download_endpoint": "https://www.cda.cit.tum.de/mntbench/download",
            "downloaded_archive": hash_record(dataset_zip, root),
            "selection": {
                "benchmark_functions": 13,
                "benchmark_names": [
                    "mux21",
                    "xnor2",
                    "par_gen",
                    "par_check",
                    "t",
                    "t_5",
                    "majority_5_r1",
                    "newtag",
                    "clpl",
                    "xor5Maj",
                    "xor5_r1",
                    "cm82a_5",
                    "parity",
                ],
                "gate_library": "QCA ONE",
                "clocking_schemes": ["2DDWave", "USE", "RES"],
                "physical_design": "NanoPlaceR",
                "measured_layout_variant": "UnOpt_UnOrd_area",
                "measured_layouts": 39,
            },
        },
        "hashed_inputs": [hash_record(path, root) for path in required],
        "host": {
            "platform": platform.platform(),
            "machine": platform.machine(),
            "python": platform.python_version(),
            "logical_cpus": os.cpu_count(),
            "cpu": optional_command(
                "bash",
                "-lc",
                "lscpu | sed -n 's/^Model name:[[:space:]]*//p' | head -1",
            ),
            "memory": optional_command("bash", "-lc", "free -h | sed -n '2p'"),
            "compiler": optional_command("bash", "-lc", "c++ --version | head -1"),
            "cmake": optional_command("bash", "-lc", "cmake --version | head -1"),
            "git": optional_command("git", "--version"),
        },
        "execution": {
            "build_type": "Release",
            "sat_engine": "bill::solvers::bsat2 (fiction default)",
            "repetitions": args.repetitions,
            "official_target": "clock_number_assignment_versatility",
            "compile_command": (
                "cmake --build build/tools/external/fiction-v0.7.0-build "
                "--target clock_number_assignment_versatility -j2"
            ),
            "aggregate_command": "python3 scripts/aggregate_fiction_clocking_baseline.py",
            "reported_comparison_command": (
                "python3 scripts/build_walter2024_reported_comparison.py"
            ),
            "end_to_end_command": (
                "python3 scripts/run_fiction_external_baselines.py "
                f"--repetitions {args.repetitions} --jobs 2"
            ),
            "runtime_comparison_policy": (
                "no paper/rerun runtime ratios: hosts differ and Table I is rounded to 0.01 s"
            ),
        },
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(output)

    gold_payload = {
        "generated_at_utc": payload["generated_at_utc"],
        "scope": {
            "paper": "Hofmann, Walter, Wille, IEEE NANO 2024",
            "doi": "https://doi.org/10.1109/NANO61778.2024.10628808",
            "paper_pdf": (
                "https://www.cda.cit.tum.de/files/eda/2024_ieee_nano_a_star_is_born.pdf"
            ),
            "algorithm_docs": (
                "https://fiction.readthedocs.io/en/v0.7.0/algorithms/"
                "graph_oriented_layout_design.html"
            ),
            "classification": "shared combinational MNT P&R context; not sequential head-to-head",
            "reason_not_head_to_head": (
                "GOLD consumes combinational technology_network objects and emits "
                "2DDWave gate-level layouts without register/iteration semantics"
            ),
        },
        "fiction": payload["fiction"],
        "host": payload["host"],
        "license": {
            "name": "MIT",
            "file": payload["fiction"]["license_file"],
        },
        "execution": {
            "build_type": "Release",
            "wrapper_target": "fiction_gold_subset",
            "algorithm": "fiction::graph_oriented_layout_design",
            "effort_mode": "HIGH_EFFICIENCY",
            "cost_objective": "AREA",
            "timeout_ms_per_network": 60000,
            "repetitions": args.repetitions,
            "networks": ["mux21", "xor2", "xnor2", "par_gen"],
            "all_runs_formally_verified": True,
            "build_command": (
                "cmake --build build/tools/external/fiction-v0.7.0-build "
                "--target fiction_gold_subset -j2"
            ),
            "aggregate_command": "python3 scripts/aggregate_fiction_gold_baseline.py",
            "end_to_end_command": (
                "python3 scripts/run_fiction_external_baselines.py "
                f"--repetitions {args.repetitions} --jobs 2"
            ),
        },
        "hashed_inputs_and_results": [hash_record(path, root) for path in gold_required],
        "wrapper_note": (
            "The local wrapper only exposes upstream parameters and CSV output; "
            "the fiction GOLD header is unmodified"
        ),
    }
    gold_output.parent.mkdir(parents=True, exist_ok=True)
    gold_output.write_text(
        json.dumps(gold_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(gold_output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
