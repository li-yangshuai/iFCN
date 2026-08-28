#!/usr/bin/env python3
"""Audit mapped cyclic sequential layouts with Simon and EnergyAnalysis.

This runner deliberately keeps three claims separate:

* structural mapping/DRC evidence from the IFCN layout,
* baseline-versus-accelerated Simon implementation equivalence,
* diagnostic feedback-probe agreement with the sampled RTL recurrence.

The feedback probe reclassifies an existing dynamic gate output-port cell as
an OUTPUT (or retains the center of an existing output pseudo node).  It adds
no QCA cell and changes no coordinate, phase, or coupling.
Because the physical register/state device is not characterized, probe results
and energy numbers are exploratory and never promoted to physical signoff.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import platform
import re
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any, Iterable


ELECTRON_VOLT_J = 1.602176634e-19


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-directory",
        type=Path,
        default=Path("build/artifacts/sequential_paper_cyclic_benchmarks_v1"),
    )
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(
        "--fixture-directory",
        type=Path,
        default=Path("tests/benchmarks_f/SEQUENTIAL/papers"),
    )
    parser.add_argument(
        "--energy-executable", type=Path, default=Path("build/ifcn_energy_analysis")
    )
    parser.add_argument(
        "--mapping-executable", type=Path, default=Path("build/ifcn_mapping_metrics")
    )
    parser.add_argument(
        "--physical-benchmark-executable",
        type=Path,
        default=Path("build/ifcn_physical_benchmark"),
    )
    parser.add_argument("--sim-model", choices=("none", "bistable", "coherence", "both"),
                        default="bistable")
    parser.add_argument("--sim-samples", type=int, default=8192)
    parser.add_argument("--sim-repetitions", type=int, default=1)
    parser.add_argument("--sim-warmup", type=int, default=0)
    parser.add_argument("--sim-time-step", type=float, default=1.0e-16)
    parser.add_argument("--sim-numeric-method", choices=("euler", "rk4"), default="rk4")
    parser.add_argument("--functional-warmup-cycles", type=int, default=2)
    parser.add_argument("--logic-threshold", type=float, default=0.1)
    parser.add_argument("--skip-energy", action="store_true")
    parser.add_argument(
        "--energy-time-steps",
        default="6.25e-17",
        help="comma-separated integration steps; multiple values form a convergence sweep",
    )
    parser.add_argument("--clock-period", type=float, default=1.0e-11)
    parser.add_argument("--clock-slope", type=float, default=1.0e-12)
    parser.add_argument("--max-cases", type=int)
    parser.add_argument("--timeout-seconds", type=float, default=900.0)
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()

    if args.sim_samples < 2 or args.sim_repetitions < 1 or args.sim_warmup < 0:
        parser.error("invalid simulator sample/repetition/warmup count")
    if args.functional_warmup_cycles < 0:
        parser.error("--functional-warmup-cycles must be non-negative")
    if args.clock_period <= 0.0 or args.clock_slope <= 0.0:
        parser.error("clock timing values must be positive")
    try:
        args.energy_time_steps = [float(value) for value in args.energy_time_steps.split(",")]
    except ValueError as error:
        parser.error(f"invalid --energy-time-steps: {error}")
    if not args.energy_time_steps or any(value <= 0.0 for value in args.energy_time_steps):
        parser.error("all energy time steps must be positive")
    return args


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return data


def run_command(command: list[str], stdout_path: Path, stderr_path: Path,
                timeout: float) -> tuple[int, float, str, str]:
    start = time.perf_counter()
    try:
        completed = subprocess.run(
            command,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
        code = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
    except subprocess.TimeoutExpired as error:
        code = 124
        stdout = error.stdout or ""
        stderr = (error.stderr or "") + f"\ntimeout after {timeout} seconds\n"
    elapsed = time.perf_counter() - start
    stdout_path.write_text(stdout, encoding="utf-8")
    stderr_path.write_text(stderr, encoding="utf-8")
    return code, elapsed, stdout, stderr


def discover_layouts(root: Path, max_cases: int | None) -> list[Path]:
    layouts = sorted(root.rglob("layout.ifcn"))
    successful: list[Path] = []
    for layout in layouts:
        report_path = layout.with_suffix(".ifcn.json")
        if not report_path.is_file():
            continue
        report = load_json(report_path)
        if report.get("directed_cycle_present") is True:
            successful.append(layout)
    return successful[:max_cases] if max_cases is not None else successful


def schedule_for(module: str) -> list[dict[str, int]]:
    if module.startswith("d_latch"):
        values = [
            (1, 0), (1, 0), (0, 1), (1, 1),
            (0, 0), (1, 0), (0, 1), (1, 1),
        ]
        return [{"clock": clock, "d": data} for clock, data in values]
    if "sr_nand" in module:
        values = [
            (1, 0), (1, 0), (1, 1), (0, 1),
            (1, 1), (1, 0), (1, 1), (0, 1),
        ]
        return [{"s_n": s_n, "r_n": r_n} for s_n, r_n in values]
    if "sr_majority" in module:
        values = [
            (0, 1), (0, 1), (0, 0), (1, 0),
            (1, 1), (0, 1), (0, 0), (1, 0),
        ]
        return [{"s": s, "r": r} for s, r in values]
    raise ValueError(f"no deterministic sequential stimulus schedule for {module}")


def write_vectors(path: Path, state: dict[str, Any], rows: list[dict[str, int]]) -> None:
    primary_inputs = state["cut_dag"]["primary_inputs"]
    event_port = [(str(entry["event"]), str(entry["port"])) for entry in primary_inputs]
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow([event for event, _ in event_port])
        for row in rows:
            writer.writerow([row[port] for _, port in event_port])


def three_not(value: int | None) -> int | None:
    return None if value is None else 1 - value


def three_and(left: int | None, right: int | None) -> int | None:
    if left == 0 or right == 0:
        return 0
    if left == 1 and right == 1:
        return 1
    return None


def three_or(left: int | None, right: int | None) -> int | None:
    if left == 1 or right == 1:
        return 1
    if left == 0 and right == 0:
        return 0
    return None


def three_majority(a: int | None, b: int | None, c: int | None) -> int | None:
    values = [a, b, c]
    if values.count(0) >= 2:
        return 0
    if values.count(1) >= 2:
        return 1
    return None


def expected_probe_traces(module: str, state: dict[str, Any],
                          rows: list[dict[str, int]]) -> dict[str, list[int | None]]:
    boundaries = state["state_boundaries"]
    traces = {str(boundary["data_event"]): [] for boundary in boundaries}
    signal_for_data = {
        str(boundary["data_event"]): str(boundary["q_signal"])
        for boundary in boundaries
    }

    if module.startswith("d_latch"):
        q: int | None = None
        for row in rows:
            q = row["d"] if row["clock"] else q
            traces[next(iter(traces))].append(q)
    elif "sr_nand_latch_sampled_topology" in module:
        q: int | None = None
        q_bar: int | None = None
        for row in rows:
            next_q = three_not(three_and(row["s_n"], q_bar))
            next_q_bar = three_not(three_and(row["r_n"], q))
            q, q_bar = next_q, next_q_bar
            for data_event, signal in signal_for_data.items():
                traces[data_event].append(q if signal == "q" else q_bar)
    elif "sr_nand" in module:
        q = None
        for row in rows:
            q = three_or(three_not(row["s_n"]), three_and(row["r_n"], q))
            traces[next(iter(traces))].append(q)
    elif "sr_majority" in module:
        q = None
        for row in rows:
            q = three_majority(row["s"], 1 - row["r"], q)
            traces[next(iter(traces))].append(q)
    else:
        raise ValueError(f"no recurrence oracle for {module}")
    return traces


def parse_rst(path: Path) -> dict[str, list[float]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    traces: dict[str, list[float]] = {}
    index = 0
    while index < len(lines):
        if lines[index] != "[TRACE]":
            index += 1
            continue
        label = ""
        data: list[float] | None = None
        cursor = index + 1
        while cursor < len(lines) and lines[cursor] != "[#TRACE]":
            if lines[cursor].startswith("data_labels="):
                label = lines[cursor].split("=", 1)[1]
            if lines[cursor] == "[TRACE_DATA]" and cursor + 1 < len(lines):
                data = [float(value) for value in lines[cursor + 1].split()]
            cursor += 1
        if data is not None:
            traces[label] = data
        index = cursor + 1
    return traces


def probe_diagnostic(waveform: Path, expected: dict[str, list[int | None]],
                     warmup_cycles: int, threshold: float) -> dict[str, Any]:
    physical = parse_rst(waveform)
    per_probe: list[dict[str, Any]] = []
    compared = 0
    matches = 0
    weak = 0
    for event, expected_values in expected.items():
        label = f"feedback_probe_{event}"
        samples = physical.get(label)
        if not samples:
            per_probe.append({"probe": label, "status": "missing"})
            continue
        cycles = len(expected_values)
        observed_polarization: list[float] = []
        observed_logic: list[int | None] = []
        for cycle in range(cycles):
            begin = int((cycle + 0.25) * len(samples) / cycles)
            end = max(begin + 1, int((cycle + 0.75) * len(samples) / cycles))
            median = statistics.median(samples[begin:end])
            observed_polarization.append(median)
            observed_logic.append(1 if median >= threshold else 0 if median <= -threshold else None)

        probe_compared = 0
        probe_matches = 0
        probe_weak = 0
        for cycle, (want, got) in enumerate(zip(expected_values, observed_logic)):
            if cycle < warmup_cycles or want is None:
                continue
            probe_compared += 1
            probe_weak += int(got is None)
            probe_matches += int(got == want)
        compared += probe_compared
        matches += probe_matches
        weak += probe_weak
        per_probe.append({
            "probe": label,
            "status": "diagnostic_pass" if probe_compared and probe_matches == probe_compared
                      else "diagnostic_fail",
            "expected_logic": expected_values,
            "observed_logic": observed_logic,
            "observed_stable_window_median": observed_polarization,
            "compared_cycles": probe_compared,
            "matching_cycles": probe_matches,
            "weak_cycles": probe_weak,
        })
    return {
        "scope": "diagnostic_only_uncharacterized_physical_state",
        "physical_state_signoff": False,
        "warmup_cycles_excluded": warmup_cycles,
        "stable_window_cycle_fraction": [0.25, 0.75],
        "logic_threshold": threshold,
        "compared_cycles": compared,
        "matching_cycles": matches,
        "weak_cycles": weak,
        "logic_agreement": matches / compared if compared else None,
        "status": "diagnostic_pass" if compared and matches == compared else "diagnostic_fail",
        "per_probe": per_probe,
    }


def parse_energy_report(path: Path) -> dict[str, Any]:
    scalars: dict[str, Any] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" not in line or line.startswith("["):
            continue
        key, value = line.split("=", 1)
        if key == "available":
            scalars[key] = value == "TRUE"
        elif key == "cycle_count":
            scalars[key] = int(value)
        elif key.startswith("total_") or key.startswith("average_"):
            scalars[key] = float(value)
    return scalars


def all_finite(values: Iterable[Any]) -> bool:
    numeric = [value for value in values if isinstance(value, (int, float))]
    return bool(numeric) and all(math.isfinite(float(value)) for value in numeric)


def machine_metadata() -> dict[str, Any]:
    cpu_model = None
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.is_file():
        for line in cpuinfo.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.lower().startswith("model name") and ":" in line:
                cpu_model = line.split(":", 1)[1].strip()
                break
    return {
        "platform": platform.platform(),
        "python": platform.python_version(),
        "cpu_model": cpu_model,
    }


def write_summary_csv(path: Path, records: list[dict[str, Any]]) -> None:
    rows: list[dict[str, Any]] = []
    for record in records:
        simulation = record.get("simulation", {})
        functional = record.get("functional_diagnostic", {})
        energy_runs = record.get("energy_runs", [])
        final_energy = energy_runs[-1] if energy_runs else {}
        rows.append({
            "case_id": record.get("case_id"),
            "benchmark_id": record.get("benchmark_id"),
            "status": record.get("status"),
            "nodes": record.get("pnr", {}).get("nodes"),
            "routes": record.get("pnr", {}).get("routes"),
            "feedback_routes": record.get("pnr", {}).get("feedback_routes"),
            "mapped_qca_cells": record.get("mapping", {}).get("qca_cells"),
            "crossovers": record.get("mapping", {}).get("crossovers"),
            "engine_equivalent": simulation.get("engine_equivalent"),
            "bistable_speedup": simulation.get("bistable_speedup"),
            "functional_scope": functional.get("scope"),
            "functional_status": functional.get("status"),
            "diagnostic_logic_agreement": functional.get("logic_agreement"),
            "physical_state_signoff": False,
            "energy_time_step_s": final_energy.get("time_step_s"),
            "energy_cycles": final_energy.get("cycle_count"),
            "average_error_energy_eV": final_energy.get("average_error_eV"),
            "average_bath_clock_energy_eV": final_energy.get("average_bath_clock_eV"),
            "derived_power_W": final_energy.get("derived_power_W"),
            "energy_finite": final_energy.get("finite"),
            "energy_exploratory_only": bool(energy_runs),
        })
    fieldnames = list(rows[0]) if rows else ["case_id", "status"]
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    input_root = args.input_directory.resolve()
    fixture_root = args.fixture_directory.resolve()
    output_root = args.output_directory.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    executables = {
        "energy": args.energy_executable.resolve(),
        "mapping": args.mapping_executable.resolve(),
        "physical_benchmark": args.physical_benchmark_executable.resolve(),
    }
    for name, executable in executables.items():
        if not executable.is_file():
            raise FileNotFoundError(f"{name} executable does not exist: {executable}")

    layouts = discover_layouts(input_root, args.max_cases)
    if not layouts:
        raise RuntimeError(f"no successful cyclic layout.ifcn files under {input_root}")

    records: list[dict[str, Any]] = []
    for layout in layouts:
        relative = layout.parent.relative_to(input_root)
        case_id = relative.as_posix()
        case_output = output_root / relative
        case_output.mkdir(parents=True, exist_ok=True)
        record: dict[str, Any] = {
            "case_id": case_id,
            "layout": str(layout),
            "status": "running",
            "physical_state_signoff": "not_characterized",
            "claims": {
                "mapping": "structural",
                "simulator_equivalence": "implementation_equivalence_only",
                "functional": "diagnostic_only",
                "energy": "exploratory_only",
            },
        }
        try:
            state = load_json(layout.parent / "state.json")
            pnr = load_json(layout.with_suffix(".ifcn.json"))
            fixture_manifest = fixture_root / relative / "benchmark.json"
            benchmark = load_json(fixture_manifest)
            module = str(state["source_module"])
            rows = schedule_for(module)
            vector_path = case_output / "stimulus.vt"
            write_vectors(vector_path, state, rows)
            probes = [str(boundary["data_event"]) for boundary in state["state_boundaries"]]
            expected = expected_probe_traces(module, state, rows)
            record["benchmark_id"] = benchmark.get("benchmark_id")
            record["module"] = module
            record["pnr"] = pnr
            record["stimulus"] = {
                "path": str(vector_path),
                "cycles": len(rows),
                "semantic_rows": rows,
                "physical_input_mapping": state["cut_dag"]["primary_inputs"],
            }
            record["feedback_probes"] = probes

            mapping_command = [str(executables["mapping"]), str(layout),
                               "--no-io-contraction", "--timing"]
            code, elapsed, stdout, stderr = run_command(
                mapping_command,
                case_output / "mapping.stdout.log",
                case_output / "mapping.stderr.log",
                args.timeout_seconds,
            )
            if code != 0:
                raise RuntimeError(f"mapping metrics failed ({code}): {stderr.strip()}")
            fields = stdout.strip().split()
            record["mapping"] = {
                "status": "pass",
                "cells_reported_by_mapping_metrics": int(fields[0]),
                "crossovers": int(fields[1]),
                "io_contraction_seconds": float(fields[2]),
                "elapsed_seconds": elapsed,
                "command": mapping_command,
            }

            baseline_qca_prefix = case_output / "unprobed"
            baseline_export_command = [
                str(executables["energy"]), str(layout), str(baseline_qca_prefix),
                "--qca-only",
            ]
            code, baseline_export_elapsed, stdout, stderr = run_command(
                baseline_export_command,
                case_output / "qca_unprobed_export.stdout.log",
                case_output / "qca_unprobed_export.stderr.log",
                args.timeout_seconds,
            )
            if code != 0:
                raise RuntimeError(f"unprobed QCA export failed ({code}): {stderr.strip()}")
            baseline_qca_path = Path(str(baseline_qca_prefix) + "_energy_input.qca")
            baseline_qca_cells = baseline_qca_path.read_text(encoding="utf-8").count(
                "[TYPE:QCADCell]"
            )

            qca_prefix = case_output / "instrumented"
            export_command = [str(executables["energy"]), str(layout), str(qca_prefix),
                              "--qca-only"]
            for probe in probes:
                export_command.extend(("--probe-node", probe))
            code, elapsed, stdout, stderr = run_command(
                export_command,
                case_output / "qca_export.stdout.log",
                case_output / "qca_export.stderr.log",
                args.timeout_seconds,
            )
            if code != 0:
                raise RuntimeError(f"QCA export failed ({code}): {stderr.strip()}")
            qca_path = Path(str(qca_prefix) + "_energy_input.qca")
            qca_text = qca_path.read_text(encoding="utf-8")
            qca_cells = qca_text.count("[TYPE:QCADCell]")
            qca_outputs = qca_text.count("cell_function=QCAD_CELL_OUTPUT")
            record["mapping"].update({
                "qca_path": str(qca_path),
                "qca_cells": qca_cells,
                "qca_probe_outputs": qca_outputs,
                "unprobed_qca_path": str(baseline_qca_path),
                "unprobed_qca_cells": baseline_qca_cells,
                "probe_is_cell_neutral": qca_cells == baseline_qca_cells,
                "pnr_reported_cells": int(pnr["mapped_qca_cells"]),
                "current_mapping_matches_pnr_report": (
                    int(fields[0]) == int(pnr["mapped_qca_cells"])
                ),
                "current_qca_export_matches_pnr_report": (
                    baseline_qca_cells == int(pnr["mapped_qca_cells"])
                ),
                "baseline_export_elapsed_seconds": baseline_export_elapsed,
                "export_elapsed_seconds": elapsed,
                "baseline_export_command": baseline_export_command,
                "export_command": export_command,
            })

            if args.sim_model != "none":
                sim_prefix = case_output / "physical"
                sim_json = case_output / "physical.json"
                sim_csv = case_output / "physical.csv"
                duration = len(rows) * args.clock_period
                sim_command = [
                    str(executables["physical_benchmark"]), str(qca_path),
                    "--model", args.sim_model,
                    "--vectors", str(vector_path),
                    "--repetitions", str(args.sim_repetitions),
                    "--warmup", str(args.sim_warmup),
                    "--samples", str(args.sim_samples),
                    "--logic-threshold", str(args.logic_threshold),
                    "--time-step", str(args.sim_time_step),
                    "--duration", str(duration),
                    "--numeric-method", args.sim_numeric_method,
                    "--output-prefix", str(sim_prefix),
                    "--json", str(sim_json),
                    "--csv", str(sim_csv),
                    "--require-equivalent",
                ]
                code, elapsed, stdout, stderr = run_command(
                    sim_command,
                    case_output / "physical.stdout.log",
                    case_output / "physical.stderr.log",
                    args.timeout_seconds,
                )
                if code not in (0, 3):
                    raise RuntimeError(f"physical benchmark failed ({code}): {stderr.strip()}")
                sim_report = load_json(sim_json)
                comparisons = sim_report.get("comparisons", [])
                simulation: dict[str, Any] = {
                    "scope": "baseline_vs_accelerated_implementation_equivalence",
                    "does_not_establish_rtl_correctness": True,
                    "elapsed_seconds": elapsed,
                    "exit_code": code,
                    "engine_equivalent": code == 0,
                    "report": str(sim_json),
                    "command": sim_command,
                    "comparisons": comparisons,
                }
                for comparison in comparisons:
                    simulation[f"{comparison['model']}_speedup"] = comparison.get("speedup")
                record["simulation"] = simulation

                diagnostic_waveform = Path(str(sim_prefix) + "_bistable_baseline.rst")
                if diagnostic_waveform.is_file():
                    record["functional_diagnostic"] = probe_diagnostic(
                        diagnostic_waveform,
                        expected,
                        args.functional_warmup_cycles,
                        args.logic_threshold,
                    )
                else:
                    record["functional_diagnostic"] = {
                        "scope": "diagnostic_only_uncharacterized_physical_state",
                        "physical_state_signoff": False,
                        "status": "not_run_without_bistable_waveform",
                    }

            energy_runs: list[dict[str, Any]] = []
            if not args.skip_energy:
                duration = len(rows) * args.clock_period
                for index, time_step in enumerate(args.energy_time_steps):
                    energy_prefix = case_output / f"energy_dt_{time_step:.6g}"
                    energy_command = [
                        str(executables["energy"]), str(qca_path), str(energy_prefix),
                        "--vectors", str(vector_path),
                        "--time-step", str(time_step),
                        "--duration", str(duration),
                        "--clock-period", str(args.clock_period),
                        "--input-period", str(args.clock_period),
                        "--clock-slope", str(args.clock_slope),
                    ]
                    code, elapsed, stdout, stderr = run_command(
                        energy_command,
                        case_output / f"energy_{index}.stdout.log",
                        case_output / f"energy_{index}.stderr.log",
                        args.timeout_seconds,
                    )
                    energy_record: dict[str, Any] = {
                        "time_step_s": time_step,
                        "duration_s": duration,
                        "clock_period_s": args.clock_period,
                        "elapsed_seconds": elapsed,
                        "exit_code": code,
                        "command": energy_command,
                        "scope": "exploratory_uncharacterized_physical_state",
                        "physical_state_signoff": False,
                    }
                    report_path = Path(str(energy_prefix) + "_energy.txt")
                    if code == 0 and report_path.is_file():
                        parsed = parse_energy_report(report_path)
                        energy_record.update({
                            "report": str(report_path),
                            "cycle_count": parsed.get("cycle_count"),
                            "total_error_eV": parsed.get("total_error_eV"),
                            "total_bath_clock_eV": parsed.get("total_bath_clock_eV"),
                            "average_error_eV": parsed.get("average_error_eV"),
                            "average_bath_clock_eV": parsed.get("average_bath_clock_eV"),
                            "finite": all_finite(parsed.values()),
                        })
                        average_error = parsed.get("average_error_eV")
                        if isinstance(average_error, float) and math.isfinite(average_error):
                            energy_record["derived_power_W"] = (
                                average_error * ELECTRON_VOLT_J / args.clock_period
                            )
                    else:
                        energy_record["finite"] = False
                        energy_record["error"] = stderr.strip() or stdout.strip()
                    energy_runs.append(energy_record)
            record["energy_runs"] = energy_runs
            if len(energy_runs) >= 2:
                previous = energy_runs[-2].get("average_error_eV")
                final = energy_runs[-1].get("average_error_eV")
                denominator = max(abs(final or 0.0), 1.0e-30)
                record["energy_step_convergence"] = {
                    "relative_change_last_two": (
                        abs(final - previous) / denominator
                        if isinstance(previous, float) and isinstance(final, float)
                        and math.isfinite(previous) and math.isfinite(final)
                        else None
                    ),
                    "paper_ready": False,
                    "reason": "physical state is uncharacterized even if numerical integration converges",
                }
            record["status"] = "completed_exploratory_physical_analysis"
        except Exception as error:  # keep other benchmark cases running
            record["status"] = "error"
            record["error"] = str(error)
            records.append(record)
            (case_output / "result.json").write_text(
                json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            if args.fail_fast:
                break
            continue

        records.append(record)
        (case_output / "result.json").write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    summary = {
        "schema": "ifcn.sequential-cyclic-physical-benchmark.v1",
        "generated_unix_time": time.time(),
        "input_directory": str(input_root),
        "output_directory": str(output_root),
        "machine": machine_metadata(),
        "executables": {name: str(path) for name, path in executables.items()},
        "claim_boundary": {
            "physical_state_signoff": "not_characterized",
            "functional_waveform": "diagnostic_only",
            "simulator_comparison": "implementation_equivalence_only",
            "energy": "exploratory_only",
        },
        "records": records,
    }
    (output_root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_summary_csv(output_root / "summary.csv", records)
    errors = sum(record.get("status") == "error" for record in records)
    print(f"completed={len(records) - errors} errors={errors} output={output_root}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
