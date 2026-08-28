#!/usr/bin/env python3
"""Build, run, and independently validate sequential clock experiments.

The C++ executable emits raw solver observations.  This runner checks every
synthetic recurrence with a separate dynamic-programming oracle that neither
calls GlobalPhaseSolver nor reuses its closed-form feasibility test.  LIMIT is
kept as a first-class outcome and is never counted as UNSAT.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


STATUS_NAMES = {"SAT", "UNSAT", "LIMIT", "INVALID_INPUT"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir", type=Path, default=Path("build-release"),
        help="configured CMake build directory (default: build-release)",
    )
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path("build/artifacts/sequential_clock_comparison_v2"),
    )
    parser.add_argument("--repetitions", type=int, default=50)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--validate-only", action="store_true",
        help="validate raw CSVs already present in --output-dir",
    )
    args = parser.parse_args()
    if args.repetitions <= 0:
        parser.error("--repetitions must be positive")
    return args


def run_logged(command: list[str], cwd: Path, log_path: Path) -> None:
    completed = subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
    )
    log_path.write_text(completed.stdout, encoding="utf-8")
    if completed.returncode:
        raise RuntimeError(
            f"command failed with exit code {completed.returncode}: "
            f"{' '.join(command)}; see {log_path}"
        )


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def integer(row: dict[str, str], key: str) -> int:
    return int(row[key])


def recurrence_dp(
    route_edges: int,
    required_advances: int,
    max_same_phase_cells: int,
) -> bool:
    """Independent exact oracle over (advance-count, same-phase-run) states."""
    if route_edges < 0 or required_advances < 0:
        return False
    states: set[tuple[int, int]] = {(0, 1)}
    for _ in range(route_edges):
        following: set[tuple[int, int]] = set()
        for advances, run in states:
            if max_same_phase_cells <= 0 or run < max_same_phase_cells:
                following.add((advances, run + 1))
            if advances < required_advances:
                following.add((advances + 1, 1))
        states = following
    return any(advances == required_advances for advances, _ in states)


def modulo_only_dp(
    route_edges: int,
    phase_count: int,
    state_latency: int,
    max_same_phase_cells: int,
) -> bool:
    """Strong local baseline with run limits but without absolute epochs/II."""
    states: set[tuple[int, int]] = {(0, 1)}
    for _ in range(route_edges):
        following: set[tuple[int, int]] = set()
        for residue, run in states:
            if max_same_phase_cells <= 0 or run < max_same_phase_cells:
                following.add((residue, run + 1))
            following.add(((residue + 1) % phase_count, 1))
        states = following
    return any(
        (residue + state_latency) % phase_count == 0
        for residue, _ in states
    )


def expect(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def validate_recurrence(rows: list[dict[str, str]]) -> dict[str, object]:
    errors: list[str] = []
    counts: Counter[str] = Counter()
    strict: Counter[str] = Counter()
    for index, row in enumerate(rows, start=2):
        edges = integer(row, "route_edges")
        ii = integer(row, "ii")
        latency = integer(row, "state_latency")
        run_limit = integer(row, "max_same_phase_cells")
        exact = recurrence_dp(edges, ii - latency, run_limit)
        modulo = modulo_only_dp(edges, 4, latency, run_limit)
        status = row["solver_status"]
        expect(status in STATUS_NAMES, f"recurrence:{index}: bad status", errors)
        expect(
            status == ("SAT" if exact else "UNSAT"),
            f"recurrence:{index}: solver={status}, DP={exact}", errors,
        )
        expect(integer(row, "exact_oracle") == int(exact),
               f"recurrence:{index}: embedded exact oracle mismatch", errors)
        expect(integer(row, "modulo_only") == int(modulo),
               f"recurrence:{index}: embedded modulo oracle mismatch", errors)
        expect(integer(row, "false_accept") == int(modulo and not exact),
               f"recurrence:{index}: false_accept flag mismatch", errors)
        expect(integer(row, "false_reject") == int(exact and not modulo),
               f"recurrence:{index}: false_reject flag mismatch", errors)
        counts["cases"] += 1
        counts["exact_sat" if exact else "exact_unsat"] += 1
        counts["modulo_sat" if modulo else "modulo_unsat"] += 1
        counts["false_accept"] += int(modulo and not exact)
        counts["false_reject"] += int(exact and not modulo)
        counts[f"solver_{status.lower()}"] += 1
        if run_limit == 1:
            strict["cases"] += 1
            strict["exact_sat" if exact else "exact_unsat"] += 1
            strict["false_accept"] += int(modulo and not exact)
            strict["false_reject"] += int(exact and not modulo)
    if errors:
        raise AssertionError("\n".join(errors[:25]))
    result = dict(counts)
    result["false_accept_rate_among_exact_unsat"] = (
        counts["false_accept"] / counts["exact_unsat"]
        if counts["exact_unsat"] else 0.0
    )
    strict_result = dict(strict)
    strict_result["false_accept_rate_among_exact_unsat"] = (
        strict["false_accept"] / strict["exact_unsat"]
        if strict["exact_unsat"] else 0.0
    )
    result["strict_next_phase"] = strict_result
    result["mismatches"] = 0
    return result


def validate_ii_ablation(rows: list[dict[str, str]]) -> dict[str, object]:
    errors: list[str] = []
    counts: Counter[str] = Counter()
    for index, row in enumerate(rows, start=2):
        edges = integer(row, "route_edges")
        latency = integer(row, "state_latency")
        run_limit = integer(row, "max_same_phase_cells")
        candidates = sorted(int(value) for value in row["adaptive_candidates"].split("|"))
        fixed_ii = integer(row, "fixed_ii")
        feasible_iis = [
            ii for ii in candidates
            if recurrence_dp(edges, ii - latency, run_limit)
        ]
        adaptive = bool(feasible_iis)
        fixed = recurrence_dp(edges, fixed_ii - latency, run_limit)
        adaptive_status = row["adaptive_solver_status"]
        fixed_status = row["fixed_solver_status"]
        expect(adaptive_status == ("SAT" if adaptive else "UNSAT"),
               f"ii_ablation:{index}: adaptive status mismatch", errors)
        expect(fixed_status == ("SAT" if fixed else "UNSAT"),
               f"ii_ablation:{index}: fixed status mismatch", errors)
        expect(integer(row, "adaptive_oracle") == int(adaptive),
               f"ii_ablation:{index}: embedded adaptive oracle mismatch", errors)
        expect(integer(row, "fixed_oracle") == int(fixed),
               f"ii_ablation:{index}: embedded fixed oracle mismatch", errors)
        expected_ii = feasible_iis[0] if feasible_iis else 0
        expect(integer(row, "adaptive_ii") == expected_ii,
               f"ii_ablation:{index}: selected II is not smallest feasible", errors)
        expect(integer(row, "adaptive_validator") == 1,
               f"ii_ablation:{index}: adaptive internal validator failed", errors)
        expect(integer(row, "fixed_validator") == 1,
               f"ii_ablation:{index}: fixed internal validator failed", errors)
        counts["cases"] += 1
        counts["adaptive_sat"] += int(adaptive)
        counts["fixed_ii4_sat"] += int(fixed)
        counts["adaptive_only_sat"] += int(adaptive and not fixed)
        counts["fixed_only_sat"] += int(fixed and not adaptive)
        counts[f"adaptive_solver_{adaptive_status.lower()}"] += 1
        counts[f"fixed_solver_{fixed_status.lower()}"] += 1
    if errors:
        raise AssertionError("\n".join(errors[:25]))
    result: dict[str, object] = dict(counts)
    result["sat_recovery_over_fixed"] = (
        counts["adaptive_only_sat"] / counts["fixed_ii4_sat"]
        if counts["fixed_ii4_sat"] else 0.0
    )
    result["mismatches"] = 0
    return result


def percentile(values: Iterable[float], quantile: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    offset = quantile * (len(ordered) - 1)
    lower = int(offset)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = offset - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def validate_edge_scaling(rows: list[dict[str, str]]) -> dict[str, object]:
    errors: list[str] = []
    groups: dict[tuple[int, str], list[dict[str, str]]] = defaultdict(list)
    statuses: Counter[str] = Counter()
    required_edges = {8, 16, 32, 64, 96, 128, 160}
    for index, row in enumerate(rows, start=2):
        edges = integer(row, "route_edges")
        ii = integer(row, "ii")
        latency = integer(row, "state_latency")
        run_limit = integer(row, "max_same_phase_cells")
        exact = recurrence_dp(edges, ii - latency, run_limit)
        fixture = row["fixture"]
        actual = row["solver_status"]
        expected = row["expected_status"]
        if fixture == "budget_limit":
            independent_expected = "LIMIT"
            expect(exact, f"edge_scaling:{index}: LIMIT source is infeasible", errors)
            expect(integer(row, "dfs_budget") == 1,
                   f"edge_scaling:{index}: LIMIT budget is not one", errors)
        else:
            independent_expected = "SAT" if exact else "UNSAT"
        expect(actual == expected == independent_expected,
               f"edge_scaling:{index}: actual={actual}, declared={expected}, "
               f"DP={independent_expected}", errors)
        expect(integer(row, "exact_oracle") == int(exact),
               f"edge_scaling:{index}: embedded oracle mismatch", errors)
        expect(integer(row, "internal_validator") == 1,
               f"edge_scaling:{index}: internal validator failed", errors)
        groups[(edges, expected)].append(row)
        statuses[actual] += 1
    observed_edges = {edge for edge, _ in groups}
    expect(observed_edges == required_edges,
           f"edge scaling set mismatch: {sorted(observed_edges)}", errors)
    for edge in required_edges:
        expect({status for e, status in groups if e == edge} ==
               {"SAT", "UNSAT", "LIMIT"},
               f"edge {edge} lacks a SAT/UNSAT/LIMIT group", errors)
    if errors:
        raise AssertionError("\n".join(errors[:25]))

    summaries: list[dict[str, object]] = []
    for (edges, status), group in sorted(groups.items()):
        times = [float(row["time_us"]) for row in group]
        nodes = [float(row["dfs_nodes"]) for row in group]
        summaries.append({
            "route_edges": edges,
            "status": status,
            "runs": len(group),
            "time_us_p50": percentile(times, 0.50),
            "time_us_p95": percentile(times, 0.95),
            "dfs_nodes_p50": percentile(nodes, 0.50),
        })
    return {
        "rows": len(rows),
        "status_counts": dict(statuses),
        "groups": summaries,
        "mismatches": 0,
    }


def validate_auxiliary_outputs(
    scaling_rows: list[dict[str, str]],
    macro_rows: list[dict[str, str]],
) -> dict[str, object]:
    errors: list[str] = []
    scaling_statuses: Counter[str] = Counter()
    for index, row in enumerate(scaling_rows, start=2):
        actual = row["solver_status"]
        declared = row["expected_status"]
        expect(actual == declared,
               f"legacy_scaling:{index}: {actual} != {declared}", errors)
        expect(actual in {"SAT", "UNSAT"},
               f"legacy_scaling:{index}: unexpected {actual}", errors)
        scaling_statuses[actual] += 1
    for index, row in enumerate(macro_rows, start=2):
        expect(integer(row, "structural_valid") == 1,
               f"physical_macro:{index}: structural invalid", errors)
        expect(integer(row, "mapping_valid") == 1,
               f"physical_macro:{index}: mapping invalid", errors)
    if errors:
        raise AssertionError("\n".join(errors[:25]))
    return {
        "legacy_scaling_rows": len(scaling_rows),
        "legacy_scaling_status_counts": dict(scaling_statuses),
        "physical_macro_rows": len(macro_rows),
        "mismatches": 0,
    }


def cross_check_cpp_summary(
    raw: dict[str, object], recurrence: dict[str, object],
    ii_ablation: dict[str, object],
) -> None:
    checks = {
        ("correctness", "cases"): recurrence["cases"],
        ("correctness", "exact_sat"): recurrence["exact_sat"],
        ("correctness", "exact_unsat"): recurrence["exact_unsat"],
        ("correctness", "modulo_false_accept"): recurrence["false_accept"],
        ("correctness", "modulo_false_reject"): recurrence["false_reject"],
        ("ii_ablation", "cases"): ii_ablation["cases"],
        ("ii_ablation", "adaptive_sat"): ii_ablation["adaptive_sat"],
        ("ii_ablation", "fixed_ii4_sat"): ii_ablation["fixed_ii4_sat"],
        ("ii_ablation", "adaptive_only_sat"): ii_ablation["adaptive_only_sat"],
    }
    for (section, key), expected in checks.items():
        actual = raw[section][key]  # type: ignore[index]
        if actual != expected:
            raise AssertionError(
                f"summary cross-check failed: {section}.{key}="
                f"{actual}, independently computed={expected}"
            )


def write_comparison_csv(
    path: Path, recurrence: dict[str, object],
    ii_ablation: dict[str, object], edge_scaling: dict[str, object],
) -> None:
    rows = [
        ("exact_vs_modulo", "cases", recurrence["cases"], "cases"),
        ("exact_vs_modulo", "exact_sat", recurrence["exact_sat"], "cases"),
        ("exact_vs_modulo", "exact_unsat", recurrence["exact_unsat"], "cases"),
        ("exact_vs_modulo", "modulo_false_accept", recurrence["false_accept"], "cases"),
        ("exact_vs_modulo", "modulo_false_reject", recurrence["false_reject"], "cases"),
        ("exact_vs_modulo", "false_accept_rate_exact_unsat",
         recurrence["false_accept_rate_among_exact_unsat"], "ratio"),
        ("ii_ablation", "adaptive_sat", ii_ablation["adaptive_sat"], "cases"),
        ("ii_ablation", "fixed_ii4_sat", ii_ablation["fixed_ii4_sat"], "cases"),
        ("ii_ablation", "adaptive_only_sat", ii_ablation["adaptive_only_sat"], "cases"),
        ("ii_ablation", "sat_recovery_over_fixed",
         ii_ablation["sat_recovery_over_fixed"], "ratio"),
    ]
    for group in edge_scaling["groups"]:  # type: ignore[index]
        label = f"E{group['route_edges']}_{group['status']}"
        rows.extend([
            ("edge_scaling", f"{label}_time_us_p50", group["time_us_p50"], "us"),
            ("edge_scaling", f"{label}_time_us_p95", group["time_us_p95"], "us"),
            ("edge_scaling", f"{label}_dfs_nodes_p50", group["dfs_nodes_p50"], "nodes"),
        ])
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["experiment", "metric", "value", "unit"])
        writer.writerows(rows)


def latex_escape_status(status: str) -> str:
    return "LIMIT" if status == "LIMIT" else status


def write_latex(
    path: Path, recurrence: dict[str, object],
    ii_ablation: dict[str, object], edge_scaling: dict[str, object],
) -> None:
    strict = recurrence["strict_next_phase"]
    lines = [
        "% Generated by scripts/run_sequential_clock_experiments.py.",
        "% Every aggregate below was recomputed by the independent DP validator.",
        r"\begin{table}[t]", r"\centering",
        r"\caption{Exact global recurrence closure versus a strong modulo-only baseline.}",
        r"\begin{tabular}{lr}", r"\hline", r"Metric & Value \\", r"\hline",
        f"Cases & {recurrence['cases']} \\\\",
        f"Exact SAT / UNSAT & {recurrence['exact_sat']} / {recurrence['exact_unsat']} \\\\",
        f"Modulo false accepts & {recurrence['false_accept']} "
        f"({100.0 * float(recurrence['false_accept_rate_among_exact_unsat']):.2f}\\%) \\\\",
        f"Modulo false rejects & {recurrence['false_reject']} \\\\",
        f"Strict-family false accepts & {strict['false_accept']} "
        f"({100.0 * float(strict['false_accept_rate_among_exact_unsat']):.2f}\\%) \\\\",
        r"Independent-oracle mismatches & 0 \\",
        r"\hline", r"\end{tabular}", r"\end{table}", "",
        r"\begin{table}[t]", r"\centering",
        r"\caption{Initiation-interval selection ablation.}",
        r"\begin{tabular}{lr}", r"\hline", r"Metric & Value \\", r"\hline",
        f"Geometries & {ii_ablation['cases']} \\\\",
        f"Adaptive II SAT & {ii_ablation['adaptive_sat']} \\\\",
        f"Fixed II=4 SAT & {ii_ablation['fixed_ii4_sat']} \\\\",
        f"Adaptive-only recovery & {ii_ablation['adaptive_only_sat']} \\\\",
        f"Recovery over fixed II=4 & "
        f"{100.0 * float(ii_ablation['sat_recovery_over_fixed']):.2f}\\% \\\\",
        r"Independent-oracle mismatches & 0 \\",
        r"\hline", r"\end{tabular}", r"\end{table}", "",
        r"\begin{table}[t]", r"\centering",
        r"\caption{Bounded-reference solver scaling by total route edges.}",
        r"\begin{tabular}{lrrrr}", r"\hline",
        r"$E$ & Status & Runs & p50 ($\mu$s) & p95 ($\mu$s) \\", r"\hline",
    ]
    for group in edge_scaling["groups"]:  # type: ignore[index]
        lines.append(
            f"{group['route_edges']} & {latex_escape_status(str(group['status']))} & "
            f"{group['runs']} & {float(group['time_us_p50']):.2f} & "
            f"{float(group['time_us_p95']):.2f} \\\\"
        )
    lines.extend([r"\hline", r"\end{tabular}", r"\end{table}", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_value(root: Path, *arguments: str) -> str:
    completed = subprocess.run(
        ["git", *arguments], cwd=root, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def command_value(root: Path, command: list[str]) -> str:
    completed = subprocess.run(
        command, cwd=root, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
    )
    return completed.stdout.strip() if completed.returncode == 0 else "unknown"


def cpu_model() -> str:
    cpuinfo = Path("/proc/cpuinfo")
    if not cpuinfo.is_file():
        return platform.processor() or "unknown"
    for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.lower().startswith("model name") and ":" in line:
            return line.split(":", 1)[1].strip()
    return platform.processor() or "unknown"


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parents[1]
    build_dir = (root / args.build_dir).resolve()
    output_dir = (root / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    binary = build_dir / "ifcn_sequential_clock_experiment"
    build_command = [
        "cmake", "--build", str(build_dir), "--target",
        "ifcn_sequential_clock_experiment", "-j", str(args.jobs),
    ]
    run_command = [
        str(binary), str(output_dir), "--repetitions", str(args.repetitions),
    ]
    affinity = (
        sorted(os.sched_getaffinity(0))
        if hasattr(os, "sched_getaffinity") else None
    )

    if not args.validate_only:
        if not args.skip_build:
            run_logged(build_command, root, output_dir / "build.log")
        if not binary.is_file():
            raise FileNotFoundError(f"experiment binary not found: {binary}")
        run_logged(run_command, root, output_dir / "experiment.log")
    elif not binary.is_file():
        binary = Path("unavailable-in-validate-only-mode")

    raw_summary = json.loads((output_dir / "summary.json").read_text(encoding="utf-8"))
    recurrence = validate_recurrence(read_csv(output_dir / "recurrence_sweep.csv"))
    ii_ablation = validate_ii_ablation(read_csv(output_dir / "ii_ablation.csv"))
    edge_scaling = validate_edge_scaling(read_csv(output_dir / "edge_scaling_runs.csv"))
    auxiliary = validate_auxiliary_outputs(
        read_csv(output_dir / "scaling_runs.csv"),
        read_csv(output_dir / "physical_macro_runs.csv"),
    )
    cross_check_cpp_summary(raw_summary, recurrence, ii_ablation)

    write_comparison_csv(
        output_dir / "comparison_summary.csv",
        recurrence, ii_ablation, edge_scaling,
    )
    write_latex(
        output_dir / "validated_tables.tex",
        recurrence, ii_ablation, edge_scaling,
    )
    manifest = {
        "schema": "ifcn.sequential_clock_experiment.manifest.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "repository": str(root),
        "git_commit": git_value(root, "rev-parse", "HEAD"),
        "git_dirty": bool(git_value(root, "status", "--porcelain")),
        "platform": platform.platform(),
        "cpu_model": cpu_model(),
        "logical_cpu_count": os.cpu_count(),
        "cpu_affinity": affinity,
        "python": sys.version.split()[0],
        "cmake": command_value(root, ["cmake", "--version"]).splitlines()[0],
        "cxx": command_value(root, ["c++", "--version"]).splitlines()[0],
        "build_type": "Release" if "release" in build_dir.name.lower() else "unknown",
        "build_command": build_command,
        "run_command": run_command,
        "runner_command": [sys.executable, str(Path(__file__).resolve()), *sys.argv[1:]],
        "affinity_launcher": (
            ["taskset", "-c", ",".join(str(cpu) for cpu in affinity)]
            if affinity is not None else None
        ),
        "binary": str(binary),
        "binary_sha256": sha256(binary) if binary.is_file() else None,
        "source_sha256": {
            "solver": sha256(root / "include/autopr/sequential/globalPhaseSolver.cpp"),
            "experiment": sha256(root / "src/app/ifcn_sequential_clock_experiment.cpp"),
            "runner_validator": sha256(Path(__file__).resolve()),
        },
        "repetitions": args.repetitions,
        "timing_method": "steady_clock per in-process solve; sequential repetitions; p50/p95 reported",
        "threading": "single-threaded solver",
    }
    (output_dir / "run_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    validated = {
        "schema": "ifcn.sequential_clock_experiment.validated.v1",
        "validation": {
            "passed": True,
            "oracle": "independent dynamic programming over route advances and same-phase runs",
            "solver_library_linked": False,
            "limit_is_unsat": False,
        },
        "exact_vs_modulo": recurrence,
        "ii_ablation": ii_ablation,
        "edge_scaling": edge_scaling,
        "auxiliary": auxiliary,
        "raw_cpp_summary": raw_summary,
        "manifest": manifest,
    }
    (output_dir / "validated_summary.json").write_text(
        json.dumps(validated, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(
        "sequential_clock_validation=PASS "
        f"recurrence_cases={recurrence['cases']} "
        f"false_accepts={recurrence['false_accept']} "
        f"adaptive_only_sat={ii_ablation['adaptive_only_sat']} "
        f"edge_scaling_rows={edge_scaling['rows']} "
        f"output={output_dir}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, FileNotFoundError, RuntimeError) as error:
        print(f"sequential clock experiment failed: {error}", file=sys.stderr)
        raise SystemExit(1)
