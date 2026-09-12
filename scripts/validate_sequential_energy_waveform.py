#!/usr/bin/env python3
"""Validate sequential Simon probe waveforms without running a simulation.

The validator aligns each feedback probe with the absolute event epoch emitted
by the global clock solver.  For accepted update ``u`` and event epoch ``e``,
the two-phase switch/hold window is

    [u * II + e + 1, u * II + e + 3)

in four-phase clock slots.  Taking a fixed fraction of every input-vector
interval is incorrect when an output crosses an II boundary.

The four currently supported recurrence oracles are ``toggle_ff``,
``enable_hold_ff``, ``johnson2_sync``, and
``reconvergent_feedback_ff``.  This script only reads its inputs and an
adjacent ``clock_solution.tsv``; it never invokes Simon or another simulator.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence


SCHEMA = "ifcn.sequential-waveform-validation.v1"
PHASE_COUNT = 4
DEFAULT_LOGIC_THRESHOLD = 0.1
SUPPORTED_MODULES = {
    "toggle_ff",
    "enable_hold_ff",
    "johnson2_sync",
    "reconvergent_feedback_ff",
}


def load_json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object in {path}")
    return value


def parse_stimulus(path: Path) -> tuple[list[str], list[dict[str, int]]]:
    try:
        with path.open("r", encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source)
            if not reader.fieldnames or any(not name for name in reader.fieldnames):
                raise ValueError("stimulus has no non-empty header")
            if len(set(reader.fieldnames)) != len(reader.fieldnames):
                raise ValueError("stimulus header contains duplicate events")
            rows: list[dict[str, int]] = []
            for index, row in enumerate(reader):
                parsed: dict[str, int] = {}
                for event in reader.fieldnames:
                    text = row.get(event)
                    if text not in ("0", "1"):
                        raise ValueError(
                            f"stimulus row {index} event {event!r} is not 0 or 1"
                        )
                    parsed[event] = int(text)
                rows.append(parsed)
    except OSError as error:
        raise ValueError(f"cannot read stimulus {path}: {error}") from error
    if not rows:
        raise ValueError("stimulus contains no vectors")
    return list(reader.fieldnames), rows


def parse_rst(path: Path) -> tuple[int, dict[str, list[float]]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValueError(f"cannot read waveform {path}: {error}") from error

    declared_samples: int | None = None
    traces: dict[str, list[float]] = {}
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("number_samples="):
            declared_samples = int(line.split("=", 1)[1])
        if line != "[TRACE]":
            index += 1
            continue

        label: str | None = None
        values: list[float] | None = None
        cursor = index + 1
        while cursor < len(lines) and lines[cursor] != "[#TRACE]":
            if lines[cursor].startswith("data_labels="):
                label = lines[cursor].split("=", 1)[1]
            elif lines[cursor] == "[TRACE_DATA]":
                cursor += 1
                tokens: list[str] = []
                while cursor < len(lines) and lines[cursor] != "[#TRACE_DATA]":
                    tokens.extend(lines[cursor].split())
                    cursor += 1
                try:
                    values = [float(token) for token in tokens]
                except ValueError as error:
                    raise ValueError(f"non-numeric trace data in {path}") from error
            cursor += 1

        if label is None or values is None:
            raise ValueError(f"incomplete TRACE block in {path}")
        if label in traces:
            raise ValueError(f"duplicate trace label {label!r}")
        if not values or any(not math.isfinite(value) for value in values):
            raise ValueError(f"trace {label!r} is empty or non-finite")
        traces[label] = values
        index = cursor + 1

    if declared_samples is None or declared_samples <= 0:
        raise ValueError(f"waveform {path} has no valid number_samples")
    for label, values in traces.items():
        if len(values) != declared_samples:
            raise ValueError(
                f"trace {label!r} has {len(values)} samples, expected {declared_samples}"
            )
    for phase in range(PHASE_COUNT):
        if f"Clock {phase}" not in traces:
            raise ValueError(f"waveform is missing Clock {phase}")
    return declared_samples, traces


def parse_clock_solution(path: Path) -> tuple[int, int, dict[str, int]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ValueError(f"cannot read clock solution {path}: {error}") from error
    if not lines or lines[0] != "ifcn.global-clock-solution.v1":
        raise ValueError(f"unexpected clock solution schema in {path}")

    phase_count: int | None = None
    initiation_interval: int | None = None
    status: str | None = None
    events: dict[str, int] = {}
    for line in lines[1:]:
        fields = line.split("\t")
        if fields[0] == "status" and len(fields) == 2:
            status = fields[1]
        elif fields[0] == "phase_count" and len(fields) == 2:
            phase_count = int(fields[1])
        elif fields[0] == "ii" and len(fields) == 2:
            initiation_interval = int(fields[1])
        elif fields[0] == "event" and len(fields) == 3:
            if fields[1] in events:
                raise ValueError(f"duplicate clock event {fields[1]!r}")
            events[fields[1]] = int(fields[2])
    if status != "SAT":
        raise ValueError(f"clock solution status is {status!r}, expected 'SAT'")
    if phase_count != PHASE_COUNT:
        raise ValueError(f"expected {PHASE_COUNT} clock phases, found {phase_count}")
    if initiation_interval is None or initiation_interval <= 0:
        raise ValueError("clock solution has no positive II")
    if initiation_interval % PHASE_COUNT:
        raise ValueError("clock solution II must be a multiple of four phases")
    if any(epoch < 0 for epoch in events.values()):
        raise ValueError("clock solution event epochs must be nonnegative")
    return phase_count, initiation_interval, events


def event_epochs(
    report: Mapping[str, Any], report_path: Path, clock_solution: Path | None
) -> tuple[int, dict[str, int], Path | None]:
    initiation_interval = report.get("initiation_interval")
    if not isinstance(initiation_interval, int) or initiation_interval <= 0:
        raise ValueError("layout report has no positive integer initiation_interval")
    if initiation_interval % PHASE_COUNT:
        raise ValueError("layout report II must be a multiple of four phases")

    embedded = report.get("event_epochs")
    if isinstance(embedded, dict) and embedded:
        epochs: dict[str, int] = {}
        for event, epoch in embedded.items():
            if not isinstance(event, str) or not isinstance(epoch, int) or epoch < 0:
                raise ValueError("layout report event_epochs must map names to nonnegative integers")
            epochs[event] = epoch
        return initiation_interval, epochs, None

    solution_path = clock_solution or report_path.with_name("clock_solution.tsv")
    _, solution_ii, epochs = parse_clock_solution(solution_path)
    if solution_ii != initiation_interval:
        raise ValueError(
            f"II mismatch: report={initiation_interval}, clock solution={solution_ii}"
        )
    return initiation_interval, epochs, solution_path


def event_rows_to_ports(
    state: Mapping[str, Any], header: Sequence[str], rows: Sequence[Mapping[str, int]]
) -> list[dict[str, int]]:
    cut_dag = state.get("cut_dag")
    if not isinstance(cut_dag, dict):
        raise ValueError("state manifest has no cut_dag")
    primary_inputs = cut_dag.get("primary_inputs")
    if not isinstance(primary_inputs, list):
        raise ValueError("state manifest has no primary_inputs")

    event_to_port: dict[str, str] = {}
    for item in primary_inputs:
        if not isinstance(item, dict) or "event" not in item or "port" not in item:
            raise ValueError("malformed primary-input entry in state manifest")
        event, port = str(item["event"]), str(item["port"])
        if event in event_to_port:
            raise ValueError(f"duplicate primary-input event {event!r}")
        event_to_port[event] = port
    if set(header) != set(event_to_port):
        raise ValueError(
            f"stimulus events {sorted(header)} do not match state events "
            f"{sorted(event_to_port)}"
        )
    if len(set(event_to_port.values())) != len(event_to_port):
        raise ValueError("state manifest maps multiple events to one port")
    return [
        {event_to_port[event]: int(row[event]) for event in header}
        for row in rows
    ]


Tri = int | None


def tri_not(value: Tri) -> Tri:
    return None if value is None else 1 - value


def tri_and(*values: Tri) -> Tri:
    if 0 in values:
        return 0
    if all(value == 1 for value in values):
        return 1
    return None


def tri_or(*values: Tri) -> Tri:
    if 1 in values:
        return 1
    if all(value == 0 for value in values):
        return 0
    return None


def expected_probe_logic(
    module: str, rows: Sequence[Mapping[str, int]]
) -> dict[str, list[Tri]]:
    if module not in SUPPORTED_MODULES:
        raise ValueError(
            f"unsupported source_module {module!r}; supported: "
            f"{', '.join(sorted(SUPPORTED_MODULES))}"
        )

    if module == "toggle_ff":
        q: Tri = None
        output: list[Tri] = []
        for row in rows:
            d = 0 if row["rst"] else tri_not(q)
            output.append(d)
            q = d
        return {"d0": output}

    if module == "enable_hold_ff":
        q = None
        output = []
        for row in rows:
            d = 0 if row["rst"] else row["d"] if row["en"] else q
            output.append(d)
            q = d
        return {"d0": output}

    if module == "johnson2_sync":
        q0: Tri = None
        q1: Tri = None
        d0_values: list[Tri] = []
        d1_values: list[Tri] = []
        for row in rows:
            if row["rst"]:
                d0, d1 = 0, 0
            else:
                d0, d1 = tri_not(q1), q0
            d0_values.append(d0)
            d1_values.append(d1)
            q0, q1 = d0, d1
        return {"d0": d0_values, "d1": d1_values}

    q = None
    output = []
    for row in rows:
        a, b, reset = row["a"], row["b"], row["rst"]
        left = tri_or(tri_not(a), tri_not(q), b)
        right = tri_or(a, tri_and(q, tri_not(b)))
        d = tri_and(left, tri_not(reset), right)
        output.append(d)
        q = d
    return {"d0": output}


def state_data_events(state: Mapping[str, Any]) -> set[str]:
    boundaries = state.get("state_boundaries")
    if not isinstance(boundaries, list):
        raise ValueError("state manifest has no state_boundaries")
    events = {
        str(item["data_event"])
        for item in boundaries
        if isinstance(item, dict) and "data_event" in item
    }
    if len(events) != len(boundaries):
        raise ValueError("state boundaries have missing or duplicate data_event values")
    return events


def stable_window_median(
    samples: Sequence[float], start_slot: int, end_slot: int, total_slots: int
) -> tuple[float, list[int]]:
    begin = int(start_slot * len(samples) / total_slots)
    end = max(begin + 1, int(end_slot * len(samples) / total_slots))
    end = min(end, len(samples))
    if begin >= len(samples) or begin >= end:
        raise ValueError("stable window contains no waveform samples")
    return statistics.median(samples[begin:end]), [begin, end]


def validate_waveform(
    *,
    layout_report_path: Path,
    state_path: Path,
    stimulus_path: Path,
    waveform_path: Path,
    clock_solution_path: Path | None = None,
    logic_threshold: float = DEFAULT_LOGIC_THRESHOLD,
) -> dict[str, Any]:
    if not math.isfinite(logic_threshold) or not 0 < logic_threshold < 1:
        raise ValueError("logic threshold must be finite and between zero and one")
    report = load_json(layout_report_path)
    state = load_json(state_path)
    header, event_rows = parse_stimulus(stimulus_path)
    port_rows = event_rows_to_ports(state, header, event_rows)
    sample_count, traces = parse_rst(waveform_path)
    initiation_interval, epochs, solution_path = event_epochs(
        report, layout_report_path, clock_solution_path
    )

    module = state.get("source_module")
    if not isinstance(module, str):
        raise ValueError("state manifest has no source_module")
    expected = expected_probe_logic(module, port_rows)
    boundary_events = state_data_events(state)
    if set(expected) != boundary_events:
        raise ValueError(
            f"oracle events {sorted(expected)} do not match state boundaries "
            f"{sorted(boundary_events)}"
        )

    total_slots = len(event_rows) * initiation_interval
    per_probe: list[dict[str, Any]] = []
    compared = matching = weak = 0
    margins: list[float] = []
    for event in sorted(expected):
        if event not in epochs:
            raise ValueError(f"clock solution has no epoch for state event {event!r}")
        epoch = epochs[event]
        label = f"feedback_probe_{event}"
        if label not in traces:
            raise ValueError(f"waveform is missing required trace {label!r}")

        observed_updates: list[int] = []
        expected_values: list[Tri] = []
        observed_values: list[Tri] = []
        medians: list[float] = []
        sample_windows: list[list[int]] = []
        probe_matching = probe_compared = probe_weak = 0
        probe_margins: list[float] = []
        for update, wanted in enumerate(expected[event]):
            start_slot = update * initiation_interval + epoch + 1
            end_slot = update * initiation_interval + epoch + 3
            if end_slot > total_slots:
                continue
            median, sample_window = stable_window_median(
                traces[label], start_slot, end_slot, total_slots
            )
            observed = (
                1
                if median >= logic_threshold
                else 0
                if median <= -logic_threshold
                else None
            )
            observed_updates.append(update)
            expected_values.append(wanted)
            observed_values.append(observed)
            medians.append(median)
            sample_windows.append(sample_window)
            if wanted is None:
                continue
            probe_compared += 1
            probe_matching += int(observed == wanted)
            probe_weak += int(observed is None)
            probe_margins.append(abs(median))

        compared += probe_compared
        matching += probe_matching
        weak += probe_weak
        margins.extend(probe_margins)
        per_probe.append(
            {
                "event": event,
                "trace": label,
                "event_epoch": epoch,
                "clock_zone": epoch % PHASE_COUNT,
                "stable_window_epoch_offsets": [epoch + 1, epoch + 3],
                "observable_updates": observed_updates,
                "unobservable_tail_updates": len(event_rows) - len(observed_updates),
                "expected_logic": expected_values,
                "observed_logic": observed_values,
                "observed_medians": medians,
                "sample_windows": sample_windows,
                "compared": probe_compared,
                "matching": probe_matching,
                "agreement": probe_matching / probe_compared if probe_compared else None,
                "weak": probe_weak,
                "min_margin": min(probe_margins) if probe_margins else None,
            }
        )

    passed = compared > 0 and matching == compared and weak == 0
    return {
        "schema": SCHEMA,
        "scope": "diagnostic_waveform_only_uncharacterized_physical_state",
        "physical_state_signoff": False,
        "status": "diagnostic_pass" if passed else "diagnostic_fail",
        "source_module": module,
        "initiation_interval": initiation_interval,
        "phase_count": PHASE_COUNT,
        "logic_threshold": logic_threshold,
        "stable_window_rule": "[u*II+event_epoch+1,u*II+event_epoch+3)",
        "timing_assumption": (
            "waveform spans stimulus_updates*II four-phase slots, as emitted by "
            "benchmark_sequential_paired_energy.py"
        ),
        "stimulus_updates": len(event_rows),
        "waveform_samples": sample_count,
        "compared": compared,
        "matching": matching,
        "agreement": matching / compared if compared else None,
        "weak": weak,
        "min_margin": min(margins) if margins else None,
        "per_probe": per_probe,
        "inputs": {
            "layout_report": str(layout_report_path),
            "state": str(state_path),
            "stimulus": str(stimulus_path),
            "waveform": str(waveform_path),
            "clock_solution": str(solution_path) if solution_path else "embedded_event_epochs",
        },
    }


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout-report", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--stimulus", required=True, type=Path)
    parser.add_argument("--waveform", required=True, type=Path)
    parser.add_argument(
        "--clock-solution",
        type=Path,
        help="defaults to clock_solution.tsv beside --layout-report",
    )
    parser.add_argument("--logic-threshold", type=float, default=DEFAULT_LOGIC_THRESHOLD)
    parser.add_argument("--output", type=Path, help="write JSON here instead of stdout")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        result = validate_waveform(
            layout_report_path=args.layout_report,
            state_path=args.state,
            stimulus_path=args.stimulus,
            waveform_path=args.waveform,
            clock_solution_path=args.clock_solution,
            logic_threshold=args.logic_threshold,
        )
    except (ValueError, OSError) as error:
        print(f"waveform validation failed: {error}", file=sys.stderr)
        return 2
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(text, end="")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return 0 if result["status"] == "diagnostic_pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
