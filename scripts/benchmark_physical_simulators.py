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
    parser.add_argument("--verify-internal-state", action="store_true",
                        help="run an untimed full internal-state certificate pass")
    parser.add_argument("--graph-mode",
                        choices=("spatial", "all-pairs", "reuse"),
                        default="spatial",
                        help="interaction-graph ablation/reuse mode")
    parser.add_argument("--disable-clock-cache", action="store_true")
    parser.add_argument("--disable-input-cache", action="store_true")
    parser.add_argument("--disable-fusion", action="store_true")
    parser.add_argument("--include-energy-input", action="store_true",
                        help="include derived *_energy_input.qca files")
    parser.add_argument("--max-circuits", type=int)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--vector-manifest", type=Path,
                        help="JSON object mapping absolute/relative QCA paths or basenames to VT files")
    parser.add_argument("--bootstrap-resamples", type=int, default=5000)
    parser.add_argument("--bootstrap-seed", type=int, default=20260712)
    parser.add_argument("--cpu-affinity",
                        help="Linux taskset CPU list, e.g. 0 or 2-3")

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
    parser.add_argument("--cache-budget-bytes", type=int)
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
        ("cache_budget_bytes", "--cache-budget-bytes"),
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


def suite_speedup(rows: Sequence[dict[str, Any]]) -> float:
    reference = sum(float(row["reference_median_seconds"]) for row in rows)
    candidate = sum(float(row["candidate_median_seconds"]) for row in rows)
    return reference / candidate if candidate > 0.0 else math.nan


def bootstrap_row_interval(rows: Sequence[dict[str, Any]],
                           statistic: Callable[[Sequence[dict[str, Any]]], float],
                           resamples: int,
                           rng: random.Random) -> list[float] | None:
    if not rows or resamples == 0:
        return None
    sampled = []
    for _ in range(resamples):
        sample = [rows[rng.randrange(len(rows))] for _ in rows]
        sampled.append(statistic(sample))
    sampled.sort()
    return [percentile(sampled, 0.025), percentile(sampled, 0.975)]


def two_sided_sign_test(wins: int, losses: int) -> float:
    trials = wins + losses
    if trials == 0:
        return 1.0
    tail = min(wins, losses)
    probability = sum(math.comb(trials, value) for value in range(tail + 1))
    return min(1.0, 2.0 * probability / (2 ** trials))


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
    sorted_speedups = sorted(speedups)
    wins = sum(value > 1.0 for value in speedups)
    losses = sum(value < 1.0 for value in speedups)
    ties = len(speedups) - wins - losses
    slower_circuits = [
        {"circuit": row["circuit"], "speedup": float(row["speedup"])}
        for row in sorted(rows, key=lambda item: float(item["speedup"]))
        if float(row["speedup"]) < 1.0
    ]

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
        "speedup_q1": percentile(sorted_speedups, 0.25),
        "speedup_q3": percentile(sorted_speedups, 0.75),
        "minimum_speedup": min(speedups, default=math.nan),
        "maximum_speedup": max(speedups, default=math.nan),
        "circuits_faster_than_baseline": wins,
        "circuits_slower_than_baseline": losses,
        "circuits_tied_with_baseline": ties,
        "two_sided_sign_test_p_value": two_sided_sign_test(wins, losses),
        "slower_circuit_details": slower_circuits,
        "maximum_peak_process_rss_kib": max(
            (int(row["peak_process_rss_kib"]) for row in rows), default=0),
        "minimum_total_cells": min(
            (int(row["total_cells"]) for row in rows), default=0),
        "maximum_total_cells": max(
            (int(row["total_cells"]) for row in rows), default=0),
        "minimum_directed_couplings": min(
            (int(row["directed_couplings"]) for row in rows), default=0),
        "maximum_directed_couplings": max(
            (int(row["directed_couplings"]) for row in rows), default=0),
        "all_internal_state_checked": all(
            bool(row["internal_state_checked"]) for row in rows),
        "all_internal_state_equivalent": all(
            (not bool(row["internal_state_checked"])) or
            bool(row["internal_state_equivalent"]) for row in rows),
        "internal_state_frames": sum(
            int(row["internal_state_frames"]) for row in rows),
        "internal_state_values": sum(
            int(row["internal_state_values"]) for row in rows),
    }
    result["bootstrap_95_percent_ci"] = {
        "suite_speedup": bootstrap_row_interval(
            rows, suite_speedup, resamples, rng),
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
    affinity = None
    if hasattr(os, "sched_getaffinity"):
        affinity = sorted(os.sched_getaffinity(0))
    governor_path = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    governor = (governor_path.read_text(encoding="utf-8").strip()
                if governor_path.is_file() else None)
    return {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "cpu_model": cpu_model,
        "logical_cpu_count": os.cpu_count(),
        "process_cpu_affinity": affinity,
        "scaling_governor": governor,
        "python": platform.python_version(),
        "benchmark_executable": str(executable.resolve()),
    }


def flatten_comparison(circuit: Path, report: dict[str, Any],
                       comparison: dict[str, Any]) -> dict[str, Any]:
    accuracy = comparison["accuracy"]
    certificate = comparison.get("internal_state_certificate", {})
    reference_phases = comparison.get("reference_phase_timings", {})
    candidate_phases = comparison.get("candidate_phase_timings", {})
    components = comparison.get("component_seconds", {})
    component_timings = comparison.get("candidate_component_timings", {})
    work = comparison.get("work", {})
    options = comparison.get("options", {})
    graph_candidates = work.get(
        "graph_candidate_checks", work.get("spatial_candidates", 0))
    all_pair_checks = work.get("graph_all_pair_checks", 0)

    def phase_median(phases: dict[str, Any], name: str) -> float:
        return float(phases.get(name, {}).get("median_seconds", math.nan))

    def component_median(name: str) -> float:
        if name in component_timings:
            return float(component_timings[name]["median_seconds"])
        return float(components.get(name, math.nan))

    return {
        "circuit": str(circuit),
        "model": comparison["model"],
        "simulation_mode": report["simulation_mode"],
        "vector_table": report["vector_table"],
        "repetitions": report["repetitions"],
        "epsilon_r": options.get("epsilon_r", math.nan),
        "numeric_method": comparison.get("numeric_method", ""),
        "peak_process_rss_kib": report.get("peak_process_rss_kib", 0),
        "total_cells": work.get("total_cells", 0),
        "dynamic_cells": work.get("dynamic_cells", 0),
        "directed_couplings": work.get("directed_couplings", 0),
        "graph_candidate_checks": graph_candidates,
        "graph_all_pair_checks": all_pair_checks,
        "graph_candidate_fraction": (
            float(graph_candidates) / float(all_pair_checks)
            if all_pair_checks else 0.0),
        "clock_cache_used": work.get("clock_cache_used", False),
        "input_cache_used": work.get("input_cache_used", False),
        "fused_integration_used": work.get("fused_integration_used", False),
        "reference_median_seconds": comparison["reference_timing"]["median_seconds"],
        "candidate_median_seconds": comparison["candidate_timing"]["median_seconds"],
        "speedup": comparison["speedup"],
        "graph_mode": comparison.get("graph_mode"),
        "graph_precompile_seconds": comparison.get(
            "graph_precompile_seconds", 0.0),
        "reference_design_initialization_median_seconds": phase_median(
            reference_phases, "design_initialization"),
        "candidate_design_initialization_median_seconds": phase_median(
            candidate_phases, "design_initialization"),
        "reference_iterations_median_seconds": phase_median(
            reference_phases, "iterations"),
        "candidate_iterations_median_seconds": phase_median(
            candidate_phases, "iterations"),
        "interaction_graph_seconds": component_median("interaction_graph"),
        "energy_materialization_seconds": component_median(
            "energy_materialization"),
        "kernel_compilation_seconds": component_median("kernel_compilation"),
        "generator_cache_seconds": component_median("generator_cache"),
        "internal_state_checked": certificate.get("checked", False),
        "internal_state_equivalent": certificate.get("equivalent", False),
        "internal_state_frames": certificate.get("frames", 0),
        "internal_state_values": certificate.get("values", 0),
        "reference_state_digest": certificate.get("reference_digest", ""),
        "candidate_state_digest": certificate.get("candidate_digest", ""),
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
    benchmark_protocol: dict[str, Any] | None = None
    for index, circuit in enumerate(circuits, start=1):
        report_path = raw_directory / f"{safe_report_stem(circuit)}.json"
        command = [
            str(executable), str(circuit),
            "--model", args.model,
            "--repetitions", str(args.repetitions),
            "--warmup", str(args.warmup),
            "--logic-threshold", str(args.logic_threshold),
            "--equivalence-tolerance", str(args.equivalence_tolerance),
            "--graph-mode", args.graph_mode,
            "--json", str(report_path),
        ]
        vector = vector_for_circuit(circuit, vector_manifest)
        if vector is not None:
            command.extend(("--vectors", str(vector)))
        if args.require_equivalent:
            command.append("--require-equivalent")
        if args.verify_internal_state:
            command.append("--verify-internal-state")
        if args.disable_clock_cache:
            command.append("--disable-clock-cache")
        if args.disable_input_cache:
            command.append("--disable-input-cache")
        if args.disable_fusion:
            command.append("--disable-fusion")
        append_optional_arguments(command, args)

        print(f"[{index}/{len(circuits)}] {circuit}", flush=True)
        if args.cpu_affinity:
            command = ["taskset", "--cpu-list", args.cpu_affinity, *command]
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
        benchmark_protocol = report.get("benchmark_protocol", benchmark_protocol)
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
        "schema_version": 3,
        "requested_circuits": len(circuits),
        "successful_circuits": len({row["circuit"] for row in rows}),
        "failed_circuits": len(failures),
        "model": args.model,
        "graph_mode": args.graph_mode,
        "verify_internal_state": args.verify_internal_state,
        "requested_cpu_affinity": args.cpu_affinity,
        "benchmark_protocol": benchmark_protocol,
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
