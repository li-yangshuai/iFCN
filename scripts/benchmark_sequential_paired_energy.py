#!/usr/bin/env python3
"""Run paired raw-versus-compact energy experiments for sequential layouts.

The comparison is intentionally strict: both layouts must have byte-identical
``cut.v`` and ``state.json`` inputs and the same initiation interval (II).  A
workload contains one startup update, eight warm-up updates, four measured
updates, and two unmeasured drain updates.  The drain updates let the last
measured vector traverse every clock zone before the simulation stops.  Only
complete ``E_bath_eV`` rows from the measured ``[PER_CYCLE]`` window are used;
signed energy-balance quantities are retained in the raw report but are never
treated as dissipated energy or power here.

The Simon energy model reports one row per four-phase clock period.  An
accepted sequential update spans ``II / 4`` such rows, so this runner reports

    T_accept = (II / 4) * 10 ps
    P_bath   = E_bath_per_update / T_accept.

Physical state macros are not characterized by this script.  Its outputs are
therefore paired numerical energy evidence, not physical-state signoff.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import platform
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


ELECTRON_VOLT_J = 1.602176634e-19
STARTUP_UPDATES = 1
WARMUP_UPDATES = 8
MEASURED_UPDATES = 4
TOTAL_UPDATES = STARTUP_UPDATES + WARMUP_UPDATES + MEASURED_UPDATES
DRAIN_UPDATES = 2
SIMULATION_UPDATES = TOTAL_UPDATES + DRAIN_UPDATES
DEFAULT_DESIGNS = (
    "toggle_ff",
    "enable_hold_ff",
    "johnson2_sync",
    "johnson4_free_running",
    "reconvergent_feedback_ff",
)


# Each stage is listed explicitly so that summary.json is a complete workload
# record rather than depending on an implicit pseudo-random generator.
DEFAULT_WORKLOADS: dict[str, dict[str, Any]] = {
    "toggle_ff": {
        "probe_nodes": ["d0"],
        "initialization": "synchronous reset during the startup update",
        "startup": [{"rst": 1}],
        "warmup": [{"rst": 0}] * 8,
        "measured": [{"rst": 0}] * 4,
        "drain": [{"rst": 0}] * 2,
    },
    "enable_hold_ff": {
        "probe_nodes": ["d0"],
        "initialization": "synchronous reset during the startup update",
        "startup": [{"rst": 1, "en": 0, "d": 0}],
        "warmup": [
            {"rst": 0, "en": 1, "d": 1},
            {"rst": 0, "en": 0, "d": 0},
            {"rst": 0, "en": 1, "d": 0},
            {"rst": 0, "en": 0, "d": 1},
            {"rst": 0, "en": 1, "d": 1},
            {"rst": 0, "en": 0, "d": 0},
            {"rst": 0, "en": 1, "d": 0},
            {"rst": 0, "en": 0, "d": 1},
        ],
        "measured": [
            {"rst": 0, "en": 1, "d": 1},
            {"rst": 0, "en": 0, "d": 0},
            {"rst": 0, "en": 1, "d": 0},
            {"rst": 0, "en": 0, "d": 1},
        ],
        # Hold the final measured input while the last update drains through
        # all four clock zones, avoiding a new transition at the boundary.
        "drain": [
            {"rst": 0, "en": 0, "d": 1},
            {"rst": 0, "en": 0, "d": 1},
        ],
    },
    "johnson2_sync": {
        "probe_nodes": ["d0", "d1"],
        "initialization": "synchronous reset during the startup update",
        "startup": [{"rst": 1}],
        "warmup": [{"rst": 0}] * 8,
        "measured": [{"rst": 0}] * 4,
        "drain": [{"rst": 0}] * 2,
    },
    "johnson4_free_running": {
        "probe_nodes": ["d0", "d1", "d2", "d3"],
        "initialization": "no RTL reset; simulator equilibrium initialization only",
        "startup": [{}],
        "warmup": [{} for _ in range(8)],
        "measured": [{} for _ in range(4)],
        "drain": [{} for _ in range(2)],
    },
    "reconvergent_feedback_ff": {
        "probe_nodes": ["d0"],
        "initialization": "synchronous reset during the startup update",
        "startup": [{"rst": 1, "a": 0, "b": 0}],
        "warmup": [
            {"rst": 0, "a": 1, "b": 0},
            {"rst": 0, "a": 0, "b": 0},
            {"rst": 0, "a": 1, "b": 1},
            {"rst": 0, "a": 0, "b": 1},
            {"rst": 0, "a": 1, "b": 0},
            {"rst": 0, "a": 0, "b": 0},
            {"rst": 0, "a": 1, "b": 1},
            {"rst": 0, "a": 0, "b": 1},
        ],
        "measured": [
            {"rst": 0, "a": 1, "b": 0},
            {"rst": 0, "a": 0, "b": 0},
            {"rst": 0, "a": 1, "b": 1},
            {"rst": 0, "a": 0, "b": 1},
        ],
        "drain": [
            {"rst": 0, "a": 0, "b": 1},
            {"rst": 0, "a": 0, "b": 1},
        ],
    },
}


@dataclass(frozen=True)
class PairInputs:
    design: str
    raw_layout: Path
    final_layout: Path
    raw_report: Mapping[str, Any]
    final_report: Mapping[str, Any]
    state: Mapping[str, Any]
    cut_sha256: str
    state_sha256: str
    initiation_interval: int


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--raw-directory",
        type=Path,
        default=Path("build/artifacts/sequential_raw_baseline_final_20260825"),
    )
    parser.add_argument(
        "--final-directory",
        type=Path,
        default=Path("build/artifacts/sequential_layout_compact_final_20260825"),
    )
    parser.add_argument("--variant", default="cyclic_z3_adaptive")
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(
        "--energy-executable", type=Path, default=Path("build/ifcn_energy_analysis")
    )
    parser.add_argument(
        "--time-steps",
        default="1.25e-16,6.25e-17,3.125e-17",
        help="comma-separated integration steps, in seconds (coarse to fine)",
    )
    parser.add_argument("--convergence-threshold", type=float, default=0.01)
    parser.add_argument("--clock-period", type=float, default=1.0e-11)
    parser.add_argument("--clock-slope", type=float, default=1.0e-12)
    parser.add_argument("--timeout-seconds", type=float, default=1800.0)
    parser.add_argument(
        "--design",
        action="append",
        choices=DEFAULT_DESIGNS,
        help="run one design; repeat for several (default: all five)",
    )
    parser.add_argument(
        "--workload-manifest",
        type=Path,
        help="optional JSON object replacing the built-in explicit workloads",
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="validate pairs/workloads and write summaries without running Simon",
    )
    parser.add_argument("--require-converged", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args(argv)

    try:
        args.time_steps = [float(value) for value in args.time_steps.split(",")]
    except ValueError as error:
        parser.error(f"invalid --time-steps: {error}")
    if not args.time_steps or any(not math.isfinite(value) or value <= 0 for value in args.time_steps):
        parser.error("all --time-steps values must be finite and positive")
    if len(set(args.time_steps)) != len(args.time_steps):
        parser.error("--time-steps must not contain duplicates")
    # Convergence is always evaluated from coarse to fine, independent of CLI order.
    args.time_steps = sorted(args.time_steps, reverse=True)
    if not 0 < args.convergence_threshold < 1:
        parser.error("--convergence-threshold must be between 0 and 1")
    if args.clock_period <= 0 or args.clock_slope <= 0 or args.timeout_seconds <= 0:
        parser.error("clock timing and timeout values must be positive")
    args.designs = args.design or list(DEFAULT_DESIGNS)
    return args


def load_json_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise FileNotFoundError(path)
    return path


def validate_pair(raw_root: Path, final_root: Path, design: str, variant: str) -> PairInputs:
    raw_design = raw_root / design
    final_design = final_root / design
    raw_cut = require_file(raw_design / "cut.v")
    final_cut = require_file(final_design / "cut.v")
    raw_state_path = require_file(raw_design / "state.json")
    final_state_path = require_file(final_design / "state.json")
    raw_layout = require_file(raw_design / variant / "layout.ifcn")
    final_layout = require_file(final_design / variant / "layout.ifcn")
    raw_report_path = require_file(raw_design / variant / "layout.ifcn.json")
    final_report_path = require_file(final_design / variant / "layout.ifcn.json")

    raw_cut_hash = sha256_file(raw_cut)
    final_cut_hash = sha256_file(final_cut)
    if raw_cut_hash != final_cut_hash:
        raise ValueError(f"{design}: raw/final cut.v hashes differ")
    raw_state_hash = sha256_file(raw_state_path)
    final_state_hash = sha256_file(final_state_path)
    if raw_state_hash != final_state_hash:
        raise ValueError(f"{design}: raw/final state.json hashes differ")

    state = load_json_object(raw_state_path)
    raw_report = load_json_object(raw_report_path)
    final_report = load_json_object(final_report_path)
    raw_ii = raw_report.get("initiation_interval")
    final_ii = final_report.get("initiation_interval")
    if not isinstance(raw_ii, int) or isinstance(raw_ii, bool) or raw_ii <= 0:
        raise ValueError(f"{design}: invalid raw initiation_interval {raw_ii!r}")
    if raw_ii != final_ii:
        raise ValueError(f"{design}: raw/final initiation intervals differ ({raw_ii} != {final_ii})")
    if raw_ii % 4:
        raise ValueError(f"{design}: initiation interval {raw_ii} is not divisible by 4")
    for label, report in (("raw", raw_report), ("final", final_report)):
        if report.get("directed_cycle_present") is not True:
            raise ValueError(f"{design}: {label} report does not retain a directed cycle")
        if report.get("mapping_drc") is not True:
            raise ValueError(f"{design}: {label} report did not pass mapping DRC")
        if not str(report.get("status", "")).startswith("success"):
            raise ValueError(f"{design}: {label} layout report is not successful")
    if state.get("source_module") != design:
        raise ValueError(
            f"{design}: state source_module is {state.get('source_module')!r}"
        )
    return PairInputs(
        design=design,
        raw_layout=raw_layout,
        final_layout=final_layout,
        raw_report=raw_report,
        final_report=final_report,
        state=state,
        cut_sha256=raw_cut_hash,
        state_sha256=raw_state_hash,
        initiation_interval=raw_ii,
    )


def load_workloads(path: Path | None) -> dict[str, dict[str, Any]]:
    if path is None:
        # JSON round-tripping provides a deep copy of the literal workload record.
        return json.loads(json.dumps(DEFAULT_WORKLOADS))
    value = load_json_object(path)
    workloads: dict[str, dict[str, Any]] = {}
    for design, workload in value.items():
        if not isinstance(workload, dict):
            raise ValueError(f"workload for {design} must be a JSON object")
        workloads[str(design)] = workload
    return workloads


def validate_workload(
    design: str, workload: Mapping[str, Any], state: Mapping[str, Any]
) -> dict[str, Any]:
    expected_lengths = {
        "startup": STARTUP_UPDATES,
        "warmup": WARMUP_UPDATES,
        "measured": MEASURED_UPDATES,
        "drain": DRAIN_UPDATES,
    }
    rows: list[dict[str, int]] = []
    normalized_stages: dict[str, list[dict[str, int]]] = {}
    for stage, expected_length in expected_lengths.items():
        stage_rows = workload.get(stage)
        if not isinstance(stage_rows, list) or len(stage_rows) != expected_length:
            raise ValueError(
                f"{design}: workload {stage} must contain {expected_length} rows"
            )
        normalized_stage: list[dict[str, int]] = []
        for row in stage_rows:
            if not isinstance(row, dict):
                raise ValueError(f"{design}: every workload row must be an object")
            normalized: dict[str, int] = {}
            for key, value in row.items():
                if value not in (0, 1) or isinstance(value, bool):
                    raise ValueError(f"{design}: vector {key}={value!r} is not binary")
                normalized[str(key)] = int(value)
            rows.append(normalized)
            normalized_stage.append(normalized)
        normalized_stages[stage] = normalized_stage

    expected_drain = [normalized_stages["measured"][-1]] * DRAIN_UPDATES
    if normalized_stages["drain"] != expected_drain:
        raise ValueError(
            f"{design}: drain rows must repeat the final measured row so no "
            "new input transition contaminates the measurement tail"
        )

    cut_dag = state.get("cut_dag")
    if not isinstance(cut_dag, dict):
        raise ValueError(f"{design}: missing state cut_dag")
    primary_inputs = cut_dag.get("primary_inputs")
    if not isinstance(primary_inputs, list):
        raise ValueError(f"{design}: missing state primary_inputs")
    event_port: list[tuple[str, str]] = []
    for item in primary_inputs:
        if not isinstance(item, dict) or "event" not in item or "port" not in item:
            raise ValueError(f"{design}: malformed primary-input mapping")
        event_port.append((str(item["event"]), str(item["port"])))
    expected_ports = {port for _, port in event_port}
    for index, row in enumerate(rows):
        if set(row) != expected_ports:
            raise ValueError(
                f"{design}: workload row {index} ports {sorted(row)} != "
                f"state ports {sorted(expected_ports)}"
            )

    boundaries = state.get("state_boundaries")
    if not isinstance(boundaries, list):
        raise ValueError(f"{design}: missing state boundaries")
    valid_probes = {
        str(boundary["data_event"])
        for boundary in boundaries
        if isinstance(boundary, dict) and "data_event" in boundary
    }
    probes = workload.get("probe_nodes")
    if not isinstance(probes, list) or not probes or len(set(probes)) != len(probes):
        raise ValueError(f"{design}: probe_nodes must be a non-empty unique list")
    probes = [str(probe) for probe in probes]
    if not set(probes).issubset(valid_probes):
        raise ValueError(
            f"{design}: probes {probes} are not state data events {sorted(valid_probes)}"
        )
    return {
        "probe_nodes": probes,
        "initialization": str(workload.get("initialization", "unspecified")),
        "startup": normalized_stages["startup"],
        "warmup": normalized_stages["warmup"],
        "measured": normalized_stages["measured"],
        "drain": normalized_stages["drain"],
        "rows": rows,
        "event_port": event_port,
        "has_primary_inputs": bool(event_port),
    }


def write_vectors(path: Path, event_port: Sequence[tuple[str, str]], rows: Sequence[Mapping[str, int]]) -> None:
    if not event_port:
        raise ValueError("a zero-input workload has no vector-table representation")
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow([event for event, _ in event_port])
        for row in rows:
            writer.writerow([row[port] for _, port in event_port])


def workload_exclusion_reason(workload: Mapping[str, Any]) -> str | None:
    """Fail closed when the workload cannot establish a reproducible state.

    The current Simon CLI can drive primary-input vector tables, but it cannot
    seed an internal feedback polarization.  Consequently a zero-PI design may
    be structurally validated but must not enter the paired energy table.
    """

    if not bool(workload.get("has_primary_inputs")):
        return "excluded_no_state_initialization"
    return None


def parse_energy_report_text(text: str) -> dict[str, Any]:
    lines = text.splitlines()
    try:
        section_start = lines.index("[PER_CYCLE]")
        section_end = lines.index("[#PER_CYCLE]", section_start + 1)
    except ValueError as error:
        raise ValueError("energy report has no complete [PER_CYCLE] section") from error
    if section_end <= section_start + 1:
        raise ValueError("energy report has an empty [PER_CYCLE] section")

    scalars: dict[str, Any] = {}
    for line in lines[:section_start]:
        if "=" not in line or line.startswith("["):
            continue
        key, value = line.split("=", 1)
        if key == "available":
            scalars[key] = value.strip().upper() == "TRUE"
        elif key == "cycle_count":
            scalars[key] = int(value)
        elif key.startswith(("total_", "average_")):
            scalars[key] = float(value)

    rows: list[dict[str, float | int]] = []
    reader = csv.DictReader(lines[section_start + 1 : section_end])
    required = {
        "cycle",
        "E_bath_eV",
        "E_clk_eV",
        "E_io_eV",
        "E_error_eV",
        "E_bath_clk_eV",
    }
    if reader.fieldnames is None or set(reader.fieldnames) != required:
        raise ValueError(f"unexpected PER_CYCLE columns: {reader.fieldnames}")
    seen: set[int] = set()
    for row in reader:
        cycle = int(row["cycle"])
        if cycle in seen:
            raise ValueError(f"duplicate energy cycle {cycle}")
        seen.add(cycle)
        parsed: dict[str, float | int] = {"cycle": cycle}
        for key in required - {"cycle"}:
            value = float(row[key])
            if not math.isfinite(value):
                raise ValueError(f"non-finite {key} in cycle {cycle}")
            parsed[key] = value
        rows.append(parsed)
    rows.sort(key=lambda item: int(item["cycle"]))
    if scalars.get("available") is not True:
        raise ValueError("energy report says analysis is unavailable")
    if scalars.get("cycle_count") != len(rows):
        raise ValueError(
            f"cycle_count={scalars.get('cycle_count')} but PER_CYCLE has {len(rows)} rows"
        )
    return {"scalars": scalars, "cycles": rows}


def parse_energy_report(path: Path) -> dict[str, Any]:
    return parse_energy_report_text(path.read_text(encoding="utf-8"))


def measured_bath_window(
    cycles: Sequence[Mapping[str, float | int]], initiation_interval: int
) -> dict[str, Any]:
    if initiation_interval <= 0 or initiation_interval % 4:
        raise ValueError("initiation interval must be a positive multiple of four")
    clock_cycles_per_update = initiation_interval // 4
    # PER_CYCLE uses one-based physical clock periods, so measured semantic
    # updates 9..12 occupy cycles 9*k+1 .. 13*k, where k=II/4.  The final row
    # reported by Simon is only partially accumulated because clock zones
    # close at staggered boundaries.  Drain updates move that partial tail
    # beyond the complete measurement window.
    first_cycle = (STARTUP_UPDATES + WARMUP_UPDATES) * clock_cycles_per_update + 1
    last_cycle = TOTAL_UPDATES * clock_cycles_per_update
    expected_cycles = list(range(first_cycle, last_cycle + 1))
    by_index = {int(row["cycle"]): row for row in cycles}
    missing = [cycle for cycle in expected_cycles if cycle not in by_index]
    if missing:
        raise ValueError(f"energy report is missing measured cycles {missing}")
    if not cycles or max(int(row["cycle"]) for row in cycles) <= last_cycle:
        raise ValueError(
            "energy report has no post-measurement guard cycle; "
            "the last measured cycle may be only partially integrated"
        )
    selected = [by_index[cycle] for cycle in expected_cycles]

    update_energies: list[float] = []
    update_cycle_indices: list[list[int]] = []
    for update in range(MEASURED_UPDATES):
        begin = update * clock_cycles_per_update
        group = selected[begin : begin + clock_cycles_per_update]
        update_energies.append(sum(float(row["E_bath_eV"]) for row in group))
        update_cycle_indices.append([int(row["cycle"]) for row in group])
    mean = statistics.fmean(update_energies)
    return {
        "clock_cycles_per_accepted_update": clock_cycles_per_update,
        "first_measured_cycle": first_cycle,
        "last_measured_cycle": last_cycle,
        "measured_cycle_count": len(selected),
        "update_cycle_indices": update_cycle_indices,
        "bath_eV_per_update": update_energies,
        "total_bath_eV": sum(update_energies),
        "mean_bath_eV_per_update": mean,
        "sample_stddev_bath_eV_per_update": (
            statistics.stdev(update_energies) if len(update_energies) >= 2 else 0.0
        ),
        "all_bath_values_nonnegative": all(value >= 0 for value in update_energies),
    }


def relative_change(coarse: float, fine: float) -> float:
    if not math.isfinite(coarse) or not math.isfinite(fine):
        raise ValueError("convergence inputs must be finite")
    return abs(fine - coarse) / max(abs(fine), 1.0e-30)


def convergence_summary(runs: Sequence[Mapping[str, Any]], threshold: float) -> dict[str, Any]:
    valid = [run for run in runs if isinstance(run.get("mean_bath_eV_per_update"), (int, float))]
    valid.sort(key=lambda run: float(run["time_step_s"]), reverse=True)
    comparisons = []
    for coarse, fine in zip(valid, valid[1:]):
        change = relative_change(
            float(coarse["mean_bath_eV_per_update"]),
            float(fine["mean_bath_eV_per_update"]),
        )
        comparisons.append(
            {
                "coarse_time_step_s": coarse["time_step_s"],
                "fine_time_step_s": fine["time_step_s"],
                "relative_change": change,
            }
        )
    final_change = comparisons[-1]["relative_change"] if comparisons else None
    return {
        "threshold": threshold,
        "comparisons": comparisons,
        "relative_change_last_two": final_change,
        "converged": final_change is not None and final_change <= threshold,
        "criterion": "last two bath-energy-per-update values",
    }


def bath_power_watts(bath_eV_per_update: float, acceptance_time_s: float) -> float:
    if acceptance_time_s <= 0:
        raise ValueError("acceptance time must be positive")
    return bath_eV_per_update * ELECTRON_VOLT_J / acceptance_time_s


def percent_reduction(baseline: float, candidate: float) -> float | None:
    if not math.isfinite(baseline) or not math.isfinite(candidate) or baseline == 0:
        return None
    return 100.0 * (baseline - candidate) / baseline


def run_command(command: Sequence[str], stdout_path: Path, stderr_path: Path, timeout: float) -> tuple[int, float, str, str]:
    start = time.perf_counter()
    try:
        completed = subprocess.run(
            list(command), text=True, capture_output=True, check=False, timeout=timeout
        )
        code, stdout, stderr = completed.returncode, completed.stdout, completed.stderr
    except subprocess.TimeoutExpired as error:
        code = 124
        stdout = error.stdout or ""
        stderr = (error.stderr or "") + f"\ntimeout after {timeout} seconds\n"
    elapsed = time.perf_counter() - start
    stdout_path.write_text(stdout, encoding="utf-8")
    stderr_path.write_text(stderr, encoding="utf-8")
    return code, elapsed, stdout, stderr


def qca_cell_count(path: Path) -> int:
    return path.read_text(encoding="utf-8", errors="replace").count("[TYPE:QCADCell]")


def time_step_slug(value: float) -> str:
    return f"{value:.12g}".replace(".", "p").replace("-", "m").replace("+", "p")


def selected_layout_metrics(report: Mapping[str, Any]) -> dict[str, Any]:
    keys = (
        "bbox_width",
        "bbox_height",
        "bbox_area",
        "route_steps",
        "mapped_qca_cells",
        "mapped_layer_cell_records",
        "crossover_segments",
    )
    return {key: report.get(key) for key in keys}


def run_layout_energy(
    *,
    label: str,
    layout: Path,
    report: Mapping[str, Any],
    design_output: Path,
    vector_path: Path | None,
    probes: Sequence[str],
    energy_executable: Path,
    initiation_interval: int,
    time_steps: Sequence[float],
    clock_period: float,
    clock_slope: float,
    convergence_threshold: float,
    timeout: float,
) -> dict[str, Any]:
    layout_output = design_output / label
    layout_output.mkdir(parents=True, exist_ok=True)
    qca_prefix = layout_output / "instrumented"
    export_command = [str(energy_executable), str(layout), str(qca_prefix), "--qca-only"]
    for probe in probes:
        export_command.extend(("--probe-node", probe))
    code, elapsed, stdout, stderr = run_command(
        export_command,
        layout_output / "qca_export.stdout.log",
        layout_output / "qca_export.stderr.log",
        timeout,
    )
    if code != 0:
        raise RuntimeError(f"{label} QCA export failed ({code}): {stderr.strip() or stdout.strip()}")
    qca_path = Path(str(qca_prefix) + "_energy_input.qca")
    require_file(qca_path)

    clock_cycles_per_update = initiation_interval // 4
    acceptance_time = clock_cycles_per_update * clock_period
    duration = SIMULATION_UPDATES * acceptance_time
    runs: list[dict[str, Any]] = []
    for index, time_step in enumerate(time_steps):
        energy_prefix = layout_output / f"energy_dt_{time_step_slug(time_step)}"
        command = [
            str(energy_executable),
            str(qca_path),
            str(energy_prefix),
            "--time-step",
            f"{time_step:.17g}",
            "--duration",
            f"{duration:.17g}",
            "--clock-period",
            f"{clock_period:.17g}",
            "--input-period",
            f"{acceptance_time:.17g}",
            "--clock-slope",
            f"{clock_slope:.17g}",
        ]
        if vector_path is not None:
            command.extend(("--vectors", str(vector_path)))
        code, run_elapsed, stdout, stderr = run_command(
            command,
            layout_output / f"energy_{index}.stdout.log",
            layout_output / f"energy_{index}.stderr.log",
            timeout,
        )
        if code != 0:
            raise RuntimeError(
                f"{label} energy dt={time_step:g} failed ({code}): "
                f"{stderr.strip() or stdout.strip()}"
            )
        report_path = Path(str(energy_prefix) + "_energy.txt")
        parsed = parse_energy_report(require_file(report_path))
        window = measured_bath_window(parsed["cycles"], initiation_interval)
        mean_bath = float(window["mean_bath_eV_per_update"])
        runs.append(
            {
                "time_step_s": time_step,
                "duration_s": duration,
                "clock_period_s": clock_period,
                "acceptance_time_s": acceptance_time,
                "elapsed_seconds": run_elapsed,
                "report": str(report_path),
                "command": command,
                "report_scalars": parsed["scalars"],
                "measurement": window,
                "mean_bath_eV_per_update": mean_bath,
                "bath_power_W": bath_power_watts(mean_bath, acceptance_time),
            }
        )
    convergence = convergence_summary(runs, convergence_threshold)
    finest = min(runs, key=lambda run: float(run["time_step_s"]))
    numerically_valid = all(
        bool(run["measurement"]["all_bath_values_nonnegative"]) for run in runs
    )
    return {
        "layout": str(layout),
        "layout_metrics": selected_layout_metrics(report),
        "qca_path": str(qca_path),
        "qca_cells": qca_cell_count(qca_path),
        "qca_export_elapsed_seconds": elapsed,
        "qca_export_command": export_command,
        "energy_runs": runs,
        "convergence": convergence,
        "nonnegative_measured_bath_energy": numerically_valid,
        "finest": {
            "time_step_s": finest["time_step_s"],
            "mean_bath_eV_per_update": finest["mean_bath_eV_per_update"],
            "bath_power_W": finest["bath_power_W"],
            "measurement": finest["measurement"],
        },
    }


def make_design_record(
    pair: PairInputs,
    workload: Mapping[str, Any],
    output_root: Path,
    args: argparse.Namespace,
) -> dict[str, Any]:
    design_output = output_root / pair.design
    design_output.mkdir(parents=True, exist_ok=True)
    normalized_workload = validate_workload(pair.design, workload, pair.state)
    vector_path: Path | None = None
    if normalized_workload["has_primary_inputs"]:
        vector_path = design_output / "stimulus.vt"
        write_vectors(
            vector_path,
            normalized_workload["event_port"],
            normalized_workload["rows"],
        )
    acceptance_time = (pair.initiation_interval // 4) * args.clock_period
    record: dict[str, Any] = {
        "design": pair.design,
        "status": "validated_only" if args.validate_only else "running",
        "pair_validation": {
            "cut_v_identical": True,
            "state_json_identical": True,
            "initiation_interval_identical": True,
            "cut_v_sha256": pair.cut_sha256,
            "state_json_sha256": pair.state_sha256,
        },
        "initiation_interval": pair.initiation_interval,
        "clock_cycles_per_accepted_update": pair.initiation_interval // 4,
        "acceptance_time_s": acceptance_time,
        "workload": {
            key: value
            for key, value in normalized_workload.items()
            if key not in ("rows", "event_port", "has_primary_inputs")
        },
        "stimulus_path": str(vector_path) if vector_path else None,
        "raw": {
            "layout": str(pair.raw_layout),
            "layout_metrics": selected_layout_metrics(pair.raw_report),
        },
        "final": {
            "layout": str(pair.final_layout),
            "layout_metrics": selected_layout_metrics(pair.final_report),
        },
    }
    exclusion = workload_exclusion_reason(normalized_workload)
    if exclusion is not None:
        record["status"] = exclusion
        record["exclusion"] = {
            "reason": exclusion,
            "detail": (
                "design has no primary input/reset and the energy CLI has no "
                "internal-state seed mechanism; exhaustive equilibrium is forbidden"
            ),
        }
        return record
    if args.validate_only:
        return record

    common = {
        "design_output": design_output,
        "vector_path": vector_path,
        "probes": normalized_workload["probe_nodes"],
        "energy_executable": args.energy_executable,
        "initiation_interval": pair.initiation_interval,
        "time_steps": args.time_steps,
        "clock_period": args.clock_period,
        "clock_slope": args.clock_slope,
        "convergence_threshold": args.convergence_threshold,
        "timeout": args.timeout_seconds,
    }
    record["raw"] = run_layout_energy(
        label="raw", layout=pair.raw_layout, report=pair.raw_report, **common
    )
    record["final"] = run_layout_energy(
        label="final", layout=pair.final_layout, report=pair.final_report, **common
    )
    raw_energy = float(record["raw"]["finest"]["mean_bath_eV_per_update"])
    final_energy = float(record["final"]["finest"]["mean_bath_eV_per_update"])
    raw_power = float(record["raw"]["finest"]["bath_power_W"])
    final_power = float(record["final"]["finest"]["bath_power_W"])
    both_converged = bool(record["raw"]["convergence"]["converged"]) and bool(
        record["final"]["convergence"]["converged"]
    )
    numerically_valid = bool(record["raw"]["nonnegative_measured_bath_energy"]) and bool(
        record["final"]["nonnegative_measured_bath_energy"]
    )
    record["paired_result"] = {
        "time_step_s": min(args.time_steps),
        "raw_bath_eV_per_update": raw_energy,
        "final_bath_eV_per_update": final_energy,
        "energy_reduction_percent": percent_reduction(raw_energy, final_energy),
        "raw_bath_power_W": raw_power,
        "final_bath_power_W": final_power,
        "power_reduction_percent": percent_reduction(raw_power, final_power),
        "both_layouts_converged": both_converged,
        "nonnegative_measured_bath_energy": numerically_valid,
    }
    if not numerically_valid:
        record["status"] = "completed_invalid_negative_bath_energy"
    else:
        record["status"] = "completed_converged" if both_converged else "completed_unconverged"
    return record


def summary_row(record: Mapping[str, Any]) -> dict[str, Any]:
    raw = record.get("raw", {})
    final = record.get("final", {})
    raw_metrics = raw.get("layout_metrics", {})
    final_metrics = final.get("layout_metrics", {})
    paired = record.get("paired_result", {})
    raw_area = raw_metrics.get("bbox_area")
    final_area = final_metrics.get("bbox_area")
    return {
        "design": record.get("design"),
        "status": record.get("status"),
        "ii": record.get("initiation_interval"),
        "acceptance_time_s": record.get("acceptance_time_s"),
        "raw_bbox_area": raw_area,
        "final_bbox_area": final_area,
        "area_reduction_percent": (
            percent_reduction(float(raw_area), float(final_area))
            if isinstance(raw_area, (int, float)) and isinstance(final_area, (int, float))
            else None
        ),
        "raw_route_steps": raw_metrics.get("route_steps"),
        "final_route_steps": final_metrics.get("route_steps"),
        "raw_pnr_qca_cells": raw_metrics.get("mapped_qca_cells"),
        "final_pnr_qca_cells": final_metrics.get("mapped_qca_cells"),
        "raw_exported_qca_cells": raw.get("qca_cells"),
        "final_exported_qca_cells": final.get("qca_cells"),
        "raw_crossovers": raw_metrics.get("crossover_segments"),
        "final_crossovers": final_metrics.get("crossover_segments"),
        "finest_time_step_s": paired.get("time_step_s"),
        "raw_bath_eV_per_update": paired.get("raw_bath_eV_per_update"),
        "final_bath_eV_per_update": paired.get("final_bath_eV_per_update"),
        "energy_reduction_percent": paired.get("energy_reduction_percent"),
        "raw_bath_power_W": paired.get("raw_bath_power_W"),
        "final_bath_power_W": paired.get("final_bath_power_W"),
        "power_reduction_percent": paired.get("power_reduction_percent"),
        "raw_relative_change_last_two": raw.get("convergence", {}).get(
            "relative_change_last_two"
        ),
        "final_relative_change_last_two": final.get("convergence", {}).get(
            "relative_change_last_two"
        ),
        "both_layouts_converged": paired.get("both_layouts_converged"),
        "nonnegative_measured_bath_energy": paired.get(
            "nonnegative_measured_bath_energy"
        ),
    }


def write_summary_csv(path: Path, records: Sequence[Mapping[str, Any]]) -> None:
    rows = [summary_row(record) for record in records]
    fieldnames = list(rows[0]) if rows else ["design", "status"]
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def machine_metadata() -> dict[str, Any]:
    return {
        "platform": platform.platform(),
        "python": platform.python_version(),
    }


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    args.raw_directory = args.raw_directory.resolve()
    args.final_directory = args.final_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.energy_executable = args.energy_executable.resolve()
    if not args.validate_only:
        require_file(args.energy_executable)
    workloads = load_workloads(args.workload_manifest)
    args.output_directory.mkdir(parents=True, exist_ok=True)

    records: list[dict[str, Any]] = []
    for design in args.designs:
        try:
            if design not in workloads:
                raise ValueError(f"no workload for {design}")
            pair = validate_pair(
                args.raw_directory, args.final_directory, design, args.variant
            )
            record = make_design_record(pair, workloads[design], args.output_directory, args)
        except Exception as error:  # preserve completed designs for a long numerical sweep
            record = {"design": design, "status": "error", "error": str(error)}
            records.append(record)
            (args.output_directory / f"{design}.result.json").write_text(
                json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            if args.fail_fast:
                break
            continue
        records.append(record)
        (args.output_directory / f"{design}.result.json").write_text(
            json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    summary = {
        "schema": "ifcn.sequential-paired-energy.v1",
        "generated_unix_time": time.time(),
        "raw_directory": str(args.raw_directory),
        "final_directory": str(args.final_directory),
        "variant": args.variant,
        "energy_executable": str(args.energy_executable),
        "time_steps_s": args.time_steps,
        "clock_period_s": args.clock_period,
        "clock_slope_s": args.clock_slope,
        "convergence_threshold": args.convergence_threshold,
        "window": {
            "startup_updates": STARTUP_UPDATES,
            "warmup_updates": WARMUP_UPDATES,
            "measured_updates": MEASURED_UPDATES,
            "drain_updates": DRAIN_UPDATES,
            "energy_quantity": "PER_CYCLE.E_bath_eV",
            "partial_tail_cycle_excluded": True,
        },
        "claim_boundary": {
            "scope": "paired_numerical_energy_comparison",
            "physical_state_signoff": False,
            "power_definition": "mean measured E_bath per accepted update / T_accept",
            "acceptance_time": "(II / 4) * clock_period",
        },
        "machine": machine_metadata(),
        "records": records,
    }
    (args.output_directory / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_summary_csv(args.output_directory / "summary.csv", records)
    errors = sum(record.get("status") == "error" for record in records)
    unconverged = sum(record.get("status") == "completed_unconverged" for record in records)
    invalid = sum(
        record.get("status") == "completed_invalid_negative_bath_energy"
        for record in records
    )
    print(
        f"records={len(records)} errors={errors} invalid={invalid} "
        f"unconverged={unconverged} "
        f"output={args.output_directory}"
    )
    if errors or invalid or (args.require_converged and unconverged):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
