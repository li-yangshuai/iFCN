#!/usr/bin/env python3
"""Batch benchmark strict accelerated Bistable and Coherence simulators.

The C++ runner performs paired measurements and rejects comparisons whose
input/clock traces differ.  This wrapper discovers QCA files, invokes that
runner without a shell, preserves every per-circuit JSON report, and computes
pooled accuracy plus circuit-level timing statistics suitable for a paper.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
from typing import Any, Callable, Iterable, Sequence


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path,
                        help="QCA files or directories searched recursively")
    parser.add_argument("--benchmark-executable", type=Path,
                        default=Path("build/ifcn_physical_benchmark"))
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--model", choices=("bistable", "coherence", "both"),
                        default="both")
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--logic-threshold", type=float, default=0.1)
    parser.add_argument("--equivalence-tolerance", type=float, default=0.0)
    parser.add_argument("--require-equivalent", action="store_true")
    parser.add_argument("--include-energy-input", action="store_true",
                        help="include derived *_energy_input.qca files")
    parser.add_argument("--max-circuits", type=int)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--vector-manifest", type=Path,
                        help="JSON object mapping absolute/relative QCA paths or basenames to VT files")
    parser.add_argument("--bootstrap-resamples", type=int, default=5000)
    parser.add_argument("--bootstrap-seed", type=int, default=20260712)

    # The wrapper deliberately exposes the same physical parameters as the
    # paired runner.  Values left as None retain the C++ baseline defaults.
    parser.add_argument("--samples", type=int)
    parser.add_argument("--max-iterations", type=int)
    parser.add_argument("--convergence-tolerance", type=float)
    parser.add_argument("--seed", type=int)
    parser.add_argument("--temperature", type=float)
    parser.add_argument("--relaxation", type=float)
    parser.add_argument("--time-step", type=float)
    parser.add_argument("--duration", type=float)
    parser.add_argument("--steady-state-tolerance", type=float)
    parser.add_argument("--max-steady-state-iterations", type=int)
    parser.add_argument("--numeric-method", choices=("euler", "rk4"))
    parser.add_argument("--epsilon-r", type=float)
    parser.add_argument("--layer-separation", type=float)
    parser.add_argument("--radius-effect", type=float)
    parser.add_argument("--amplitude", type=float)
    parser.add_argument("--clock-high", type=float)
    parser.add_argument("--clock-low", type=float)
    parser.add_argument("--clock-shift", type=float)
    parser.add_argument("--jitters")
    args = parser.parse_args()

    if args.repetitions <= 0:
        parser.error("--repetitions must be positive")
    if args.warmup < 0:
        parser.error("--warmup must be non-negative")
    if args.bootstrap_resamples < 0:
        parser.error("--bootstrap-resamples must be non-negative")
    if args.max_circuits is not None and args.max_circuits <= 0:
        parser.error("--max-circuits must be positive")
    return args


def discover_qca(inputs: Iterable[Path], include_energy_input: bool) -> list[Path]:
    discovered: dict[str, Path] = {}
    for source in inputs:
        source = source.expanduser()
        if source.is_file():
            candidates = [source]
        elif source.is_dir():
            candidates = source.rglob("*.qca")
        else:
            raise FileNotFoundError(f"benchmark input does not exist: {source}")
        for candidate in candidates:
            if candidate.suffix.lower() != ".qca":
                continue
            if not include_energy_input and candidate.name.lower().endswith(
                    "_energy_input.qca"):
                continue
            resolved = candidate.resolve()
            discovered[str(resolved)] = resolved
    return [discovered[key] for key in sorted(discovered)]


def load_vector_manifest(path: Path | None) -> tuple[dict[str, Path], Path | None]:
    if path is None:
        return {}, None
    manifest_path = path.resolve()
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("vector manifest must be a JSON object")
    manifest: dict[str, Path] = {}
    for key, raw_value in data.items():
        if not isinstance(key, str) or not isinstance(raw_value, str):
            raise ValueError("vector manifest keys and values must be strings")
        value = Path(raw_value).expanduser()
        if not value.is_absolute():
            value = manifest_path.parent / value
        manifest[key] = value.resolve()
    return manifest, manifest_path


def vector_for_circuit(circuit: Path, manifest: dict[str, Path]) -> Path | None:
    if not manifest:
        return None
    candidates = (str(circuit), circuit.as_posix(), circuit.name, circuit.stem)
    for candidate in candidates:
        if candidate in manifest:
            vector = manifest[candidate]
            if not vector.is_file():
                raise FileNotFoundError(f"vector table does not exist: {vector}")
            return vector
    return None


def append_optional_arguments(command: list[str], args: argparse.Namespace) -> None:
    mappings = (
        ("samples", "--samples"),
        ("max_iterations", "--max-iterations"),
        ("convergence_tolerance", "--convergence-tolerance"),
        ("seed", "--seed"),
        ("temperature", "--temperature"),
        ("relaxation", "--relaxation"),
        ("time_step", "--time-step"),
        ("duration", "--duration"),
        ("steady_state_tolerance", "--steady-state-tolerance"),
        ("max_steady_state_iterations", "--max-steady-state-iterations"),
        ("numeric_method", "--numeric-method"),
        ("epsilon_r", "--epsilon-r"),
        ("layer_separation", "--layer-separation"),
        ("radius_effect", "--radius-effect"),
        ("amplitude", "--amplitude"),
        ("clock_high", "--clock-high"),
        ("clock_low", "--clock-low"),
        ("clock_shift", "--clock-shift"),
        ("jitters", "--jitters"),
    )
    for attribute, option in mappings:
        value = getattr(args, attribute)
        if value is not None:
            command.extend((option, str(value)))


def safe_report_stem(circuit: Path) -> str:
    digest = hashlib.sha256(str(circuit).encode("utf-8")).hexdigest()[:10]
    clean = "".join(character if character.isalnum() or character in "-_"
                    else "_" for character in circuit.stem)
    return f"{clean}_{digest}"


def percentile(sorted_values: Sequence[float], probability: float) -> float:
    if not sorted_values:
        return math.nan
    position = probability * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    fraction = position - lower
    return sorted_values[lower] * (1.0 - fraction) + sorted_values[upper] * fraction


def bootstrap_interval(values: Sequence[float], statistic: Callable[[Sequence[float]], float],
                       resamples: int, rng: random.Random) -> list[float] | None:
    if not values or resamples == 0:
        return None
    sampled_statistics = []
    for _ in range(resamples):
        sample = [values[rng.randrange(len(values))] for _ in values]
        sampled_statistics.append(statistic(sample))
    sampled_statistics.sort()
    return [percentile(sampled_statistics, 0.025),
            percentile(sampled_statistics, 0.975)]


def geometric_mean(values: Sequence[float]) -> float:
    positive = [value for value in values if value > 0.0 and math.isfinite(value)]
    if len(positive) != len(values) or not positive:
        return math.nan
    return math.exp(sum(math.log(value) for value in positive) / len(positive))


def aggregate_model(rows: Sequence[dict[str, Any]], resamples: int,
                    rng: random.Random) -> dict[str, Any]:
    stable_samples = sum(int(row["stable_reference_samples"]) for row in rows)
    output_samples = sum(int(row["output_samples"]) for row in rows)
    sign_matches = sum(int(row["sign_matches"]) for row in rows)
    confident_matches = sum(int(row["confident_matches"]) for row in rows)
    absolute_error_sum = sum(float(row["mae"]) * int(row["output_samples"])
                             for row in rows)
    squared_error_sum = sum(float(row["rmse"]) ** 2 * int(row["output_samples"])
                            for row in rows)
    speedups = [float(row["speedup"]) for row in rows]
    sign_agreements = [float(row["sign_agreement"]) for row in rows]
    confident_agreements = [float(row["confident_logic_agreement"]) for row in rows]
    maes = [float(row["mae"]) for row in rows]
    reference_time = sum(float(row["reference_median_seconds"]) for row in rows)
    candidate_time = sum(float(row["candidate_median_seconds"]) for row in rows)

    result: dict[str, Any] = {
        "circuits": len(rows),
        "all_comparable": all(bool(row["comparable"]) for row in rows),
        "output_samples": output_samples,
        "stable_reference_samples": stable_samples,
        "pooled_sign_agreement": sign_matches / stable_samples if stable_samples else 1.0,
        "pooled_confident_logic_agreement": (
            confident_matches / stable_samples if stable_samples else 1.0),
        "pooled_mae": absolute_error_sum / output_samples if output_samples else 0.0,
        "pooled_rmse": math.sqrt(squared_error_sum / output_samples)
                       if output_samples else 0.0,
        "maximum_absolute_error": max((float(row["max_absolute_error"])
                                        for row in rows), default=0.0),
        "macro_sign_agreement": statistics.fmean(sign_agreements) if rows else math.nan,
        "macro_confident_logic_agreement": (
            statistics.fmean(confident_agreements) if rows else math.nan),
        "macro_mae": statistics.fmean(maes) if rows else math.nan,
        "reference_sum_median_seconds": reference_time,
        "candidate_sum_median_seconds": candidate_time,
        "suite_speedup": reference_time / candidate_time if candidate_time > 0.0 else math.nan,
        "geometric_mean_speedup": geometric_mean(speedups),
        "median_speedup": statistics.median(speedups) if rows else math.nan,
    }
    result["bootstrap_95_percent_ci"] = {
        "geometric_mean_speedup": bootstrap_interval(
            speedups, geometric_mean, resamples, rng),
        "macro_sign_agreement": bootstrap_interval(
            sign_agreements, statistics.fmean, resamples, rng),
        "macro_confident_logic_agreement": bootstrap_interval(
            confident_agreements, statistics.fmean, resamples, rng),
        "macro_mae": bootstrap_interval(maes, statistics.fmean, resamples, rng),
    }
    return result


def machine_metadata(executable: Path) -> dict[str, Any]:
    cpu_model = None
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lower().startswith("model name") and ":" in line:
                cpu_model = line.split(":", 1)[1].strip()
                break
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "cpu_model": cpu_model,
        "logical_cpu_count": os.cpu_count(),
        "python": platform.python_version(),
        "benchmark_executable": str(executable.resolve()),
    }


def flatten_comparison(circuit: Path, report: dict[str, Any],
                       comparison: dict[str, Any]) -> dict[str, Any]:
    accuracy = comparison["accuracy"]
    return {
        "circuit": str(circuit),
        "model": comparison["model"],
        "simulation_mode": report["simulation_mode"],
        "vector_table": report["vector_table"],
        "repetitions": report["repetitions"],
        "reference_median_seconds": comparison["reference_timing"]["median_seconds"],
        "candidate_median_seconds": comparison["candidate_timing"]["median_seconds"],
        "speedup": comparison["speedup"],
        "comparable": accuracy["comparable"],
        "incompatibility": accuracy["incompatibility"],
        "output_samples": accuracy["output_samples"],
        "stable_reference_samples": accuracy["stable_reference_samples"],
        "sign_matches": accuracy["sign_matches"],
        "confident_matches": accuracy["confident_matches"],
        "sign_agreement": accuracy["sign_agreement"],
        "confident_logic_agreement": accuracy["confident_logic_agreement"],
        "weak_candidate_samples": accuracy["weak_candidate_samples"],
        "mae": accuracy["mae"],
        "rmse": accuracy["rmse"],
        "max_absolute_error": accuracy["max_absolute_error"],
    }


def write_rows(path: Path, rows: Sequence[dict[str, Any]]) -> None:
    fieldnames = list(rows[0]) if rows else ["circuit", "model"]
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_arguments()
    executable = args.benchmark_executable.expanduser().resolve()
    if not executable.is_file():
        raise FileNotFoundError(f"benchmark executable does not exist: {executable}")
    circuits = discover_qca(args.inputs, args.include_energy_input)
    if args.max_circuits is not None:
        circuits = circuits[:args.max_circuits]
    if not circuits:
        raise RuntimeError("no QCA files discovered")

    vector_manifest, vector_manifest_path = load_vector_manifest(args.vector_manifest)
    output_directory = args.output_directory.expanduser().resolve()
    raw_directory = output_directory / "raw"
    raw_directory.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, Any]] = []
    failures: list[dict[str, Any]] = []
    for index, circuit in enumerate(circuits, start=1):
        report_path = raw_directory / f"{safe_report_stem(circuit)}.json"
        command = [
            str(executable), str(circuit),
            "--model", args.model,
            "--repetitions", str(args.repetitions),
            "--warmup", str(args.warmup),
            "--logic-threshold", str(args.logic_threshold),
            "--equivalence-tolerance", str(args.equivalence_tolerance),
            "--json", str(report_path),
        ]
        vector = vector_for_circuit(circuit, vector_manifest)
        if vector is not None:
            command.extend(("--vectors", str(vector)))
        if args.require_equivalent:
            command.append("--require-equivalent")
        append_optional_arguments(command, args)

        print(f"[{index}/{len(circuits)}] {circuit}", flush=True)
        completed = subprocess.run(command, text=True, capture_output=True, check=False)
        if completed.returncode != 0:
            failure = {
                "circuit": str(circuit),
                "returncode": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
                "command": command,
            }
            failures.append(failure)
            print(f"  failed (exit {completed.returncode}): "
                  f"{completed.stderr.strip() or completed.stdout.strip()}",
                  file=sys.stderr, flush=True)
            if args.fail_fast:
                break
            continue

        report = json.loads(report_path.read_text(encoding="utf-8"))
        for comparison in report["comparisons"]:
            rows.append(flatten_comparison(circuit, report, comparison))

    rows_path = output_directory / "results.csv"
    write_rows(rows_path, rows)
    rng = random.Random(args.bootstrap_seed)
    model_summaries = {
        model: aggregate_model([row for row in rows if row["model"] == model],
                               args.bootstrap_resamples, rng)
        for model in ("bistable", "coherence")
        if any(row["model"] == model for row in rows)
    }
    summary = {
        "schema_version": 1,
        "requested_circuits": len(circuits),
        "successful_circuits": len({row["circuit"] for row in rows}),
        "failed_circuits": len(failures),
        "model": args.model,
        "vector_manifest": str(vector_manifest_path) if vector_manifest_path else None,
        "machine": machine_metadata(executable),
        "models": model_summaries,
        "failures": failures,
    }
    summary_path = output_directory / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
                            encoding="utf-8")

    print(f"rows={rows_path}")
    print(f"summary={summary_path}")
    for model, values in model_summaries.items():
        print(f"{model}: circuits={values['circuits']} "
              f"logic={values['pooled_confident_logic_agreement']:.6f} "
              f"MAE={values['pooled_mae']:.6g} "
              f"RMSE={values['pooled_rmse']:.6g} "
              f"max={values['maximum_absolute_error']:.6g} "
              f"suite_speedup={values['suite_speedup']:.3f}x")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"physical benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(2)
