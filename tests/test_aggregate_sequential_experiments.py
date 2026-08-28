#!/usr/bin/env python3
"""Offline tests for the paper-result aggregation boundary."""

from __future__ import annotations

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = REPOSITORY_ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import aggregate_sequential_experiments as aggregator  # noqa: E402


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


class SequentialExperimentAggregatorTest(unittest.TestCase):
    def test_latex_escape_is_applied_once(self) -> None:
        self.assertEqual(aggregator.latex_escape("a_b&c%"), r"a\_b\&c\%")
        self.assertEqual(aggregator.percent(0.625), "62.5%")

    def test_external_validation_uses_observed_counts(self) -> None:
        self.assertEqual(
            aggregator.external_validation_text(
                {
                    "id": "fiction_determine_clocking_walter2024",
                    "available": True,
                    "raw_measurements": 10,
                    "equivalent_measurements": 7,
                    "reported_geometry_rows": 5,
                    "reported_geometry_matches": 4,
                }
            ),
            "7/10 eq.; 4/5 geometry",
        )
        self.assertEqual(
            aggregator.external_validation_text(
                {
                    "id": "fiction_gold_subset",
                    "available": True,
                    "raw_measurements": 4,
                    "pass_measurements": 3,
                    "strong_equivalence_measurements": 2,
                    "timeout_boundary_measurements": 1,
                }
            ),
            "3/4 pass; 2/4 strong-eq.; 1 timeout",
        )

    def test_incomparable_simulator_outputs_are_not_labeled_equivalent(self) -> None:
        metric_rows: list[dict[str, str]] = []
        result = aggregator.summarize_physical(
            [],
            None,
            None,
            {
                "records": [
                    {
                        "case_id": "counterexample",
                        "simulation": {
                            "engine_equivalent": True,
                            "scope": "implementation_equivalence_only",
                            "comparisons": [
                                {
                                    "model": "bistable",
                                    "reference": "baseline",
                                    "candidate": "accelerated",
                                    "speedup": 2.0,
                                    "accuracy": {
                                        "comparable": False,
                                        "confident_logic_agreement": 0,
                                    },
                                }
                            ],
                        },
                    }
                ]
            },
            metric_rows,
            {"models": "fixture"},
        )
        self.assertFalse(
            result["simulator_models"][0]["engine_outputs_equivalent"]
        )
        statuses = {
            row["status"]
            for row in metric_rows
            if row["comparison_class"] == "simulator_implementation_equivalence"
        }
        self.assertEqual(
            statuses, {"non_equivalent_or_incomparable_engine_outputs"}
        )

    def test_partial_optional_external_input_stays_incomplete(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            clocking = root / "walter2024"
            gold = root / "gold_subset"
            write_json(
                clocking / "summary.json",
                {
                    "all_equivalent": True,
                    "equivalent_measurements": 10,
                    "raw_measurements": 10,
                },
            )
            inventory: list[dict[str, object]] = []
            metric_rows: list[dict[str, str]] = []
            result = aggregator.summarize_external(
                root,
                clocking,
                gold,
                None,
                inventory,
                metric_rows,
            )
            walter = result["same_machine_context"][0]
            self.assertFalse(walter["available"])
            self.assertEqual(walter["input_status"], "partial")
            self.assertFalse(
                any(
                    row["record_id"] == "fiction_determine_clocking_walter2024"
                    for row in metric_rows
                )
            )

    def test_missing_optional_physical_and_fiction_inputs_do_not_abort(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifacts = root / "artifacts"
            clock = artifacts / "clock"
            rtl = artifacts / "rtl"
            paper = artifacts / "paper"
            output = artifacts / "master"

            write_json(
                clock / "summary.json",
                {
                    "schema": "ifcn.sequential_clock_experiment.v2",
                    "correctness": {},
                    "ii_ablation": {},
                    "edge_scaling": [],
                },
            )
            write_json(
                clock / "validated_summary.json",
                {
                    "schema": "ifcn.sequential_clock_experiment.validated.v1",
                    "validation": {"passed": True},
                    "exact_vs_modulo": {
                        "cases": 2,
                        "exact_sat": 1,
                        "exact_unsat": 1,
                        "modulo_sat": 2,
                        "false_accept": 1,
                        "false_reject": 0,
                        "false_accept_rate_among_exact_unsat": 1.0,
                        "mismatches": 0,
                    },
                    "ii_ablation": {
                        "cases": 2,
                        "adaptive_sat": 2,
                        "fixed_ii4_sat": 1,
                        "adaptive_only_sat": 1,
                        "fixed_only_sat": 0,
                        "sat_recovery_over_fixed": 1.0,
                        "mismatches": 0,
                    },
                },
            )
            write_json(
                rtl / "summary.json",
                {
                    "schema": "ifcn.sequential-rtl-experiments.v1",
                    "scope": "test fixture",
                    "records": [
                        {
                            "design": "toggle_ff",
                            "state_bits": 1,
                            "comb_nodes": 1,
                            "transition_check": {
                                "cases": 4,
                                "mismatches": 0,
                                "status": "pass",
                            },
                            "variants": [
                                {
                                    "name": "cyclic_z3_adaptive",
                                    "status": "success",
                                    "duration_seconds": 0.1,
                                    "metrics": {
                                        "initiation_interval": 4,
                                        "nodes": 3,
                                        "routes": 3,
                                        "feedback_routes": 1,
                                        "mapped_qca_cells": 20,
                                        "bbox_area": 12,
                                        "directed_cycle_present": True,
                                        "mapping_drc": True,
                                        "phase_backend": "external_z3",
                                        "physical_state_signoff": "not_characterized",
                                    },
                                    "layout": "layout.ifcn",
                                    "tex": "layout.tex",
                                }
                            ],
                        }
                    ],
                    "aggregate": [],
                },
            )
            paper.mkdir(parents=True)
            fieldnames = [
                "benchmark_id",
                "state_element",
                "temporal_relation",
                "status",
                "expectation_met",
                "failed_stage",
                "converter_status",
                "pnr_status",
                "state_boundaries",
                "initiation_interval",
                "nodes",
                "routes",
                "feedback_routes",
                "directed_cycle_present",
                "physical_state_signoff",
                "mapped_qca_cells",
                "duration_seconds",
                "latex",
            ]
            with (paper / "summary.csv").open(
                "w", newline="", encoding="utf-8"
            ) as handle:
                writer = csv.DictWriter(handle, fieldnames=fieldnames)
                writer.writeheader()
                writer.writerow(
                    {
                        "benchmark_id": "paper/sample",
                        "state_element": "dff",
                        "temporal_relation": "sampled_state_only",
                        "status": "pass",
                        "expectation_met": "True",
                        "converter_status": "success",
                        "pnr_status": "success",
                        "state_boundaries": 1,
                        "initiation_interval": 4,
                        "nodes": 3,
                        "routes": 3,
                        "feedback_routes": 1,
                        "directed_cycle_present": "True",
                        "physical_state_signoff": "not_characterized",
                        "mapped_qca_cells": 20,
                        "duration_seconds": 0.1,
                    }
                )

            args = aggregator.parse_args(
                [
                    "--artifacts-root",
                    str(artifacts),
                    "--clock-dir",
                    str(clock),
                    "--rtl-dir",
                    str(rtl),
                    "--paper-dir",
                    str(paper),
                    "--output-dir",
                    str(output),
                ]
            )
            summary = aggregator.aggregate(args)

            external = summary["sections"]["external"]
            self.assertFalse(external["head_to_head_speedup_computed"])
            self.assertEqual(
                [row["available"] for row in external["same_machine_context"]],
                [False, False],
            )
            self.assertEqual(
                summary["sections"]["rtl_auto_pnr"]["transition_equivalence"],
                {
                    "total_designs": 1,
                    "designs_checked": 1,
                    "unchecked_designs": 0,
                    "transition_vectors": 4,
                    "mismatches": 0,
                    "nonpass_designs": 0,
                    "zero_case_designs": 0,
                    "all_pass": True,
                },
            )
            self.assertEqual(
                summary["sections"]["physical_energy_exploratory"]
                ["physical_state_signoff_characterized_cases"],
                0,
            )
            self.assertTrue((output / "summary.csv").is_file())
            self.assertTrue((output / "summary.json").is_file())
            self.assertIn(
                r"100.0\%",
                (output / "tables/correctness_ablation.tex").read_text(
                    encoding="utf-8"
                ),
            )
            with (output / "summary.csv").open(encoding="utf-8") as handle:
                reported_rows = [
                    row
                    for row in csv.DictReader(handle)
                    if row["section"] == "external_reported_only"
                ]
            self.assertFalse(
                any(
                    row["metric"] in {"speedup", "runtime_ratio"}
                    for row in reported_rows
                )
            )

            validated_path = clock / "validated_summary.json"
            invalidated = json.loads(validated_path.read_text(encoding="utf-8"))
            invalidated["validation"]["passed"] = False
            write_json(validated_path, invalidated)
            with self.assertRaisesRegex(ValueError, "clock validation failed"):
                aggregator.aggregate(args)


if __name__ == "__main__":
    unittest.main()
