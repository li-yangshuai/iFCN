#!/usr/bin/env python3
"""Cross-check outputs against a frozen pre-optimization simon checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("circuits", nargs="+", type=Path)
    parser.add_argument("--legacy-repository", required=True, type=Path)
    parser.add_argument("--benchmark-executable", type=Path,
                        default=Path("build-release/ifcn_physical_benchmark"))
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--duration", type=float, default=4.096e-13)
    parser.add_argument("--time-step", type=float, default=1e-16)
    parser.add_argument("--logic-threshold", type=float, default=0.1)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_output(repository: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(repository), *arguments], text=True).strip()


def trace_values(path: Path) -> list[float]:
    lines = path.read_text(encoding="utf-8").splitlines()
    values: list[float] = []
    for index, line in enumerate(lines[:-1]):
        if line == "[TRACE_DATA]":
            values.extend(float(value) for value in lines[index + 1].split())
    return values


def compare_values(reference: Path, candidate: Path,
                   logic_threshold: float) -> dict[str, Any]:
    expected = trace_values(reference)
    actual = trace_values(candidate)
    if len(expected) != len(actual):
        return {"compatible": False, "values": min(len(expected), len(actual))}
    errors = [abs(left - right) for left, right in zip(expected, actual)]
    stable = [(left, right) for left, right in zip(expected, actual)
              if abs(left) > logic_threshold]
    sign_matches = sum((left > 0.0) == (right > 0.0)
                       for left, right in stable)
    return {
        "compatible": True,
        "values": len(expected),
        "stable_reference_values": len(stable),
        "sign_agreement": sign_matches / len(stable) if stable else 1.0,
        "mae": sum(errors) / len(errors) if errors else 0.0,
        "maximum_absolute_error": max(errors, default=0.0),
    }


def main() -> int:
    args = arguments()
    root = Path(__file__).resolve().parents[1]
    legacy = args.legacy_repository.expanduser().resolve()
    legacy_include = legacy / "include"
    if not (legacy_include / "simon" / "simon.hpp").is_file():
        raise FileNotFoundError("legacy simon headers are missing")
    output = args.output_directory.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    runner = output / "legacy_simon_reference_runner"
    source = root / "tests" / "LegacySimonReferenceRunner.cpp"
    subprocess.run([
        args.compiler, "-std=c++17", "-O2", "-DNDEBUG",
        f"-I{legacy_include}", str(source), "-o", str(runner),
    ], check=True)

    rows: list[dict[str, Any]] = []
    for circuit in args.circuits:
        circuit = circuit.expanduser().resolve()
        case = output / circuit.stem
        legacy_prefix = case / "legacy"
        current_prefix = case / "current"
        legacy_prefix.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([
            str(runner), str(circuit), str(legacy_prefix), str(args.samples),
            str(args.duration), str(args.time_step),
        ], check=True)
        subprocess.run([
            str(args.benchmark_executable.expanduser().resolve()), str(circuit),
            "--model", "both", "--repetitions", "1", "--warmup", "0",
            "--samples", str(args.samples), "--duration", str(args.duration),
            "--time-step", str(args.time_step),
            "--output-prefix", str(current_prefix), "--require-equivalent",
        ], check=True, stdout=subprocess.DEVNULL)
        for model in ("bistable", "coherence"):
            legacy_path = Path(f"{legacy_prefix}_{model}_legacy.rst")
            baseline_path = Path(f"{current_prefix}_{model}_baseline.rst")
            candidate_path = Path(f"{current_prefix}_{model}_accelerated.rst")
            legacy_hash = sha256(legacy_path)
            baseline_hash = sha256(baseline_path)
            candidate_hash = sha256(candidate_path)
            metrics = compare_values(
                legacy_path, candidate_path, args.logic_threshold)
            rows.append({
                "circuit": str(circuit),
                "model": model,
                "legacy_sha256": legacy_hash,
                "current_baseline_sha256": baseline_hash,
                "accelerated_sha256": candidate_hash,
                "legacy_equals_current_baseline": legacy_hash == baseline_hash,
                "legacy_equals_accelerated": legacy_hash == candidate_hash,
                **metrics,
            })

    summary = {
        "schema_version": 1,
        "scope_note": (
            "This is a frozen pre-optimization simon cross-version check, "
            "not an independent QCADesigner validation."),
        "legacy_repository": str(legacy),
        "legacy_commit": git_output(legacy, "rev-parse", "HEAD"),
        "legacy_remote": git_output(legacy, "remote", "get-url", "origin"),
        "compiler": subprocess.check_output(
            [args.compiler, "--version"], text=True).splitlines()[0],
        "samples": args.samples,
        "duration": args.duration,
        "time_step": args.time_step,
        "logic_threshold": args.logic_threshold,
        "all_exact": all(row["legacy_equals_accelerated"] for row in rows),
        "all_logic_signs_agree": all(
            row.get("sign_agreement") == 1.0 for row in rows),
        "rows": rows,
    }
    path = output / "legacy_crosscheck.json"
    path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"exact rows={sum(row['legacy_equals_accelerated'] for row in rows)}/"
          f"{len(rows)}")
    print(path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"legacy cross-check failed: {error}", file=sys.stderr)
        raise SystemExit(2)
