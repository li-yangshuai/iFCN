#!/usr/bin/env python3
"""Aggregate the sequential-QCA experiments into claim-safe paper tables.

The script intentionally uses only the Python standard library.  It keeps
incompatible comparison classes separate and treats the external fiction
results as optional because those native builds can finish after the internal
experiments.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


SCHEMA = "ifcn.sequential-master-summary.v2"

MASTER_COLUMNS = [
    "section",
    "comparison_class",
    "record_id",
    "benchmark",
    "variant",
    "status",
    "metric",
    "value",
    "unit",
    "claim_scope",
    "source",
]

REPORTED_ONLY_DEFAULTS = [
    {
        "reference": "Bhowmik et al. (2022)",
        "year": 2022,
        "doi": "10.1016/j.compeleceng.2021.107668",
        "method_scope": "manual sequential-QCA examples and placement method",
        "availability": "reported_only_plus_equation_topology_reconstruction",
        "numerical_use": "no_cross_host_ratio",
        "notes": "source layouts are not an executable same-machine baseline",
    },
    {
        "reference": "Deng et al. (2022)",
        "year": 2022,
        "doi": "10.1016/j.mejo.2022.105544",
        "method_scope": "manual majority-feedback latch under a general clocking scheme",
        "availability": "reported_only_plus_equation_topology_reconstruction",
        "numerical_use": "no_cross_host_ratio",
        "notes": "source coordinates are not copied into the automatic layouts",
    },
    {
        "reference": "Li et al. (2022)",
        "year": 2022,
        "doi": "10.1109/TCSI.2022.3197450",
        "method_scope": "genetic-algorithm and A*-based combinational QCA P&R",
        "availability": "reported_only",
        "numerical_use": "no_head_to_head_ratio",
        "notes": "different combinational task/network/library and no pinned executable run",
    },
    {
        "reference": "Zhang et al. (2024)",
        "year": 2024,
        "doi": "10.1016/j.nancom.2024.100495",
        "method_scope": "hierarchical and A*-based combinational QCA P&R",
        "availability": "reported_only",
        "numerical_use": "no_head_to_head_ratio",
        "notes": "different combinational task/network/library and no pinned executable run",
    },
]

DISPLAY_NAMES = {
    "bhowmik2022/d_latch_active_high_1b": "Bhowmik D latch (faithful)",
    "bhowmik2022/d_latch_sampled_state_equation_1b": "Bhowmik D latch (sampled)",
    "bhowmik2022/sr_nand_latch_active_low_1b": "Bhowmik NAND-SR (faithful)",
    "bhowmik2022/sr_nand_latch_sampled_topology_2b": "Bhowmik NAND-SR (sampled, 2b)",
    "bhowmik2022/sr_nand_latch_sampled_valid_domain_1b": "Bhowmik NAND-SR (valid domain)",
    "deng2022/sr_majority_latch_1b": "Deng majority-SR (faithful)",
    "deng2022/sr_majority_latch_sampled_1b": "Deng majority-SR (sampled)",
    "bhowmik2022_use_sequential/d_latch_sampled_state_equation_1b": "Bhowmik D latch",
    "bhowmik2022_use_sequential/sr_nand_latch_sampled_topology_2b": "Bhowmik NAND-SR (2b)",
    "bhowmik2022_use_sequential/sr_nand_latch_sampled_valid_domain_1b": "Bhowmik NAND-SR",
    "deng2022_general_clocking/sr_majority_latch_sampled_1b": "Deng majority-SR",
}


def display_name(value: Any) -> str:
    text = scalar(value)
    return DISPLAY_NAMES.get(text, text)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--artifacts-root", type=Path, default=Path("build/artifacts")
    )
    parser.add_argument("--clock-dir", type=Path)
    parser.add_argument("--rtl-dir", type=Path)
    parser.add_argument(
        "--z3-audit",
        type=Path,
        help="optional final Z3-backend audit JSON",
    )
    parser.add_argument("--paper-dir", type=Path)
    parser.add_argument("--physical-dir", type=Path)
    parser.add_argument("--energy-convergence-dir", type=Path)
    parser.add_argument("--simon-models-dir", type=Path)
    parser.add_argument(
        "--external-root",
        type=Path,
        help="optional fiction baseline root containing comparison_scope.json",
    )
    parser.add_argument("--external-clocking-dir", type=Path)
    parser.add_argument("--external-gold-dir", type=Path)
    parser.add_argument(
        "--reported-only-csv",
        type=Path,
        help="optional literature catalog; built-in claim-only rows are used otherwise",
    )
    parser.add_argument(
        "--allow-unvalidated",
        action="store_true",
        help="emit diagnostic output even when the clock oracle validation failed",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/artifacts/sequential_master_results_v2"),
    )
    return parser.parse_args(argv)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def scalar(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, float):
        if not math.isfinite(value):
            return ""
        return format(value, ".12g")
    return str(value)


def number(value: Any) -> int | float | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, (int, float)):
        return value if math.isfinite(float(value)) else None
    try:
        parsed = float(str(value))
    except ValueError:
        return None
    if not math.isfinite(parsed):
        return None
    return int(parsed) if parsed.is_integer() else parsed


def boolean(value: Any) -> bool | None:
    if value is None or value == "":
        return None
    if isinstance(value, bool):
        return value
    lowered = str(value).strip().lower()
    if lowered in {"1", "true", "yes", "pass"}:
        return True
    if lowered in {"0", "false", "no", "fail"}:
        return False
    return None


def percentile(values: Sequence[float], fraction: float) -> float | None:
    """Nearest-rank percentile, matching run_sequential_rtl_experiments.py."""
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def load_json(
    path: Path,
    label: str,
    inventory: list[dict[str, Any]],
    *,
    required: bool,
) -> dict[str, Any] | None:
    if not path.is_file():
        inventory.append(
            {"id": label, "path": str(path), "available": False, "required": required}
        )
        if required:
            raise FileNotFoundError(f"required input is missing: {path}")
        return None
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: expected a JSON object")
    inventory.append(
        {
            "id": label,
            "path": str(path),
            "available": True,
            "required": required,
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
            "schema": payload.get("schema", ""),
        }
    )
    return payload


def load_csv(
    path: Path,
    label: str,
    inventory: list[dict[str, Any]],
    *,
    required: bool,
) -> list[dict[str, str]] | None:
    if not path.is_file():
        inventory.append(
            {"id": label, "path": str(path), "available": False, "required": required}
        )
        if required:
            raise FileNotFoundError(f"required input is missing: {path}")
        return None
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    inventory.append(
        {
            "id": label,
            "path": str(path),
            "available": True,
            "required": required,
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
            "rows": len(rows),
        }
    )
    return rows


def add_metric(
    rows: list[dict[str, str]],
    *,
    section: str,
    comparison_class: str,
    record_id: str,
    metric: str,
    value: Any,
    unit: str = "",
    benchmark: str = "",
    variant: str = "",
    status: str = "",
    claim_scope: str,
    source: str,
) -> None:
    rows.append(
        {
            "section": section,
            "comparison_class": comparison_class,
            "record_id": record_id,
            "benchmark": benchmark,
            "variant": variant,
            "status": status,
            "metric": metric,
            "value": scalar(value),
            "unit": unit,
            "claim_scope": claim_scope,
            "source": source,
        }
    )


def summarize_correctness(
    raw: Mapping[str, Any],
    validated: Mapping[str, Any],
    master_rows: list[dict[str, str]],
    source: str,
) -> dict[str, Any]:
    exact = dict(validated.get("exact_vs_modulo") or raw.get("correctness") or {})
    ii = dict(validated.get("ii_ablation") or raw.get("ii_ablation") or {})
    validation = dict(validated.get("validation") or {})
    edge_scaling = list(raw.get("edge_scaling") or [])
    comparison_class = "internal_controlled_ablation"
    scope = "fixed-geometry phase/epoch/II closure with an independent oracle"

    exact_aliases = {
        "cases": exact.get("cases"),
        "exact_sat": exact.get("exact_sat"),
        "exact_unsat": exact.get("exact_unsat"),
        "modulo_sat": exact.get("modulo_sat"),
        "modulo_false_accept": exact.get(
            "false_accept", exact.get("modulo_false_accept")
        ),
        "modulo_false_reject": exact.get(
            "false_reject", exact.get("modulo_false_reject")
        ),
        "false_accept_rate_among_exact_unsat": exact.get(
            "false_accept_rate_among_exact_unsat"
        ),
        "solver_oracle_mismatches": exact.get(
            "mismatches", exact.get("solver_oracle_mismatches")
        ),
    }
    for metric, value in exact_aliases.items():
        add_metric(
            master_rows,
            section="correctness_ablation",
            comparison_class=comparison_class,
            record_id="exact_vs_modulo",
            metric=metric,
            value=value,
            unit="ratio" if metric.endswith("rate_among_exact_unsat") else "cases",
            status="validated" if validation.get("passed") is True else "",
            claim_scope=scope,
            source=source,
        )

    ii_metrics = {
        "cases": ii.get("cases"),
        "adaptive_candidates": "|".join(
            str(value) for value in ii.get("adaptive_candidates", raw.get("ii_ablation", {}).get("adaptive_candidates", []))
        ),
        "fixed_ii": ii.get("fixed_ii", raw.get("ii_ablation", {}).get("fixed_ii")),
        "adaptive_sat": ii.get("adaptive_sat"),
        "fixed_ii4_sat": ii.get("fixed_ii4_sat"),
        "adaptive_only_sat": ii.get("adaptive_only_sat"),
        "fixed_only_sat": ii.get("fixed_only_sat"),
        "sat_recovery_over_fixed": ii.get("sat_recovery_over_fixed"),
        "solver_oracle_mismatches": ii.get(
            "mismatches", ii.get("solver_oracle_mismatches")
        ),
    }
    if ii_metrics["sat_recovery_over_fixed"] is None:
        recovered = number(ii_metrics["adaptive_only_sat"])
        fixed = number(ii_metrics["fixed_ii4_sat"])
        if recovered is not None and fixed:
            ii_metrics["sat_recovery_over_fixed"] = recovered / fixed
    for metric, value in ii_metrics.items():
        add_metric(
            master_rows,
            section="correctness_ablation",
            comparison_class=comparison_class,
            record_id="adaptive_ii_vs_fixed_ii4",
            metric=metric,
            value=value,
            unit=(
                "ratio" if metric == "sat_recovery_over_fixed"
                else ("phase_ticks" if metric in {"adaptive_candidates", "fixed_ii"} else "cases")
            ),
            status="validated" if validation.get("passed") is True else "",
            claim_scope=scope,
            source=source,
        )

    for group in edge_scaling:
        edge = scalar(group.get("route_edges"))
        expected = scalar(group.get("expected_status"))
        record_id = f"edge_scaling_E{edge}_{expected}"
        for metric, unit in (
            ("ii", "phase_ticks"),
            ("state_latency", "phase_ticks"),
            ("dfs_budget", "nodes"),
            ("time_us_p50", "us"),
            ("time_us_p95", "us"),
            ("dfs_nodes_p50", "nodes"),
            ("sat_runs", "runs"),
            ("unsat_runs", "runs"),
            ("limit_runs", "runs"),
            ("invalid_runs", "runs"),
        ):
            add_metric(
                master_rows,
                section="correctness_ablation",
                comparison_class="internal_solver_scaling",
                record_id=record_id,
                metric=metric,
                value=group.get(metric),
                unit=unit,
                variant=expected,
                status=expected,
                claim_scope="synthetic recurrence scaling; not end-to-end P&R runtime",
                source=source,
            )

    strict_next_phase = exact.get("strict_next_phase") or {}
    for metric, value in strict_next_phase.items():
        add_metric(
            master_rows,
            section="correctness_ablation",
            comparison_class="constraint_level_recent_paper_context",
            record_id="strict_next_phase",
            metric=metric,
            value=value,
            unit="ratio" if "rate" in metric else "cases",
            status="validated" if validation.get("passed") is True else "",
            claim_scope="strict next-phase recurrence family; not an implementation-runtime reproduction",
            source=source,
        )

    recent_paper = dict(raw.get("recent_paper_model_comparison") or {})
    for metric, value in recent_paper.items():
        add_metric(
            master_rows,
            section="correctness_ablation",
            comparison_class="constraint_level_recent_paper_context",
            record_id="recent_paper_model_comparison",
            metric=metric,
            value=value,
            unit="ratio" if "rate" in metric else ("cases" if isinstance(value, int) else "text"),
            claim_scope="constraint-family context only; no cross-implementation runtime claim",
            source=source,
        )

    legacy_scaling = list(raw.get("scaling") or [])
    for group in legacy_scaling:
        record_id = f"legacy_state_scaling_S{group.get('states')}_{group.get('status')}"
        for metric, value in group.items():
            add_metric(
                master_rows,
                section="correctness_ablation",
                comparison_class="auxiliary_synthetic_state_scaling",
                record_id=record_id,
                variant=scalar(group.get("status")),
                status=scalar(group.get("status")),
                metric=metric,
                value=value,
                unit=("us" if metric.startswith("time_us") else "count"),
                claim_scope="legacy synthetic state-count scaling; not end-to-end RTL P&R",
                source=source,
            )

    physical_macros = list(raw.get("physical_macros") or [])
    for macro in physical_macros:
        record_id = f"structural_macro:{macro.get('name')}"
        for metric, value in macro.items():
            add_metric(
                master_rows,
                section="correctness_ablation",
                comparison_class="auxiliary_structural_macro_reference",
                record_id=record_id,
                benchmark=scalar(macro.get("name")),
                metric=metric,
                value=value,
                unit=("us" if "time_us" in metric else "count"),
                claim_scope="manually defined structural macro reference; not automatic cyclic P&R",
                source=source,
            )

    return {
        "comparison_class": comparison_class,
        "scope": scope,
        "validation": validation,
        "exact_vs_modulo": exact_aliases,
        "strict_next_phase": strict_next_phase,
        "recent_paper_model_comparison": recent_paper,
        "ii_ablation": ii_metrics,
        "edge_scaling": edge_scaling,
        "legacy_state_scaling": legacy_scaling,
        "physical_macro_structural_reference": physical_macros,
        "configuration": raw.get("configuration", {}),
    }


def summarize_rtl(
    payload: Mapping[str, Any],
    master_rows: list[dict[str, str]],
    source: str,
    warnings: list[str],
) -> dict[str, Any]:
    flattened: list[dict[str, Any]] = []
    transition_designs = 0
    transition_cases = 0
    transition_mismatches = 0
    transition_nonpass_designs = 0
    transition_zero_case_designs = 0
    source_designs = list(payload.get("records", []))
    for design in source_designs:
        check = design.get("transition_check") or {}
        if check:
            transition_designs += 1
            transition_cases += int(number(check.get("cases")) or 0)
            transition_mismatches += int(number(check.get("mismatches")) or 0)
            transition_nonpass_designs += check.get("status") != "pass"
            transition_zero_case_designs += int(number(check.get("cases")) or 0) <= 0
        for variant in design.get("variants", []):
            metrics = variant.get("metrics") or {}
            phase = metrics.get("phase_solver") or {}
            variant_name = str(variant.get("name") or "")
            if variant.get("status") == "success" and variant_name.startswith("cyclic"):
                invariant_failures: list[str] = []
                if metrics.get("directed_cycle_present") is not True:
                    invariant_failures.append("directed_cycle_present")
                if metrics.get("mapping_drc") is not True:
                    invariant_failures.append("mapping_drc")
                if not (number(metrics.get("feedback_routes")) or 0) > 0:
                    invariant_failures.append("feedback_routes")
                if not (number(metrics.get("initiation_interval")) or 0) > 0:
                    invariant_failures.append("initiation_interval")
                if not metrics.get("phase_backend"):
                    invariant_failures.append("phase_backend")
                if not variant.get("layout") or not variant.get("tex"):
                    invariant_failures.append("layout_or_tex")
                if invariant_failures:
                    raise ValueError(
                        f"RTL cyclic success invariant failed for "
                        f"{design.get('design')}:{variant_name}: "
                        + ", ".join(invariant_failures)
                    )
            record = {
                "design": design.get("design"),
                "state_bits": design.get("state_bits"),
                "comb_nodes": design.get("comb_nodes"),
                "transition_cases": check.get("cases"),
                "transition_status": check.get("status"),
                "transition_mismatches": check.get("mismatches"),
                "variant": variant_name,
                "status": variant.get("status"),
                "workflow_wall_seconds": variant.get("duration_seconds"),
                "ii": metrics.get("initiation_interval"),
                "nodes": metrics.get("nodes"),
                "routes": metrics.get("routes"),
                "feedback_routes": metrics.get("feedback_routes"),
                "directed_cycle_present": metrics.get("directed_cycle_present"),
                "mapping_drc": metrics.get("mapping_drc"),
                # The P&R report counts unique (x,y) positions and is not a
                # layer-aware QCADCell-instance count.
                "mapped_unique_xy_sites": metrics.get("mapped_qca_cells"),
                "bbox_area": metrics.get("bbox_area"),
                "routed_steps": metrics.get("routed_steps", metrics.get("route_steps")),
                "dfs_nodes": phase.get("dfs_nodes"),
                "conflicts": phase.get("conflicts"),
                "phase_backend": metrics.get("phase_backend"),
                "physical_state_signoff": metrics.get("physical_state_signoff"),
                "layout": variant.get("layout"),
                "tex": variant.get("tex"),
            }
            flattened.append(record)

    comparison_class = "same_machine_internal_pipeline_variants"
    claim_scope = (
        "RTL transition equivalence plus gate-level P&R; cyclic storage is not "
        "cell-level characterized"
    )
    metric_units = {
        "state_bits": "bits",
        "comb_nodes": "nodes",
        "transition_cases": "vectors",
        "transition_mismatches": "cases",
        "workflow_wall_seconds": "s",
        "ii": "phase_ticks",
        "nodes": "nodes",
        "routes": "routes",
        "feedback_routes": "routes",
        "mapped_unique_xy_sites": "xy_sites",
        "bbox_area": "grid_sites",
        "routed_steps": "steps",
        "dfs_nodes": "nodes",
        "conflicts": "conflicts",
        "directed_cycle_present": "boolean",
        "mapping_drc": "boolean",
        "phase_backend": "backend",
        "physical_state_signoff": "status",
    }
    for record in flattened:
        record_id = f"{record['design']}:{record['variant']}"
        add_metric(
            master_rows,
            section="rtl_auto_pnr",
            comparison_class=comparison_class,
            record_id=record_id,
            benchmark=scalar(record["design"]),
            variant=scalar(record["variant"]),
            status=scalar(record["status"]),
            metric="outcome",
            value=record["status"],
            claim_scope=claim_scope,
            source=source,
        )
        for metric, unit in metric_units.items():
            add_metric(
                master_rows,
                section="rtl_auto_pnr",
                comparison_class=comparison_class,
                record_id=record_id,
                benchmark=scalar(record["design"]),
                variant=scalar(record["variant"]),
                status=scalar(record["status"]),
                metric=metric,
                value=record.get(metric),
                unit=unit,
                claim_scope=claim_scope,
                source=source,
            )

    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in flattened:
        groups[scalar(record.get("variant"))].append(record)
    aggregates: list[dict[str, Any]] = []
    source_aggregate = {
        str(row.get("variant")): row for row in payload.get("aggregate", [])
    }
    for variant in sorted(groups):
        records = groups[variant]
        eligible = [
            row
            for row in records
            if row.get("status") != "unsupported_observation_only_state"
        ]
        successful = [row for row in records if row.get("status") == "success"]
        durations = [
            float(value)
            for row in records
            if (value := number(row.get("workflow_wall_seconds"))) is not None
        ]
        aggregate = {
            "variant": variant,
            "cases": len(records),
            "eligible_cases": len(eligible),
            "successes": len(successful),
            "success_rate_all_designs": len(successful) / len(records) if records else None,
            "success_rate_eligible": len(successful) / len(eligible) if eligible else None,
            "workflow_wall_seconds_median_all_outcomes": (
                statistics.median(durations) if durations else None
            ),
            "workflow_wall_seconds_p95_all_outcomes": percentile(durations, 0.95),
            "status_counts": {
                status: sum(row.get("status") == status for row in records)
                for status in sorted({scalar(row.get("status")) for row in records})
            },
        }
        aggregates.append(aggregate)
        published = source_aggregate.get(variant)
        if published and number(published.get("successes")) != aggregate["successes"]:
            warnings.append(f"RTL aggregate mismatch for {variant}: successes")
        for metric, unit in (
            ("cases", "designs"),
            ("eligible_cases", "designs"),
            ("successes", "designs"),
            ("success_rate_all_designs", "ratio"),
            ("success_rate_eligible", "ratio"),
            ("workflow_wall_seconds_median_all_outcomes", "s"),
            ("workflow_wall_seconds_p95_all_outcomes", "s"),
        ):
            add_metric(
                master_rows,
                section="rtl_auto_pnr",
                comparison_class=comparison_class,
                record_id=f"aggregate:{variant}",
                variant=variant,
                status="aggregate",
                metric=metric,
                value=aggregate[metric],
                unit=unit,
                claim_scope=claim_scope,
                source=source,
            )

    transition = {
        "total_designs": len(source_designs),
        "designs_checked": transition_designs,
        "unchecked_designs": len(source_designs) - transition_designs,
        "transition_vectors": transition_cases,
        "mismatches": transition_mismatches,
        "nonpass_designs": transition_nonpass_designs,
        "zero_case_designs": transition_zero_case_designs,
        "all_pass": (
            transition_designs > 0
            and transition_mismatches == 0
            and transition_nonpass_designs == 0
            and transition_zero_case_designs == 0
            and transition_designs == len(source_designs)
        ),
    }
    for metric, unit in (
        ("total_designs", "designs"),
        ("designs_checked", "designs"),
        ("unchecked_designs", "designs"),
        ("transition_vectors", "vectors"),
        ("mismatches", "cases"),
        ("nonpass_designs", "designs"),
        ("zero_case_designs", "designs"),
        ("all_pass", "boolean"),
    ):
        add_metric(
            master_rows,
            section="rtl_auto_pnr",
            comparison_class="rtl_transition_equivalence",
            record_id="transition_equivalence",
            status="pass" if transition["all_pass"] else "fail",
            metric=metric,
            value=transition[metric],
            unit=unit,
            claim_scope="exhaustive/supplied RTL state-transition checks before physical mapping",
            source=source,
        )

    return {
        "comparison_class": comparison_class,
        "scope": payload.get("scope", claim_scope),
        "transition_equivalence": transition,
        "variant_aggregates": aggregates,
        "records": flattened,
    }


def summarize_paper_cases(
    rows: Sequence[Mapping[str, Any]],
    master_rows: list[dict[str, str]],
    source: str,
) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    for row in rows:
        record = {
            "benchmark_id": row.get("benchmark_id"),
            "state_element": row.get("state_element"),
            "temporal_relation": row.get("temporal_relation"),
            "status": row.get("status"),
            "expectation_met": boolean(row.get("expectation_met")),
            "failed_stage": row.get("failed_stage"),
            "converter_status": row.get("converter_status"),
            "pnr_status": row.get("pnr_status"),
            "state_boundaries": number(row.get("state_boundaries")),
            "ii": number(row.get("initiation_interval")),
            "nodes": number(row.get("nodes")),
            "routes": number(row.get("routes")),
            "feedback_routes": number(row.get("feedback_routes")),
            "directed_cycle_present": boolean(row.get("directed_cycle_present")),
            "mapped_unique_xy_sites": number(row.get("mapped_qca_cells")),
            "pipeline_wall_seconds": number(row.get("duration_seconds")),
            "physical_state_signoff": row.get("physical_state_signoff"),
            "latex": row.get("latex"),
        }
        records.append(record)
        if record["status"] == "pass":
            invariant_failures: list[str] = []
            if record["temporal_relation"] != "sampled_state_only":
                invariant_failures.append("temporal_relation")
            if record["expectation_met"] is not True:
                invariant_failures.append("expectation_met")
            if record["converter_status"] != "success":
                invariant_failures.append("converter_status")
            if record["pnr_status"] != "success":
                invariant_failures.append("pnr_status")
            if record["directed_cycle_present"] is not True:
                invariant_failures.append("directed_cycle_present")
            if not (record["feedback_routes"] or 0) > 0:
                invariant_failures.append("feedback_routes")
            if not record["physical_state_signoff"]:
                invariant_failures.append("physical_state_signoff")
            if invariant_failures:
                raise ValueError(
                    f"paper pass invariant failed for {record['benchmark_id']}: "
                    + ", ".join(invariant_failures)
                )
        record_id = scalar(record["benchmark_id"])
        claim_scope = (
            "equation/topology reconstruction; sampled adapters do not claim source "
            "latch transparent-window equivalence"
        )
        add_metric(
            master_rows,
            section="paper_reconstructed",
            comparison_class="automatic_reconstruction_not_original_coordinates",
            record_id=record_id,
            benchmark=record_id,
            status=scalar(record["status"]),
            metric="outcome",
            value=record["status"],
            claim_scope=claim_scope,
            source=source,
        )
        for metric, unit in (
            ("expectation_met", "boolean"),
            ("state_boundaries", "boundaries"),
            ("ii", "phase_ticks"),
            ("nodes", "nodes"),
            ("routes", "routes"),
            ("feedback_routes", "routes"),
            ("directed_cycle_present", "boolean"),
            ("mapped_unique_xy_sites", "xy_sites"),
            ("pipeline_wall_seconds", "s"),
            ("physical_state_signoff", "status"),
        ):
            add_metric(
                master_rows,
                section="paper_reconstructed",
                comparison_class="automatic_reconstruction_not_original_coordinates",
                record_id=record_id,
                benchmark=record_id,
                status=scalar(record["status"]),
                metric=metric,
                value=record[metric],
                unit=unit,
                claim_scope=claim_scope,
                source=source,
            )

    return {
        "comparison_class": "automatic_reconstruction_not_original_coordinates",
        "cases": len(records),
        "expectations_met": sum(record["expectation_met"] is True for record in records),
        "accepted_sampled_cases": sum(
            record["status"] == "pass"
            and record["temporal_relation"] == "sampled_state_only"
            and record["expectation_met"] is True
            and record["converter_status"] == "success"
            and record["pnr_status"] == "success"
            for record in records
        ),
        "safely_rejected_level_sensitive_cases": sum(
            record["status"] == "reject"
            and record["temporal_relation"] == "same_model"
            and str(record["state_element"] or "").endswith("latch")
            and record["expectation_met"] is True
            and record["converter_status"] == "reject"
            for record in records
        ),
        "records": records,
    }


def summarize_z3_audit(
    payload: Mapping[str, Any] | None,
    master_rows: list[dict[str, str]],
    source: str,
) -> dict[str, Any]:
    if payload is None:
        return {
            "available": False,
            "comparison_class": "same_machine_backend_audit_subset",
            "records": [],
        }
    records: list[dict[str, Any]] = []
    transition_vectors = 0
    transition_mismatches = 0
    transition_nonpass_designs = 0
    transition_zero_case_designs = 0
    for design in payload.get("records", []):
        check = design.get("transition_check") or {}
        transition_vectors += int(number(check.get("cases")) or 0)
        transition_mismatches += int(number(check.get("mismatches")) or 0)
        transition_nonpass_designs += check.get("status") != "pass"
        transition_zero_case_designs += int(number(check.get("cases")) or 0) <= 0
        variant = next(
            (
                candidate
                for candidate in design.get("variants", [])
                if candidate.get("name") == "cyclic_z3_adaptive"
            ),
            {},
        )
        metrics = variant.get("metrics") or {}
        if variant.get("status") == "success":
            invariant_failures: list[str] = []
            if metrics.get("directed_cycle_present") is not True:
                invariant_failures.append("directed_cycle_present")
            if metrics.get("mapping_drc") is not True:
                invariant_failures.append("mapping_drc")
            if not (number(metrics.get("feedback_routes")) or 0) > 0:
                invariant_failures.append("feedback_routes")
            if not (number(metrics.get("initiation_interval")) or 0) > 0:
                invariant_failures.append("initiation_interval")
            if metrics.get("phase_backend") != "external_z3":
                invariant_failures.append("phase_backend")
            if not variant.get("layout") or not variant.get("tex"):
                invariant_failures.append("layout_or_tex")
            if invariant_failures:
                raise ValueError(
                    f"Z3 audit success invariant failed for {design.get('design')}: "
                    + ", ".join(invariant_failures)
                )
        record = {
            "design": design.get("design"),
            "status": variant.get("status"),
            "workflow_wall_seconds": variant.get("duration_seconds"),
            "ii": metrics.get("initiation_interval"),
            "nodes": metrics.get("nodes"),
            "routes": metrics.get("routes"),
            "feedback_routes": metrics.get("feedback_routes"),
            "mapped_unique_xy_sites": metrics.get("mapped_qca_cells"),
            "phase_backend": metrics.get("phase_backend"),
            "physical_state_signoff": metrics.get("physical_state_signoff"),
        }
        records.append(record)
        for metric, unit in (
            ("workflow_wall_seconds", "s"),
            ("ii", "phase_ticks"),
            ("nodes", "nodes"),
            ("routes", "routes"),
            ("feedback_routes", "routes"),
            ("mapped_unique_xy_sites", "xy_sites"),
        ):
            add_metric(
                master_rows,
                section="rtl_auto_pnr",
                comparison_class="same_machine_backend_audit_subset",
                record_id=f"z3_audit:{record['design']}",
                benchmark=scalar(record["design"]),
                variant="cyclic_z3_adaptive",
                status=scalar(record["status"]),
                metric=metric,
                value=record[metric],
                unit=unit,
                claim_scope="post-fix Z3 backend audit subset; not the nine-design success-rate denominator",
                source=source,
            )
    return {
        "available": True,
        "comparison_class": "same_machine_backend_audit_subset",
        "designs": len(records),
        "successes": sum(record["status"] == "success" for record in records),
        "transition_vectors": transition_vectors,
        "transition_mismatches": transition_mismatches,
        "transition_nonpass_designs": transition_nonpass_designs,
        "transition_zero_case_designs": transition_zero_case_designs,
        "all_audited_z3_runs_successful": bool(records)
        and all(record["status"] == "success" for record in records)
        and transition_mismatches == 0
        and transition_nonpass_designs == 0
        and transition_zero_case_designs == 0,
        "records": records,
    }


def summarize_physical(
    physical_rows: Sequence[Mapping[str, Any]],
    physical_payload: Mapping[str, Any] | None,
    convergence: Mapping[str, Any] | None,
    models: Mapping[str, Any] | None,
    master_rows: list[dict[str, str]],
    sources: Mapping[str, str],
) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    detail_by_benchmark = {
        str(record.get("benchmark_id")): record
        for record in (physical_payload or {}).get("records", [])
    }
    for row in physical_rows:
        detail = detail_by_benchmark.get(str(row.get("benchmark_id")), {})
        mapping = detail.get("mapping") or {}
        record = {
            "case_id": row.get("case_id"),
            "benchmark_id": row.get("benchmark_id"),
            "status": row.get("status"),
            "pnr_unique_xy_sites": number(mapping.get("pnr_reported_cells")),
            "exported_qca_cells": number(row.get("mapped_qca_cells")),
            "export_minus_pnr_count": (
                number(row.get("mapped_qca_cells"))
                - number(mapping.get("pnr_reported_cells"))
                if number(row.get("mapped_qca_cells")) is not None
                and number(mapping.get("pnr_reported_cells")) is not None
                else None
            ),
            "source_cell_counts_match": mapping.get(
                "current_mapping_matches_pnr_report"
            ),
            "qca_export_matches_pnr_report": mapping.get(
                "current_qca_export_matches_pnr_report"
            ),
            "crossovers": number(row.get("crossovers")),
            "engine_equivalent": boolean(row.get("engine_equivalent")),
            "bistable_speedup": number(row.get("bistable_speedup")),
            "functional_scope": row.get("functional_scope"),
            "functional_status": row.get("functional_status"),
            "diagnostic_logic_agreement": number(row.get("diagnostic_logic_agreement")),
            "physical_state_signoff": boolean(row.get("physical_state_signoff")),
            "energy_time_step_s": number(row.get("energy_time_step_s")),
            "energy_cycles": number(row.get("energy_cycles")),
            "average_error_energy_eV": number(row.get("average_error_energy_eV")),
            "average_bath_clock_energy_eV": number(
                row.get("average_bath_clock_energy_eV")
            ),
            "derived_power_W": number(row.get("derived_power_W")),
            "energy_finite": boolean(row.get("energy_finite")),
            "energy_exploratory_only": boolean(row.get("energy_exploratory_only")),
        }
        records.append(record)
        record_id = scalar(record["case_id"])
        claim_scope = (
            "structural mapping, implementation-equivalent simulator engines, and "
            "exploratory energy on uncharacterized physical storage"
        )
        for metric, unit in (
            ("pnr_unique_xy_sites", "xy_sites"),
            ("exported_qca_cells", "qcadcell_instances"),
            ("export_minus_pnr_count", "count_difference"),
            ("source_cell_counts_match", "boolean"),
            ("qca_export_matches_pnr_report", "boolean"),
            ("crossovers", "crossovers"),
            ("engine_equivalent", "boolean"),
            ("bistable_speedup", "ratio"),
            ("functional_scope", "scope"),
            ("functional_status", "status"),
            ("diagnostic_logic_agreement", "ratio"),
            ("physical_state_signoff", "boolean"),
            ("energy_time_step_s", "s"),
            ("energy_cycles", "cycles"),
            ("average_error_energy_eV", "eV"),
            ("average_bath_clock_energy_eV", "eV"),
            ("derived_power_W", "W"),
            ("energy_finite", "boolean"),
            ("energy_exploratory_only", "boolean"),
        ):
            add_metric(
                master_rows,
                section="physical_energy_exploratory",
                comparison_class="same_machine_exploratory_physical_analysis",
                record_id=record_id,
                benchmark=scalar(record["benchmark_id"]),
                status=scalar(record["status"]),
                metric=metric,
                value=record[metric],
                unit=unit,
                claim_scope=claim_scope,
                source=sources.get("physical", ""),
            )

    convergence_records: list[dict[str, Any]] = []
    for case in (convergence or {}).get("records", []):
        convergence_note = case.get("energy_step_convergence") or {}
        for run in case.get("energy_runs") or []:
            record = {
                "case_id": case.get("case_id"),
                "time_step_s": run.get("time_step_s"),
                "average_error_eV": run.get("average_error_eV"),
                "average_bath_clock_eV": run.get("average_bath_clock_eV"),
                "derived_power_W": run.get("derived_power_W"),
                "finite": run.get("finite"),
                "paper_ready": convergence_note.get("paper_ready"),
                "relative_change_last_two": convergence_note.get(
                    "relative_change_last_two"
                ),
            }
            convergence_records.append(record)
            record_id = f"energy_dt:{record['case_id']}:{scalar(record['time_step_s'])}"
            for metric, unit in (
                ("average_error_eV", "eV"),
                ("average_bath_clock_eV", "eV"),
                ("derived_power_W", "W"),
                ("finite", "boolean"),
                ("paper_ready", "boolean"),
                ("relative_change_last_two", "ratio"),
            ):
                add_metric(
                    master_rows,
                    section="physical_energy_exploratory",
                    comparison_class="energy_time_step_sensitivity",
                    record_id=record_id,
                    benchmark=scalar(record["case_id"]),
                    variant=f"dt={scalar(record['time_step_s'])}",
                    status="exploratory_only",
                    metric=metric,
                    value=record[metric],
                    unit=unit,
                    claim_scope="numerical time-step sensitivity; not physical-state sign-off",
                    source=sources.get("convergence", ""),
                )

    model_records: list[dict[str, Any]] = []
    for case in (models or {}).get("records", []):
        simulation = case.get("simulation") or {}
        for comparison in simulation.get("comparisons") or []:
            accuracy = comparison.get("accuracy") or {}
            engine_outputs_equivalent = (
                simulation.get("engine_equivalent") is True
                and accuracy.get("comparable") is True
                and number(accuracy.get("confident_logic_agreement")) == 1
            )
            record = {
                "case_id": case.get("case_id"),
                "model": comparison.get("model"),
                "reference": comparison.get("reference"),
                "candidate": comparison.get("candidate"),
                "speedup": comparison.get("speedup"),
                "confident_logic_agreement": accuracy.get(
                    "confident_logic_agreement"
                ),
                "accuracy_comparable": accuracy.get("comparable"),
                "engine_outputs_equivalent": engine_outputs_equivalent,
                "comparison_scope": simulation.get("scope"),
            }
            model_records.append(record)
            for metric, unit in (
                ("speedup", "ratio"),
                ("confident_logic_agreement", "ratio"),
                ("accuracy_comparable", "boolean"),
                ("engine_outputs_equivalent", "boolean"),
            ):
                add_metric(
                    master_rows,
                    section="physical_energy_exploratory",
                    comparison_class="simulator_implementation_equivalence",
                    record_id=f"simulator:{record['case_id']}:{record['model']}",
                    benchmark=scalar(record["case_id"]),
                    variant=scalar(record["model"]),
                    status=(
                        "equivalent_engine_outputs"
                        if engine_outputs_equivalent
                        else "non_equivalent_or_incomparable_engine_outputs"
                    ),
                    metric=metric,
                    value=record[metric],
                    unit=unit,
                    claim_scope="baseline versus accelerated implementation; not RTL correctness",
                    source=sources.get("models", ""),
                )

    return {
        "comparison_class": "exploratory_only",
        "cell_count_definitions": {
            "pnr_unique_xy_sites": (
                "unique mapped (x,y) positions reported by cyclic P&R; layers are collapsed"
            ),
            "exported_qca_cells": (
                "layer-aware QCADCell instances in the physical-analysis export"
            ),
            "direct_comparison_permitted": False,
        },
        "physical_state_signoff_characterized_cases": sum(
            record["physical_state_signoff"] is True for record in records
        ),
        "records": records,
        "energy_convergence": convergence_records,
        "simulator_models": model_records,
    }


def load_reported_only(path: Path | None) -> list[dict[str, Any]]:
    if path is None:
        return [dict(row) for row in REPORTED_ONLY_DEFAULTS]
    with path.open(newline="", encoding="utf-8") as handle:
        return [dict(row) for row in csv.DictReader(handle)]


def summarize_external(
    external_root: Path,
    clocking_dir: Path,
    gold_dir: Path,
    reported_only_csv: Path | None,
    inventory: list[dict[str, Any]],
    master_rows: list[dict[str, str]],
    expected_platform: str | None = None,
) -> dict[str, Any]:
    same_machine: list[dict[str, Any]] = []
    scope_audit = load_json(
        external_root / "comparison_scope.json",
        "external_comparison_scope_json",
        inventory,
        required=False,
    )
    load_csv(
        external_root / "comparison_scope.csv",
        "external_comparison_scope_csv",
        inventory,
        required=False,
    )

    clocking_json = load_json(
        clocking_dir / "summary.json", "external_fiction_clocking_json", inventory,
        required=False,
    )
    clocking_provenance = load_json(
        clocking_dir / "provenance.json",
        "external_fiction_clocking_provenance",
        inventory,
        required=False,
    )
    clocking_rows = load_csv(
        clocking_dir / "summary.csv", "external_fiction_clocking_csv", inventory,
        required=False,
    )
    reported_comparison = load_csv(
        clocking_dir / "reported_vs_reproduced.csv",
        "external_walter_reported_vs_reproduced",
        inventory,
        required=False,
    )
    if clocking_json is not None and clocking_rows is not None:
        if clocking_provenance is None:
            raise ValueError("complete fiction clocking results require provenance.json")
        host_platform = str((clocking_provenance.get("host") or {}).get("platform") or "")
        if expected_platform and host_platform != expected_platform:
            raise ValueError("fiction clocking host differs from the internal RTL host")
        per_layout_medians = [
            float(value)
            for row in (clocking_rows or [])
            if (value := number(row.get("runtime_median_s"))) is not None
        ]
        record = {
            "id": "fiction_determine_clocking_walter2024",
            "available": True,
            "input_status": "complete",
            "host_platform": host_platform,
            "same_machine_with_internal_rtl": bool(expected_platform),
            "task": "combinational_fixed_layout_clock_assignment",
            "benchmarks": clocking_json.get("layouts_per_repetition"),
            "repetitions": clocking_json.get("repetitions"),
            "raw_measurements": clocking_json.get("raw_measurements"),
            "equivalent_measurements": clocking_json.get(
                "equivalent_measurements"
            ),
            "all_valid": clocking_json.get("all_equivalent"),
            "median_of_per_layout_runtime_medians_s": (
                statistics.median(per_layout_medians) if per_layout_medians else None
            ),
            "reported_geometry_rows": (
                len(reported_comparison) if reported_comparison is not None else None
            ),
            "reported_geometry_matches": (
                sum(
                    boolean(row.get("area_match")) is True
                    and boolean(row.get("dimensions_match_allowing_rotation")) is True
                    for row in reported_comparison
                )
                if reported_comparison is not None else None
            ),
            "comparison_class": "external_combinational_context",
            "source_comparison_class": clocking_json.get("comparison_class"),
            "scope": clocking_json.get("scope"),
        }
        observed_all_valid = (
            number(record["equivalent_measurements"])
            == number(record["raw_measurements"])
            and all(
                number(row.get("equivalent_runs")) == number(row.get("runs"))
                for row in clocking_rows
            )
        )
        if number(record["raw_measurements"]) != sum(
            int(number(row.get("runs")) or 0) for row in clocking_rows
        ):
            raise ValueError("fiction clocking raw_measurements disagrees with CSV runs")
        if number(record["benchmarks"]) != len(clocking_rows):
            raise ValueError("fiction clocking layout count disagrees with summary.csv")
        if boolean(record["all_valid"]) is not observed_all_valid:
            raise ValueError("fiction clocking all_valid disagrees with observed counts")
        same_machine.append(record)
        for metric, unit in (
            ("benchmarks", "layouts"),
            ("repetitions", "runs"),
            ("raw_measurements", "measurements"),
            ("equivalent_measurements", "measurements"),
            ("all_valid", "boolean"),
            ("median_of_per_layout_runtime_medians_s", "s"),
            ("reported_geometry_rows", "layouts"),
            ("reported_geometry_matches", "layouts"),
        ):
            add_metric(
                master_rows,
                section="external_same_machine_context",
                comparison_class="external_combinational_context",
                record_id=record["id"],
                variant="fiction_v0.7.0",
                status="complete" if record["all_valid"] is True else "incomplete",
                metric=metric,
                value=record[metric],
                unit=unit,
                claim_scope="combinational clock assignment; incompatible with cyclic sequential P&R",
                source=str(clocking_dir / "summary.json"),
            )
    else:
        same_machine.append(
            {
                "id": "fiction_determine_clocking_walter2024",
                "available": False,
                "input_status": (
                    "partial" if clocking_json is not None or clocking_rows is not None
                    else "missing"
                ),
                "task": "combinational_fixed_layout_clock_assignment",
                "comparison_class": "external_combinational_context",
            }
        )

    gold_json = load_json(
        gold_dir / "summary.json", "external_fiction_gold_json", inventory,
        required=False,
    )
    gold_provenance = load_json(
        gold_dir / "provenance.json",
        "external_fiction_gold_provenance",
        inventory,
        required=False,
    )
    gold_rows = load_csv(
        gold_dir / "summary.csv", "external_fiction_gold_csv", inventory,
        required=False,
    )
    if gold_json is not None and gold_rows is not None:
        if gold_provenance is None:
            raise ValueError("complete fiction GOLD results require provenance.json")
        host_platform = str((gold_provenance.get("host") or {}).get("platform") or "")
        if expected_platform and host_platform != expected_platform:
            raise ValueError("fiction GOLD host differs from the internal RTL host")
        per_benchmark_medians = [
            float(value)
            for row in (gold_rows or [])
            if (value := number(row.get("runtime_median_s"))) is not None
        ]
        record = {
            "id": "fiction_gold_subset",
            "available": True,
            "input_status": "complete",
            "host_platform": host_platform,
            "same_machine_with_internal_rtl": bool(expected_platform),
            "task": "combinational_gate_level_pnr",
            "benchmarks": gold_json.get("benchmarks"),
            "repetitions": gold_json.get("repetitions"),
            "raw_measurements": gold_json.get("raw_measurements"),
            "pass_measurements": sum(
                int(number(row.get("pass_runs")) or 0) for row in (gold_rows or [])
            ),
            "strong_equivalence_measurements": sum(
                int(number(row.get("strong_equivalence_runs")) or 0)
                for row in (gold_rows or [])
            ),
            "timeout_boundary_measurements": sum(
                int(number(row.get("timeout_boundary_runs")) or 0)
                for row in (gold_rows or [])
            ),
            "all_valid": gold_json.get(
                "all_pass_strong_equivalent_without_timeout"
            ),
            "median_of_per_benchmark_runtime_medians_s": (
                statistics.median(per_benchmark_medians)
                if per_benchmark_medians else None
            ),
            "comparison_class": "external_combinational_context",
            "source_comparison_class": gold_json.get("comparison_class"),
            "scope": gold_json.get("scope"),
        }
        observed_all_valid = (
            number(record["pass_measurements"])
            == number(record["raw_measurements"])
            and number(record["strong_equivalence_measurements"])
            == number(record["raw_measurements"])
            and number(record["timeout_boundary_measurements"]) == 0
        )
        if number(record["raw_measurements"]) != sum(
            int(number(row.get("runs")) or 0) for row in gold_rows
        ):
            raise ValueError("fiction GOLD raw_measurements disagrees with CSV runs")
        if number(record["benchmarks"]) != len(gold_rows):
            raise ValueError("fiction GOLD benchmark count disagrees with summary.csv")
        if boolean(record["all_valid"]) is not observed_all_valid:
            raise ValueError("fiction GOLD all_valid disagrees with observed counts")
        same_machine.append(record)
        for metric, unit in (
            ("benchmarks", "networks"),
            ("repetitions", "runs"),
            ("raw_measurements", "measurements"),
            ("pass_measurements", "measurements"),
            ("strong_equivalence_measurements", "measurements"),
            ("timeout_boundary_measurements", "measurements"),
            ("all_valid", "boolean"),
            ("median_of_per_benchmark_runtime_medians_s", "s"),
        ):
            add_metric(
                master_rows,
                section="external_same_machine_context",
                comparison_class="external_combinational_context",
                record_id=record["id"],
                variant="fiction_v0.7.0",
                status="complete" if record["all_valid"] is True else "incomplete",
                metric=metric,
                value=record[metric],
                unit=unit,
                claim_scope="combinational P&R subset; incompatible with cyclic sequential P&R",
                source=str(gold_dir / "summary.json"),
            )
    else:
        same_machine.append(
            {
                "id": "fiction_gold_subset",
                "available": False,
                "input_status": (
                    "partial" if gold_json is not None or gold_rows is not None
                    else "missing"
                ),
                "task": "combinational_gate_level_pnr",
                "comparison_class": "external_combinational_context",
            }
        )

    if reported_only_csv is not None:
        if not reported_only_csv.is_file():
            raise FileNotFoundError(f"reported-only catalog is missing: {reported_only_csv}")
        inventory.append(
            {
                "id": "reported_only_catalog",
                "path": str(reported_only_csv),
                "available": True,
                "required": True,
                "bytes": reported_only_csv.stat().st_size,
                "sha256": sha256(reported_only_csv),
            }
        )
    else:
        inventory.append(
            {
                "id": "reported_only_catalog",
                "path": "scripts/aggregate_sequential_experiments.py:REPORTED_ONLY_DEFAULTS",
                "available": True,
                "required": False,
                "embedded": True,
                "rows": len(REPORTED_ONLY_DEFAULTS),
            }
        )
    reported = load_reported_only(reported_only_csv)
    for record in reported:
        add_metric(
            master_rows,
            section="external_reported_only",
            comparison_class="reported_only_no_runtime_ratio",
            record_id=scalar(record.get("reference")),
            benchmark=scalar(record.get("reference")),
            status=scalar(record.get("availability")),
            metric="doi",
            value=record.get("doi"),
            claim_scope="bibliographic/method context only; numerical speedup intentionally blank",
            source=(str(reported_only_csv) if reported_only_csv else "built_in_claim_catalog"),
        )

    return {
        "comparison_class": "external_combinational_context",
        "scope_audit": scope_audit,
        "same_machine_context": same_machine,
        "reported_only": reported,
        "head_to_head_speedup_computed": False,
    }


LATEX_REPLACEMENTS = {
    "\\": r"\textbackslash{}",
    "&": r"\&",
    "%": r"\%",
    "$": r"\$",
    "#": r"\#",
    "_": r"\_",
    "{": r"\{",
    "}": r"\}",
    "~": r"\textasciitilde{}",
    "^": r"\textasciicircum{}",
}


def latex_escape(value: Any) -> str:
    text = scalar(value)
    if text == "":
        return "--"
    return "".join(LATEX_REPLACEMENTS.get(character, character) for character in text)


def percent(value: Any) -> str:
    numeric = number(value)
    return "--" if numeric is None else f"{100.0 * float(numeric):.1f}%"


def compact_float(value: Any, digits: int = 3) -> str:
    numeric = number(value)
    if numeric is None:
        return "--"
    return f"{float(numeric):.{digits}g}"


def external_validation_text(row: Mapping[str, Any]) -> str:
    if row.get("available") is not True:
        return "not run"
    raw = row.get("raw_measurements")
    if row.get("id") == "fiction_determine_clocking_walter2024":
        result = f"{row.get('equivalent_measurements')}/{raw} eq."
        if row.get("reported_geometry_rows") is not None:
            result += (
                f"; {row.get('reported_geometry_matches')}/"
                f"{row.get('reported_geometry_rows')} geometry"
            )
        return result
    if row.get("id") == "fiction_gold_subset":
        return (
            f"{row.get('pass_measurements')}/{raw} pass; "
            f"{row.get('strong_equivalence_measurements')}/{raw} strong-eq.; "
            f"{row.get('timeout_boundary_measurements')} timeout"
        )
    return "available"


def write_latex_table(
    path: Path,
    *,
    caption: str,
    label: str,
    headers: Sequence[str],
    rows: Iterable[Sequence[Any]],
    alignment: str,
    wide: bool = False,
) -> None:
    environment = "table*" if wide else "table"
    lines = [
        "% Generated by scripts/aggregate_sequential_experiments.py; requires booktabs and graphicx.",
        f"\\begin{{{environment}}}[t]",
        "  \\centering",
        "  \\small",
        f"  \\caption{{{latex_escape(caption)}}}",
        f"  \\label{{{label}}}",
    ]
    if wide:
        lines.append("  \\resizebox{\\textwidth}{!}{%")
    lines.extend([
        f"  \\begin{{tabular}}{{{alignment}}}",
        "    \\toprule",
        "    " + " & ".join(latex_escape(header) for header in headers) + r" \\",
        "    \\midrule",
    ])
    for row in rows:
        lines.append("    " + " & ".join(latex_escape(value) for value in row) + r" \\")
    lines.extend(
        [
            "    \\bottomrule",
            "  \\end{tabular}",
            *( ["  }%"] if wide else [] ),
            f"\\end{{{environment}}}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def write_tables(output: Path, summary: Mapping[str, Any]) -> list[str]:
    tables = output / "tables"
    tables.mkdir(parents=True, exist_ok=True)
    generated: list[str] = []

    correctness = summary["sections"]["correctness_ablation"]
    exact = correctness["exact_vs_modulo"]
    ii = correctness["ii_ablation"]
    path = tables / "correctness_ablation.tex"
    write_latex_table(
        path,
        caption="Exact clock closure and initiation-interval ablations.",
        label="tab:sequential-correctness-ablation",
        headers=("Experiment", "Cases", "Accepted", "Key difference", "Oracle mismatch"),
        rows=(
            (
                "Exact vs modulo-only",
                exact.get("cases"),
                exact.get("exact_sat"),
                f"{exact.get('modulo_false_accept', '--')} false accepts ({percent(exact.get('false_accept_rate_among_exact_unsat'))})",
                exact.get("solver_oracle_mismatches"),
            ),
            (
                "Adaptive II vs fixed II=4",
                ii.get("cases"),
                ii.get("adaptive_sat"),
                f"+{ii.get('adaptive_only_sat', '--')} vs {ii.get('fixed_ii4_sat', '--')} fixed-II SAT",
                ii.get("solver_oracle_mismatches"),
            ),
        ),
        alignment="lrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    edge_groups = correctness.get("edge_scaling") or []
    by_edge: dict[Any, dict[str, Mapping[str, Any]]] = defaultdict(dict)
    for group in edge_groups:
        by_edge[group.get("route_edges")][str(group.get("expected_status"))] = group
    solver_scaling_rows: list[tuple[Any, ...]] = []
    for edge, groups in sorted(by_edge.items(), key=lambda item: int(item[0])):
        run_counts = [
            sum(
                int(number(group.get(metric)) or 0)
                for metric in ("sat_runs", "unsat_runs", "limit_runs", "invalid_runs")
            )
            for group in groups.values()
        ]
        if run_counts and len(set(run_counts)) != 1:
            raise ValueError(f"edge scaling E={edge}: unequal repetitions per status")
        solver_scaling_rows.append(
            (
                edge,
                groups.get("SAT", {}).get("ii"),
                compact_float(groups.get("SAT", {}).get("time_us_p50")),
                compact_float(groups.get("SAT", {}).get("time_us_p95")),
                groups.get("UNSAT", {}).get("ii"),
                compact_float(groups.get("UNSAT", {}).get("time_us_p50")),
                compact_float(groups.get("UNSAT", {}).get("time_us_p95")),
                compact_float(groups.get("LIMIT", {}).get("time_us_p50")),
                run_counts[0] if run_counts else "",
            )
        )
    path = tables / "solver_scaling.tex"
    write_latex_table(
        path,
        caption="Global clock solver scaling on synthetic recurrence instances; times are microseconds and each SAT, UNSAT, and low-budget LIMIT group has the stated repetitions.",
        label="tab:sequential-solver-scaling",
        headers=("Edges", "SAT II", "SAT p50", "SAT p95", "UNSAT II", "UNSAT p50", "UNSAT p95", "LIMIT p50", "Reps/group"),
        rows=solver_scaling_rows,
        alignment="rrrrrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    rtl = summary["sections"]["rtl_auto_pnr"]
    path = tables / "rtl_variant_summary.tex"
    write_latex_table(
        path,
        caption="RTL-to-layout outcomes. Eligible rates exclude observation-only state outputs. Wall times cover each variant's full workflow and are not phase-backend speedups.",
        label="tab:rtl-variant-summary",
        headers=("Variant", "Designs", "Eligible", "Success", "All", "Eligible", "Workflow median s", "Workflow p95 s"),
        rows=(
            (
                row.get("variant"),
                row.get("cases"),
                row.get("eligible_cases"),
                row.get("successes"),
                percent(row.get("success_rate_all_designs")),
                percent(row.get("success_rate_eligible")),
                compact_float(row.get("workflow_wall_seconds_median_all_outcomes")),
                compact_float(row.get("workflow_wall_seconds_p95_all_outcomes")),
            )
            for row in rtl.get("variant_aggregates", [])
        ),
        alignment="lrrrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    z3_success = [
        row
        for row in rtl.get("records", [])
        if row.get("variant") == "cyclic_z3_adaptive" and row.get("status") == "success"
    ]
    path = tables / "rtl_z3_successful_layouts.tex"
    write_latex_table(
        path,
        caption="Successful cyclic layouts using adaptive external Z3. Workflow wall time includes geometry export, external solve, and finalization; storage physics remains uncharacterized.",
        label="tab:rtl-z3-layout-qor",
        headers=("Design", "State bits", "II", "Nodes", "Routes", "Feedback", "Mapped XY sites", "Area", "Workflow wall s"),
        rows=(
            (
                display_name(row.get("design")), row.get("state_bits"), row.get("ii"),
                row.get("nodes"), row.get("routes"), row.get("feedback_routes"),
                row.get("mapped_unique_xy_sites"), row.get("bbox_area"),
                compact_float(row.get("workflow_wall_seconds")),
            )
            for row in z3_success
        ),
        alignment="lrrrrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    z3_audit = rtl.get("z3_backend_audit") or {}
    path = tables / "z3_backend_audit.tex"
    write_latex_table(
        path,
        caption="Post-fix Z3 backend audit subset; this audit is not used as the nine-design success-rate denominator.",
        label="tab:z3-backend-audit",
        headers=("Design", "Outcome", "II", "Nodes", "Routes", "Feedback", "Mapped XY sites", "Workflow wall s"),
        rows=(
            (
                display_name(row.get("design")), row.get("status"), row.get("ii"), row.get("nodes"),
                row.get("routes"), row.get("feedback_routes"), row.get("mapped_unique_xy_sites"),
                compact_float(row.get("workflow_wall_seconds")),
            )
            for row in z3_audit.get("records", [])
        ),
        alignment="llrrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    paper = summary["sections"]["paper_reconstructed"]
    path = tables / "paper_reconstructed.tex"
    write_latex_table(
        path,
        caption="Automatic processing of recent-paper sequential-QCA reconstructions. Reject denotes a safe level-sensitive-model rejection.",
        label="tab:paper-reconstructed",
        headers=("Benchmark", "Temporal relation", "Outcome", "II", "Nodes", "Routes", "Feedback", "Mapped XY sites"),
        rows=(
            (
                display_name(row.get("benchmark_id")), row.get("temporal_relation"), row.get("status"),
                row.get("ii"), row.get("nodes"), row.get("routes"),
                row.get("feedback_routes"), row.get("mapped_unique_xy_sites"),
            )
            for row in paper.get("records", [])
        ),
        alignment="lllrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    physical = summary["sections"]["physical_energy_exploratory"]
    path = tables / "physical_energy_exploratory.tex"
    write_latex_table(
        path,
        caption="Exploratory mapped-physics diagnostics; none of these cases has characterized physical storage.",
        label="tab:physical-energy-exploratory",
        headers=("Benchmark", "Exported cells", "Xover", "Engine eq.", "Diag.", "Agreement", "Bath-clock eV", "Derived W", "Sign-off"),
        rows=(
            (
                display_name(row.get("benchmark_id")), row.get("exported_qca_cells"), row.get("crossovers"),
                row.get("engine_equivalent"), row.get("functional_status"),
                compact_float(row.get("diagnostic_logic_agreement")),
                compact_float(row.get("average_bath_clock_energy_eV")),
                compact_float(row.get("derived_power_W")), row.get("physical_state_signoff"),
            )
            for row in physical.get("records", [])
        ),
        alignment="lrrrrrrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    convergence_rows = physical.get("energy_convergence", [])
    path = tables / "energy_time_step_sensitivity.tex"
    write_latex_table(
        path,
        caption="Exploratory energy time-step sensitivity; finite output is not a convergence or physical-sign-off claim.",
        label="tab:energy-step-sensitivity",
        headers=("Time step s", "Error eV", "Bath-clock eV", "Derived W", "Finite"),
        rows=(
            (
                compact_float(row.get("time_step_s")),
                compact_float(row.get("average_error_eV")),
                compact_float(row.get("average_bath_clock_eV")),
                compact_float(row.get("derived_power_W")),
                row.get("finite"),
            )
            for row in convergence_rows
        ),
        alignment="rrrrr",
    )
    generated.append(str(path.relative_to(output)))

    external = summary["sections"]["external"]
    external_rows: list[tuple[Any, ...]] = []
    for row in external.get("same_machine_context", []):
        external_rows.append(
            (
                (
                    "fiction determine_clocking"
                    if row.get("id") == "fiction_determine_clocking_walter2024"
                    else "fiction GOLD"
                ),
                "external combinational context",
                (
                    "clock assignment"
                    if row.get("task") == "combinational_fixed_layout_clock_assignment"
                    else "combinational P&R"
                ),
                row.get("benchmarks"), row.get("raw_measurements"),
                external_validation_text(row),
                "no",
            )
        )
    for row in external.get("reported_only", []):
        external_rows.append(
            (
                row.get("reference"), "reported only",
                {
                    "Bhowmik et al. (2022)": "manual sequential layouts",
                    "Deng et al. (2022)": "manual majority latch",
                    "Li et al. (2022)": "GA+A* combinational P&R",
                    "Zhang et al. (2024)": "hierarchical A* combinational P&R",
                }.get(row.get("reference"), row.get("method_scope")),
                "", "",
                (
                    "reconstructed context"
                    if "reconstruction" in scalar(row.get("availability"))
                    else "reported only"
                ),
                "no",
            )
        )
    path = tables / "external_comparison_classes.tex"
    write_latex_table(
        path,
        caption="External comparison classes. No cross-task, cross-host, or reported-only runtime ratio is computed.",
        label="tab:external-comparison-classes",
        headers=("Reference", "Class", "Task", "Cases", "Measurements", "Validation", "Head-to-head"),
        rows=external_rows,
        alignment="llllrrr",
        wide=True,
    )
    generated.append(str(path.relative_to(output)))

    (tables / "all_tables.tex").write_text(
        "% Generated table fragments. Add \\usepackage{booktabs,graphicx} to the manuscript.\n"
        + "".join(
            f"\\input{{{(output / path).as_posix()}}}\n" for path in generated
        ),
        encoding="utf-8",
    )
    generated.append(str((tables / "all_tables.tex").relative_to(output)))
    return generated


def aggregate(args: argparse.Namespace) -> dict[str, Any]:
    artifacts = args.artifacts_root
    clock_dir = args.clock_dir or artifacts / "sequential_clock_comparison_v4"
    rtl_dir = args.rtl_dir or artifacts / "sequential_rtl_experiments_z3_v3"
    z3_audit_path = args.z3_audit or artifacts / "z3_audit_final/summary.json"
    paper_dir = args.paper_dir or artifacts / "sequential_paper_cyclic_benchmarks_release_v2"
    physical_dir = args.physical_dir or artifacts / "sequential_cyclic_physical_analysis_v2"
    convergence_dir = args.energy_convergence_dir or artifacts / "sequential_cyclic_energy_convergence_v2"
    models_dir = args.simon_models_dir or artifacts / "sequential_cyclic_simon_models_v2"
    external_root = (
        args.external_root or artifacts / "external_baselines/fiction_v0.7.0"
    )
    external_clocking = args.external_clocking_dir or external_root / "walter2024"
    external_gold = args.external_gold_dir or external_root / "gold_subset"

    inventory: list[dict[str, Any]] = []
    warnings: list[str] = []
    master_rows: list[dict[str, str]] = []

    clock_raw = load_json(clock_dir / "summary.json", "clock_raw", inventory, required=True)
    clock_validated = load_json(
        clock_dir / "validated_summary.json", "clock_validated", inventory, required=True
    )
    rtl = load_json(rtl_dir / "summary.json", "rtl", inventory, required=True)
    z3_audit = load_json(z3_audit_path, "z3_backend_audit", inventory, required=False)
    paper_rows = load_csv(paper_dir / "summary.csv", "paper", inventory, required=True)
    paper_payload = load_json(
        paper_dir / "summary.json", "paper_json", inventory, required=False
    )
    physical_rows = load_csv(
        physical_dir / "summary.csv", "physical", inventory, required=False
    )
    physical_payload = load_json(
        physical_dir / "summary.json", "physical_json", inventory, required=False
    )
    convergence = load_json(
        convergence_dir / "summary.json", "energy_convergence", inventory, required=False
    )
    models = load_json(
        models_dir / "summary.json", "simon_models", inventory, required=False
    )
    assert clock_raw is not None and clock_validated is not None
    assert rtl is not None and paper_rows is not None

    expected_schemas = (
        ("clock_raw", clock_raw, "ifcn.sequential_clock_experiment.v2"),
        (
            "clock_validated",
            clock_validated,
            "ifcn.sequential_clock_experiment.validated.v1",
        ),
        ("rtl", rtl, "ifcn.sequential-rtl-experiments.v1"),
        ("z3_backend_audit", z3_audit, "ifcn.sequential-rtl-experiments.v1"),
        (
            "paper_json",
            paper_payload,
            "ifcn.sequential-paper-benchmark-run.v0",
        ),
        (
            "physical_json",
            physical_payload,
            "ifcn.sequential-cyclic-physical-benchmark.v1",
        ),
        (
            "energy_convergence",
            convergence,
            "ifcn.sequential-cyclic-physical-benchmark.v1",
        ),
        (
            "simon_models",
            models,
            "ifcn.sequential-cyclic-physical-benchmark.v1",
        ),
    )
    for label, payload, expected_schema in expected_schemas:
        if payload is not None and payload.get("schema") != expected_schema:
            raise ValueError(
                f"{label}: expected schema {expected_schema!r}, "
                f"found {payload.get('schema')!r}"
            )

    raw_snapshot = clock_validated.get("raw_cpp_summary")
    if raw_snapshot is not None and raw_snapshot != clock_raw:
        raise ValueError("clock raw summary does not match validated raw_cpp_summary")

    if paper_payload is not None:
        paper_csv_ids = {str(row.get("benchmark_id")) for row in paper_rows}
        paper_json_ids = {
            str(row.get("benchmark_id")) for row in paper_payload.get("cases", [])
        }
        if paper_csv_ids != paper_json_ids or len(paper_csv_ids) != len(paper_rows):
            raise ValueError("paper summary.csv and summary.json benchmark sets differ")
        for case in paper_payload.get("cases", []):
            csv_row = next(
                row for row in paper_rows
                if str(row.get("benchmark_id")) == str(case.get("benchmark_id"))
            )
            if str(csv_row.get("status")) != str(case.get("status")):
                raise ValueError("paper CSV/JSON status mismatch")

    if physical_payload is not None and physical_rows is not None:
        physical_csv_ids = {str(row.get("benchmark_id")) for row in physical_rows}
        physical_json_ids = {
            str(row.get("benchmark_id"))
            for row in physical_payload.get("records", [])
        }
        if physical_csv_ids != physical_json_ids or len(physical_csv_ids) != len(physical_rows):
            raise ValueError("physical summary.csv and summary.json benchmark sets differ")
        for record in physical_payload.get("records", []):
            csv_row = next(
                row for row in physical_rows
                if str(row.get("benchmark_id")) == str(record.get("benchmark_id"))
            )
            exported = (record.get("mapping") or {}).get("qca_cells")
            if number(csv_row.get("mapped_qca_cells")) != number(exported):
                raise ValueError("physical CSV/JSON exported-cell count mismatch")

    exact_validation = clock_validated.get("exact_vs_modulo") or {}
    ii_validation = clock_validated.get("ii_ablation") or {}
    validation_failures = []
    if (clock_validated.get("validation") or {}).get("passed") is not True:
        validation_failures.append("validation.passed")
    if number(exact_validation.get("mismatches")) != 0:
        validation_failures.append("exact_vs_modulo.mismatches")
    if number(ii_validation.get("mismatches")) != 0:
        validation_failures.append("ii_ablation.mismatches")
    if validation_failures:
        message = "clock validation failed: " + ", ".join(validation_failures)
        if not args.allow_unvalidated:
            raise ValueError(message)
        warnings.append(message)

    if (physical_rows is None) != (physical_payload is None):
        raise ValueError(
            "physical experiment summary.csv and summary.json must be present together"
        )

    expected_paper_dir = paper_dir.resolve()
    provenance_payloads = (
        ("physical", physical_payload),
        ("energy_convergence", convergence),
        ("simon_models", models),
    )
    for label, payload in provenance_payloads:
        if payload is None:
            continue
        input_directory = payload.get("input_directory")
        if not input_directory:
            warnings.append(f"{label}: input_directory provenance is missing")
            continue
        if Path(str(input_directory)).resolve() != expected_paper_dir:
            raise ValueError(
                f"{label}: input_directory {input_directory!r} does not match "
                f"paper experiment {str(expected_paper_dir)!r}"
            )
    if paper_payload is not None and paper_payload.get("output_root"):
        if Path(str(paper_payload["output_root"])).resolve() != expected_paper_dir:
            raise ValueError("paper summary output_root does not match --paper-dir")

    sections = {
        "correctness_ablation": summarize_correctness(
            clock_raw,
            clock_validated,
            master_rows,
            str(clock_dir / "validated_summary.json"),
        ),
        "rtl_auto_pnr": summarize_rtl(
            rtl, master_rows, str(rtl_dir / "summary.json"), warnings
        ),
        "paper_reconstructed": summarize_paper_cases(
            paper_rows, master_rows, str(paper_dir / "summary.csv")
        ),
        "physical_energy_exploratory": summarize_physical(
            physical_rows or [],
            physical_payload,
            convergence,
            models,
            master_rows,
            {
                "physical": str(physical_dir / "summary.csv"),
                "convergence": str(convergence_dir / "summary.json"),
                "models": str(models_dir / "summary.json"),
            },
        ),
    }
    sections["rtl_auto_pnr"]["z3_backend_audit"] = summarize_z3_audit(
        z3_audit, master_rows, str(z3_audit_path)
    )
    sections["physical_energy_exploratory"]["source_claim_boundaries"] = {
        "physical": (physical_payload or {}).get("claim_boundary"),
        "energy_convergence": (convergence or {}).get("claim_boundary"),
        "simon_models": (models or {}).get("claim_boundary"),
    }
    sections["external"] = summarize_external(
        external_root,
        external_clocking,
        external_gold,
        args.reported_only_csv,
        inventory,
        master_rows,
        str((rtl.get("environment") or {}).get("platform") or "") or None,
    )

    summary: dict[str, Any] = {
        "schema": SCHEMA,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "claim_boundaries": [
            "RTL transition checks precede mapping and do not establish cell-level state storage.",
            "Cyclic P&R success means geometry, global clock closure, directed feedback, and structural mapping; physical storage is not characterized.",
            "Energy and functional waveforms are exploratory while physical_state_signoff is false.",
            "Simulator speedup compares baseline and accelerated implementations of the same model, not iFCN against another P&R method.",
            "fiction runs are same-machine external context on combinational tasks, not a cyclic sequential head-to-head.",
            "Reported-only papers receive no runtime, area, power, or speedup ratio.",
        ],
        "inputs": inventory,
        "warnings": warnings,
        "sections": sections,
        "master_metric_rows": len(master_rows),
    }

    args.output_dir.mkdir(parents=True, exist_ok=True)
    master_rows.sort(
        key=lambda row: (
            row["section"], row["comparison_class"], row["record_id"], row["metric"]
        )
    )
    with (args.output_dir / "summary.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=MASTER_COLUMNS)
        writer.writeheader()
        writer.writerows(master_rows)
    summary["latex_tables"] = write_tables(args.output_dir, summary)
    (args.output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    summary = aggregate(args)
    rtl = summary["sections"]["rtl_auto_pnr"]
    paper = summary["sections"]["paper_reconstructed"]
    external = summary["sections"]["external"]
    print(f"wrote {args.output_dir / 'summary.csv'}")
    print(f"wrote {args.output_dir / 'summary.json'}")
    print(
        "RTL transition checks: "
        f"{rtl['transition_equivalence']['transition_vectors']} vectors, "
        f"{rtl['transition_equivalence']['mismatches']} mismatches"
    )
    print(
        f"paper reconstructions: {paper['accepted_sampled_cases']} accepted, "
        f"{paper['safely_rejected_level_sensitive_cases']} safely rejected"
    )
    available_external = sum(
        row.get("available") is True for row in external["same_machine_context"]
    )
    print(f"optional same-machine external baselines available: {available_external}/2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
