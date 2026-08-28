#!/usr/bin/env python3
"""Merge and qualify paired sequential-energy time-step sweeps.

The paired runner is often invoked once for the three affordable coarse time
steps and again for one or more finer time steps.  This script merges those
``summary.json`` files by design and layout label (``raw``/``final``), selects
the finest common four-level power-of-two time-step chain, and applies the
first-order Richardson extrapolation

    X_i = 2 E(h_{i+1}) - E(h_i).

A result is numerically accepted only when both layouts have non-negative bath energies,
successive finite-step differences retain one sign, both local observed
orders are in [0.8, 1.2], and the last two extrapolates differ by less than
one percent.  The reported numerical error is deliberately conservative: the
larger of the finest Richardson correction and the change between the last
two extrapolates.

Inputs may be individual paired-runner ``summary.json`` files or directories;
directories are searched recursively.  No simulator is run by this script.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
from pathlib import Path
from typing import Any, Mapping, Sequence


ELECTRON_VOLT_J = 1.602176634e-19
EXPECTED_SCHEMA = "ifcn.sequential-paired-energy.v1"
OUTPUT_SCHEMA = "ifcn.sequential-paired-energy-richardson.v1"
LABELS = ("raw", "final")
REQUIRED_LEVELS = 4
DEFAULT_RELATIVE_THRESHOLD = 0.01
DEFAULT_P_MIN = 0.8
DEFAULT_P_MAX = 1.2


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "summaries",
        nargs="+",
        type=Path,
        help="paired-runner summary.json files or directories containing them",
    )
    parser.add_argument(
        "--output-directory",
        "--output-dir",
        dest="output_directory",
        type=Path,
        required=True,
    )
    parser.add_argument(
        "--relative-threshold",
        type=float,
        default=DEFAULT_RELATIVE_THRESHOLD,
        help="maximum relative difference between the last two extrapolates",
    )
    parser.add_argument("--p-min", type=float, default=DEFAULT_P_MIN)
    parser.add_argument("--p-max", type=float, default=DEFAULT_P_MAX)
    parser.add_argument(
        "--allow-rejected",
        action="store_true",
        help="return success even when one or more designs fail qualification",
    )
    args = parser.parse_args(argv)
    if not 0 < args.relative_threshold < 1:
        parser.error("--relative-threshold must be between zero and one")
    if not 0 < args.p_min <= args.p_max:
        parser.error("expected 0 < --p-min <= --p-max")
    return args


def load_json_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"expected a JSON object: {path}")
    return value


def discover_summaries(inputs: Sequence[Path], output_directory: Path | None = None) -> list[Path]:
    discovered: set[Path] = set()
    output_resolved = output_directory.resolve() if output_directory else None
    for item in inputs:
        path = item.resolve()
        if path.is_file():
            discovered.add(path)
            continue
        if path.is_dir():
            before = len(discovered)
            for candidate in path.rglob("summary.json"):
                resolved = candidate.resolve()
                if output_resolved is not None and output_resolved in resolved.parents:
                    continue
                discovered.add(resolved)
            if len(discovered) == before:
                raise ValueError(f"no summary.json found under input directory: {path}")
            continue
        raise FileNotFoundError(path)
    if not discovered:
        raise ValueError("no summary.json inputs were found")
    return sorted(discovered)


def _finite_number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise ValueError(f"{field} must be finite")
    return result


def _same_number(left: float, right: float) -> bool:
    return math.isclose(left, right, rel_tol=1.0e-12, abs_tol=1.0e-30)


def _merge_scalar(context: dict[str, Any], key: str, value: Any, design: str) -> None:
    if value is None:
        return
    if key not in context:
        context[key] = value
        return
    old = context[key]
    if isinstance(old, (int, float)) and not isinstance(old, bool) and isinstance(value, (int, float)) and not isinstance(value, bool):
        equal = _same_number(float(old), float(value))
    else:
        equal = old == value
    if not equal:
        raise ValueError(f"{design}: incompatible {key}: {old!r} versus {value!r}")


def merge_summaries(paths: Sequence[Path]) -> dict[str, dict[str, Any]]:
    """Merge runner summaries into ``design -> context/labels`` records."""

    merged: dict[str, dict[str, Any]] = {}
    for path in paths:
        summary = load_json_object(path)
        if summary.get("schema") != EXPECTED_SCHEMA:
            raise ValueError(
                f"unsupported schema in {path}: {summary.get('schema')!r}; "
                f"expected {EXPECTED_SCHEMA!r}"
            )
        records = summary.get("records")
        if not isinstance(records, list):
            raise ValueError(f"records must be an array: {path}")
        for record in records:
            if not isinstance(record, dict) or not isinstance(record.get("design"), str):
                raise ValueError(f"invalid design record in {path}")
            design = record["design"]
            target = merged.setdefault(
                design,
                {
                    "design": design,
                    "context": {},
                    "labels": {label: {} for label in LABELS},
                    "layout_metrics": {},
                    "sources": set(),
                    "runner_statuses": set(),
                },
            )
            target["sources"].add(str(path))
            if isinstance(record.get("status"), str):
                target["runner_statuses"].add(record["status"])
            context = target["context"]
            _merge_scalar(context, "initiation_interval", record.get("initiation_interval"), design)
            _merge_scalar(context, "acceptance_time_s", record.get("acceptance_time_s"), design)
            _merge_scalar(context, "clock_period_s", summary.get("clock_period_s"), design)
            pair_validation = record.get("pair_validation", {})
            if isinstance(pair_validation, dict):
                _merge_scalar(context, "cut_v_sha256", pair_validation.get("cut_v_sha256"), design)
                _merge_scalar(context, "state_json_sha256", pair_validation.get("state_json_sha256"), design)

            for label in LABELS:
                layout = record.get(label)
                if not isinstance(layout, dict):
                    continue
                metrics = layout.get("layout_metrics")
                if isinstance(metrics, dict):
                    normalized_metrics = dict(metrics)
                    exported_cells = layout.get("qca_cells")
                    if isinstance(exported_cells, (int, float)) and not isinstance(exported_cells, bool):
                        normalized_metrics["exported_qca_cells"] = exported_cells
                    layer_aware_cells = normalized_metrics.get("exported_qca_cells")
                    if layer_aware_cells is None:
                        layer_aware_cells = normalized_metrics.get("mapped_layer_cell_records")
                    if layer_aware_cells is None:
                        layer_aware_cells = normalized_metrics.get("mapped_qca_cells")
                    normalized_metrics["layer_aware_cells"] = layer_aware_cells
                    old_metrics = target["layout_metrics"].setdefault(
                        label, normalized_metrics
                    )
                    if old_metrics != normalized_metrics:
                        raise ValueError(f"{design}/{label}: layout metrics changed between summaries")
                runs = layout.get("energy_runs")
                if not isinstance(runs, list):
                    continue
                for run in runs:
                    if not isinstance(run, dict):
                        raise ValueError(f"{design}/{label}: energy_runs entry must be an object")
                    dt = _finite_number(run.get("time_step_s"), "time_step_s")
                    energy = _finite_number(
                        run.get("mean_bath_eV_per_update"),
                        "mean_bath_eV_per_update",
                    )
                    if dt <= 0:
                        raise ValueError(f"{design}/{label}: time_step_s must be positive")
                    measurement = run.get("measurement", {})
                    per_update_nonnegative = True
                    if isinstance(measurement, dict):
                        flag = measurement.get("all_bath_values_nonnegative")
                        if flag is not None:
                            per_update_nonnegative = bool(flag)
                    sample = {
                        "time_step_s": dt,
                        "mean_bath_eV_per_update": energy,
                        "all_measured_updates_nonnegative": per_update_nonnegative,
                        "source": str(path),
                    }
                    previous = target["labels"][label].get(dt)
                    if previous is not None:
                        if not _same_number(
                            float(previous["mean_bath_eV_per_update"]), energy
                        ):
                            raise ValueError(
                                f"{design}/{label}: conflicting energy at dt={dt:.12g}: "
                                f"{previous['mean_bath_eV_per_update']} versus {energy}"
                            )
                        previous["all_measured_updates_nonnegative"] = bool(
                            previous["all_measured_updates_nonnegative"]
                        ) and per_update_nonnegative
                    else:
                        target["labels"][label][dt] = sample
    return merged


def _find_value(values: Sequence[float], target: float) -> float | None:
    for value in values:
        if math.isclose(value, target, rel_tol=1.0e-9, abs_tol=1.0e-30):
            return value
    return None


def finest_common_halving_chain(
    raw: Mapping[float, Any], final: Mapping[float, Any]
) -> list[float] | None:
    common = sorted(set(raw).intersection(final), reverse=True)
    chains: list[list[float]] = []
    for coarse in common:
        chain = [coarse]
        current = coarse
        for _ in range(REQUIRED_LEVELS - 1):
            found = _find_value(common, current / 2.0)
            if found is None:
                break
            chain.append(found)
            current = found
        if len(chain) == REQUIRED_LEVELS:
            chains.append(chain)
    if not chains:
        return None
    return min(chains, key=lambda chain: chain[-1])


def relative_difference(left: float, right: float) -> float:
    return abs(left - right) / max(abs(right), 1.0e-30)


def analyze_series(
    samples: Sequence[Mapping[str, Any]],
    *,
    relative_threshold: float = DEFAULT_RELATIVE_THRESHOLD,
    p_min: float = DEFAULT_P_MIN,
    p_max: float = DEFAULT_P_MAX,
) -> dict[str, Any]:
    if len(samples) != REQUIRED_LEVELS:
        raise ValueError(f"Richardson analysis requires exactly {REQUIRED_LEVELS} samples")
    time_steps = [_finite_number(sample.get("time_step_s"), "time_step_s") for sample in samples]
    energies = [
        _finite_number(sample.get("mean_bath_eV_per_update"), "mean_bath_eV_per_update")
        for sample in samples
    ]
    power_of_two_steps = all(
        math.isclose(coarse / fine, 2.0, rel_tol=1.0e-9, abs_tol=1.0e-12)
        for coarse, fine in zip(time_steps, time_steps[1:])
    )
    differences = [fine - coarse for coarse, fine in zip(energies, energies[1:])]
    differences_nonzero = all(value != 0.0 for value in differences)
    same_sign = differences_nonzero and (
        all(value > 0 for value in differences) or all(value < 0 for value in differences)
    )
    local_orders: list[float | None] = []
    for coarse_difference, fine_difference in zip(differences, differences[1:]):
        if coarse_difference == 0 or fine_difference == 0 or coarse_difference * fine_difference <= 0:
            local_orders.append(None)
        else:
            local_orders.append(
                math.log2(abs(coarse_difference / fine_difference))
            )
    orders_in_range = all(
        value is not None and math.isfinite(value) and p_min <= value <= p_max
        for value in local_orders
    )
    extrapolates = [
        2.0 * fine - coarse for coarse, fine in zip(energies, energies[1:])
    ]
    estimate = extrapolates[-1]
    extrapolation_change = relative_difference(extrapolates[-2], estimate)
    nonnegative = (
        all(value >= 0 for value in energies)
        and estimate >= 0
        and all(bool(sample.get("all_measured_updates_nonnegative", True)) for sample in samples)
    )
    conservative_error = max(
        abs(estimate - energies[-1]),
        abs(estimate - extrapolates[-2]),
    )
    reasons: list[str] = []
    if not power_of_two_steps:
        reasons.append("time_steps_not_power_of_two")
    if not same_sign:
        reasons.append("finite_step_differences_change_sign_or_vanish")
    if not orders_in_range:
        reasons.append("local_order_outside_range")
    if extrapolation_change >= relative_threshold:
        reasons.append("last_two_extrapolates_not_within_threshold")
    if not nonnegative:
        reasons.append("negative_bath_energy")
    return {
        "time_steps_s": time_steps,
        "bath_eV_per_update": energies,
        "finite_step_differences_eV": differences,
        "differences_same_nonzero_sign": same_sign,
        "local_observed_orders": local_orders,
        "local_orders_in_range": orders_in_range,
        "richardson_extrapolates_eV_per_update": extrapolates,
        "richardson_estimate_eV_per_update": estimate,
        "last_two_extrapolates_relative_difference": extrapolation_change,
        "nonnegative_bath_energy": nonnegative,
        "conservative_error_eV_per_update": conservative_error,
        "conservative_relative_error": conservative_error / max(abs(estimate), 1.0e-30),
        "accepted": not reasons,
        "rejection_reasons": reasons,
    }


def power_pW(energy_eV: float, acceptance_time_s: float) -> float:
    if acceptance_time_s <= 0:
        raise ValueError("acceptance_time_s must be positive")
    return energy_eV * ELECTRON_VOLT_J / acceptance_time_s * 1.0e12


def percent_reduction(raw: float, final: float) -> float | None:
    if raw <= 0:
        return None
    return 100.0 * (raw - final) / raw


def reduction_interval(
    raw: float, raw_error: float, final: float, final_error: float
) -> tuple[float | None, float | None, float | None]:
    """Return central, minimum, and maximum conservative reduction percentages."""

    central = percent_reduction(raw, final)
    raw_low = raw - raw_error
    if central is None or raw_low <= 0:
        return central, None, None
    minimum = 100.0 * (1.0 - (final + final_error) / raw_low)
    maximum = 100.0 * (
        1.0 - max(0.0, final - final_error) / (raw + raw_error)
    )
    return central, minimum, maximum


def analyze_design(
    merged: Mapping[str, Any],
    *,
    relative_threshold: float = DEFAULT_RELATIVE_THRESHOLD,
    p_min: float = DEFAULT_P_MIN,
    p_max: float = DEFAULT_P_MAX,
) -> dict[str, Any]:
    design = str(merged["design"])
    context = dict(merged.get("context", {}))
    raw_map = merged["labels"]["raw"]
    final_map = merged["labels"]["final"]
    chain = finest_common_halving_chain(raw_map, final_map)
    record: dict[str, Any] = {
        "design": design,
        "status": "insufficient_four_level_common_chain",
        "sources": sorted(merged.get("sources", [])),
        "runner_statuses": sorted(merged.get("runner_statuses", [])),
        "layout_metrics": dict(merged.get("layout_metrics", {})),
        **context,
    }
    if chain is None:
        excluded_statuses = [
            status
            for status in record["runner_statuses"]
            if status.startswith("excluded_")
        ]
        if excluded_statuses:
            # Keep deliberate runner exclusions distinct from incomplete or
            # failed numerical sweeps in every downstream table.
            record["status"] = sorted(excluded_statuses)[0]
        record["available_time_steps_s"] = {
            label: sorted(merged["labels"][label], reverse=True) for label in LABELS
        }
        return record
    record["selected_time_steps_s"] = chain
    for label, source in (("raw", raw_map), ("final", final_map)):
        samples = [source[time_step] for time_step in chain]
        result = analyze_series(
            samples,
            relative_threshold=relative_threshold,
            p_min=p_min,
            p_max=p_max,
        )
        acceptance_time = context.get("acceptance_time_s")
        if not isinstance(acceptance_time, (int, float)) or acceptance_time <= 0:
            result["accepted"] = False
            result["rejection_reasons"].append("missing_or_invalid_acceptance_time")
        else:
            result["bath_power_pW"] = power_pW(
                result["richardson_estimate_eV_per_update"], float(acceptance_time)
            )
            result["conservative_power_error_pW"] = power_pW(
                result["conservative_error_eV_per_update"], float(acceptance_time)
            )
        record[label] = result
    both_accepted = bool(record["raw"]["accepted"] and record["final"]["accepted"])
    record["status"] = (
        "numerically_accepted" if both_accepted else "rejected_numerical_qualification"
    )
    raw_energy = float(record["raw"]["richardson_estimate_eV_per_update"])
    final_energy = float(record["final"]["richardson_estimate_eV_per_update"])
    raw_error = float(record["raw"]["conservative_error_eV_per_update"])
    final_error = float(record["final"]["conservative_error_eV_per_update"])
    central, lower, upper = reduction_interval(
        raw_energy, raw_error, final_energy, final_error
    )
    record["paired_result"] = {
        "accepted": both_accepted,
        # Preserve raw arithmetic for diagnosis, but do not publish it as an
        # accepted reduction when either layout failed numerical qualification.
        "diagnostic_energy_reduction_percent": central,
        "energy_reduction_percent": central if both_accepted else None,
        "conservative_energy_reduction_lower_percent": lower if both_accepted else None,
        "conservative_energy_reduction_upper_percent": upper if both_accepted else None,
        "conservative_energy_reduction_error_percent": (
            max(abs(central - lower), abs(upper - central))
            if both_accepted
            and central is not None
            and lower is not None
            and upper is not None
            else None
        ),
        "power_reduction_percent": central if both_accepted else None,
    }
    return record


def summary_row(record: Mapping[str, Any]) -> dict[str, Any]:
    row: dict[str, Any] = {
        "design": record.get("design"),
        "status": record.get("status"),
        "ii": record.get("initiation_interval"),
        "acceptance_time_s": record.get("acceptance_time_s"),
        "dt_coarsest_s": None,
        "dt_finest_s": None,
    }
    metrics_by_label = record.get("layout_metrics", {})
    for label in LABELS:
        metrics = metrics_by_label.get(label, {}) if isinstance(metrics_by_label, dict) else {}
        row[f"{label}_bbox_area"] = metrics.get("bbox_area")
        row[f"{label}_route_steps"] = metrics.get("route_steps")
        row[f"{label}_layer_aware_cells"] = metrics.get("layer_aware_cells")
        row[f"{label}_crossovers"] = metrics.get("crossover_segments")
    selected = record.get("selected_time_steps_s")
    if isinstance(selected, list) and selected:
        row["dt_coarsest_s"] = selected[0]
        row["dt_finest_s"] = selected[-1]
    for label in LABELS:
        result = record.get(label, {})
        row[f"{label}_richardson_bath_eV_per_update"] = result.get(
            "richardson_estimate_eV_per_update"
        )
        row[f"{label}_conservative_error_eV_per_update"] = result.get(
            "conservative_error_eV_per_update"
        )
        row[f"{label}_bath_power_pW"] = result.get("bath_power_pW")
        row[f"{label}_conservative_power_error_pW"] = result.get(
            "conservative_power_error_pW"
        )
        orders = result.get("local_observed_orders")
        row[f"{label}_p_coarse"] = orders[0] if isinstance(orders, list) and len(orders) > 0 else None
        row[f"{label}_p_fine"] = orders[1] if isinstance(orders, list) and len(orders) > 1 else None
        row[f"{label}_last_extrap_relative_difference"] = result.get(
            "last_two_extrapolates_relative_difference"
        )
        row[f"{label}_accepted"] = result.get("accepted")
    paired = record.get("paired_result", {})
    for key in (
        "energy_reduction_percent",
        "conservative_energy_reduction_lower_percent",
        "conservative_energy_reduction_upper_percent",
        "conservative_energy_reduction_error_percent",
        "power_reduction_percent",
    ):
        row[key] = paired.get(key)
    return row


def write_csv(path: Path, records: Sequence[Mapping[str, Any]]) -> None:
    rows = [summary_row(record) for record in records]
    fieldnames = list(rows[0]) if rows else ["design", "status"]
    with path.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def latex_escape(value: Any) -> str:
    text = str(value)
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
    }
    return "".join(replacements.get(character, character) for character in text)


def latex_number(value: Any, digits: int = 4) -> str:
    if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(float(value)):
        return "--"
    number = float(value)
    if number == 0:
        return "0"
    exponent = math.floor(math.log10(abs(number)))
    if exponent < -3 or exponent >= 4:
        mantissa = number / (10.0**exponent)
        return rf"${mantissa:.{digits}g}\times10^{{{exponent}}}$"
    return f"{number:.{digits}g}"


def value_with_error(value: Any, error: Any) -> str:
    if not isinstance(value, (int, float)) or not isinstance(error, (int, float)):
        return "--"
    return f"{latex_number(value)} $\\pm$ {latex_number(error)}"


def metric_pair(record: Mapping[str, Any], key: str) -> str:
    metrics = record.get("layout_metrics", {})
    if not isinstance(metrics, dict):
        return "--"
    raw = metrics.get("raw", {})
    final = metrics.get("final", {})
    if not isinstance(raw, dict) or not isinstance(final, dict):
        return "--"
    raw_value = raw.get(key)
    final_value = final.get(key)
    if raw_value is None or final_value is None:
        return "--"
    return f"{latex_number(raw_value)}$\\to${latex_number(final_value)}"


def write_latex(path: Path, records: Sequence[Mapping[str, Any]]) -> None:
    lines = [
        "% Generated by scripts/summarize_sequential_paired_energy.py",
        "% Energy is E_bath per accepted update; uncertainty is the conservative numerical bound.",
        "% Numerical acceptance does not establish functional waveform equivalence or physical-state signoff.",
        r"\begin{tabular}{lrrrrrrrrrrl}",
        r"\hline",
        r"Design & II & Area R/F & Steps R/F & Cells R/F & XO R/F & Raw $E_{\rm bath}$ (eV) & Final $E_{\rm bath}$ (eV) & Raw power (pW) & Final power (pW) & Reduction (\%) & Status \\",
        r"\hline",
    ]
    for record in records:
        raw = record.get("raw", {})
        final = record.get("final", {})
        paired = record.get("paired_result", {})
        lines.append(
            " & ".join(
                (
                    latex_escape(record.get("design", "")),
                    latex_number(record.get("initiation_interval")),
                    metric_pair(record, "bbox_area"),
                    metric_pair(record, "route_steps"),
                    metric_pair(record, "layer_aware_cells"),
                    metric_pair(record, "crossover_segments"),
                    value_with_error(
                        raw.get("richardson_estimate_eV_per_update"),
                        raw.get("conservative_error_eV_per_update"),
                    ),
                    value_with_error(
                        final.get("richardson_estimate_eV_per_update"),
                        final.get("conservative_error_eV_per_update"),
                    ),
                    value_with_error(
                        raw.get("bath_power_pW"), raw.get("conservative_power_error_pW")
                    ),
                    value_with_error(
                        final.get("bath_power_pW"), final.get("conservative_power_error_pW")
                    ),
                    latex_number(paired.get("energy_reduction_percent")),
                    latex_escape(record.get("status", "")),
                )
            )
            + r" \\"
        )
    lines.extend((r"\hline", r"\end{tabular}"))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_summary(
    paths: Sequence[Path],
    *,
    relative_threshold: float = DEFAULT_RELATIVE_THRESHOLD,
    p_min: float = DEFAULT_P_MIN,
    p_max: float = DEFAULT_P_MAX,
) -> dict[str, Any]:
    merged = merge_summaries(paths)
    records = [
        analyze_design(
            merged[design],
            relative_threshold=relative_threshold,
            p_min=p_min,
            p_max=p_max,
        )
        for design in sorted(merged)
    ]
    return {
        "schema": OUTPUT_SCHEMA,
        "generated_unix_time": time.time(),
        "sources": [str(path) for path in paths],
        "criteria": {
            "required_common_time_step_levels": REQUIRED_LEVELS,
            "time_step_ratio": 2.0,
            "richardson_formula": "X_i = 2 * E(h_{i+1}) - E(h_i)",
            "relative_change_definition": "abs(X_last - X_previous) / abs(X_last)",
            "relative_threshold": relative_threshold,
            "threshold_comparison": "strictly_less_than",
            "local_order_definition": "log2(abs((E_i-E_{i+1})/(E_{i+1}-E_{i+2})))",
            "local_order_range_inclusive": [p_min, p_max],
            "finite_step_differences_same_nonzero_sign": True,
            "raw_and_final_bath_energy_nonnegative": True,
        },
        "uncertainty": {
            "definition": "max(abs(X_last-E_finest), abs(X_last-X_previous))",
            "scope": "conservative numerical time-step error, not workload variability",
        },
        "power": {
            "unit": "pW",
            "definition": "E_bath_eV_per_update * elementary_charge / acceptance_time_s",
            "elementary_charge_J_per_eV": ELECTRON_VOLT_J,
        },
        "claim_boundary": {
            "numerical_acceptance_only": True,
            "functional_waveform_equivalence_required_for_paired_power_claim": True,
            "physical_state_signoff": False,
        },
        "records": records,
        "accepted_designs": sum(
            record["status"] == "numerically_accepted" for record in records
        ),
        "excluded_designs": sum(record["status"].startswith("excluded_") for record in records),
        "rejected_designs": sum(
            record["status"] != "numerically_accepted"
            and not record["status"].startswith("excluded_")
            for record in records
        ),
    }


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    output_directory = args.output_directory.resolve()
    paths = discover_summaries(args.summaries, output_directory)
    summary = build_summary(
        paths,
        relative_threshold=args.relative_threshold,
        p_min=args.p_min,
        p_max=args.p_max,
    )
    output_directory.mkdir(parents=True, exist_ok=True)
    (output_directory / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_csv(output_directory / "summary.csv", summary["records"])
    write_latex(output_directory / "summary.tex", summary["records"])
    print(
        f"sources={len(paths)} records={len(summary['records'])} "
        f"accepted={summary['accepted_designs']} excluded={summary['excluded_designs']} "
        f"rejected={summary['rejected_designs']} "
        f"output={output_directory}"
    )
    if summary["rejected_designs"] and not args.allow_rejected:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
