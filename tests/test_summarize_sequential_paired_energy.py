#!/usr/bin/env python3
"""Tests for strict Richardson post-processing of paired energy sweeps."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = REPOSITORY_ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import summarize_sequential_paired_energy as post  # noqa: E402


TIME_STEPS = [8.0e-17, 4.0e-17, 2.0e-17, 1.0e-17]


def first_order_series(limit: float, linear: float, quadratic: float) -> list[float]:
    normalized = [8.0, 4.0, 2.0, 1.0]
    return [limit + linear * h + quadratic * h * h for h in normalized]


def write_runner_summary(
    path: Path,
    indices: list[int],
    *,
    raw_energies: list[float],
    final_energies: list[float],
    design: str = "toggle_ff",
    acceptance_time_s: float = 1.0e-11,
) -> None:
    def layout(label: str, energies: list[float]) -> dict[str, object]:
        return {
            "layout_metrics": {
                "bbox_area": 14 if label == "raw" else 8,
                "route_steps": 13 if label == "raw" else 7,
                "mapped_layer_cell_records": 70 if label == "raw" else 40,
                "crossover_segments": 0,
            },
            "qca_cells": 70 if label == "raw" else 40,
            "energy_runs": [
                {
                    "time_step_s": TIME_STEPS[index],
                    "mean_bath_eV_per_update": energies[index],
                    "measurement": {"all_bath_values_nonnegative": energies[index] >= 0},
                }
                for index in indices
            ],
        }

    value = {
        "schema": post.EXPECTED_SCHEMA,
        "clock_period_s": 1.0e-11,
        "records": [
            {
                "design": design,
                "status": "completed_unconverged",
                "initiation_interval": 4,
                "acceptance_time_s": acceptance_time_s,
                "pair_validation": {
                    "cut_v_sha256": "a" * 64,
                    "state_json_sha256": "b" * 64,
                },
                "raw": layout("raw", raw_energies),
                "final": layout("final", final_energies),
            }
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


class SequentialEnergyPostprocessTest(unittest.TestCase):
    def setUp(self) -> None:
        self.raw = first_order_series(10.0, 0.1, 0.001)
        self.final = first_order_series(6.0, 0.06, 0.0005)

    def test_merges_scattered_steps_and_accepts_first_order_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            coarse = root / "coarse" / "summary.json"
            fine = root / "fine" / "summary.json"
            write_runner_summary(
                coarse, [0, 1, 2], raw_energies=self.raw, final_energies=self.final
            )
            write_runner_summary(
                fine, [3], raw_energies=self.raw, final_energies=self.final
            )
            summary = post.build_summary([coarse, fine])

        record = summary["records"][0]
        self.assertEqual(record["status"], "numerically_accepted")
        self.assertTrue(summary["claim_boundary"]["numerical_acceptance_only"])
        self.assertEqual(record["selected_time_steps_s"], TIME_STEPS)
        self.assertEqual(len(record["raw"]["richardson_extrapolates_eV_per_update"]), 3)
        self.assertTrue(all(0.8 <= p <= 1.2 for p in record["raw"]["local_observed_orders"]))
        self.assertLess(record["raw"]["last_two_extrapolates_relative_difference"], 0.01)
        self.assertAlmostEqual(record["raw"]["richardson_estimate_eV_per_update"], 9.998)
        self.assertAlmostEqual(record["raw"]["conservative_error_eV_per_update"], 0.103)
        self.assertGreater(record["raw"]["bath_power_pW"], 0)
        self.assertAlmostEqual(record["paired_result"]["energy_reduction_percent"], 40.0, places=2)

    def test_sign_change_and_negative_energy_are_rejected(self) -> None:
        sign_change = [10.8, 10.4, 10.45, 10.2]
        negative = [-1.8, -1.4, -1.2, -1.1]
        raw = post.analyze_series(
            [
                {
                    "time_step_s": dt,
                    "mean_bath_eV_per_update": energy,
                    "all_measured_updates_nonnegative": energy >= 0,
                }
                for dt, energy in zip(TIME_STEPS, sign_change)
            ]
        )
        final = post.analyze_series(
            [
                {
                    "time_step_s": dt,
                    "mean_bath_eV_per_update": energy,
                    "all_measured_updates_nonnegative": energy >= 0,
                }
                for dt, energy in zip(TIME_STEPS, negative)
            ]
        )
        self.assertFalse(raw["accepted"])
        self.assertIn("finite_step_differences_change_sign_or_vanish", raw["rejection_reasons"])
        self.assertFalse(final["accepted"])
        self.assertIn("negative_bath_energy", final["rejection_reasons"])

    def test_rejected_pair_does_not_publish_a_reduction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "summary.json"
            write_runner_summary(
                path,
                [0, 1, 2, 3],
                raw_energies=[10.8, 10.4, 10.45, 10.2],
                final_energies=self.final,
            )
            record = post.build_summary([path])["records"][0]
        self.assertEqual(record["status"], "rejected_numerical_qualification")
        self.assertIsNone(record["paired_result"]["energy_reduction_percent"])
        self.assertIsNotNone(
            record["paired_result"]["diagnostic_energy_reduction_percent"]
        )

    def test_missing_fourth_level_remains_explicitly_insufficient(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "summary.json"
            write_runner_summary(
                path, [0, 1, 2], raw_energies=self.raw, final_energies=self.final
            )
            summary = post.build_summary([path])
        self.assertEqual(
            summary["records"][0]["status"],
            "insufficient_four_level_common_chain",
        )
        self.assertEqual(summary["rejected_designs"], 1)

    def test_five_levels_select_the_finest_consecutive_four(self) -> None:
        levels = [1.6e-16, 8.0e-17, 4.0e-17, 2.0e-17, 1.0e-17]
        mapping = {time_step: {} for time_step in levels}
        self.assertEqual(
            post.finest_common_halving_chain(mapping, mapping), levels[1:]
        )

    def test_preserves_no_state_initialization_exclusion(self) -> None:
        merged = {
            "design": "johnson4_free_running",
            "context": {"initiation_interval": 4, "acceptance_time_s": 1.0e-11},
            "labels": {"raw": {}, "final": {}},
            "layout_metrics": {
                "raw": {"bbox_area": 21},
                "final": {"bbox_area": 15},
            },
            "sources": {"summary.json"},
            "runner_statuses": {"excluded_no_state_initialization"},
        }
        record = post.analyze_design(merged)
        self.assertEqual(record["status"], "excluded_no_state_initialization")

    def test_conflicting_duplicate_measurement_is_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "one" / "summary.json"
            second = root / "two" / "summary.json"
            write_runner_summary(
                first, [0], raw_energies=self.raw, final_energies=self.final
            )
            changed = list(self.raw)
            changed[0] += 0.1
            write_runner_summary(
                second, [0], raw_energies=changed, final_energies=self.final
            )
            with self.assertRaisesRegex(ValueError, "conflicting energy"):
                post.build_summary([first, second])

    def test_cli_writes_json_csv_and_latex(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            coarse = root / "input" / "coarse" / "summary.json"
            fine = root / "input" / "fine" / "summary.json"
            output = root / "output"
            write_runner_summary(
                coarse, [0, 1, 2], raw_energies=self.raw, final_energies=self.final
            )
            write_runner_summary(
                fine, [3], raw_energies=self.raw, final_energies=self.final
            )
            code = post.main([str(root / "input"), "--output-dir", str(output)])
            self.assertEqual(code, 0)
            self.assertTrue((output / "summary.json").is_file())
            csv_text = (output / "summary.csv").read_text()
            self.assertIn("raw_bath_power_pW", csv_text)
            self.assertIn("raw_layer_aware_cells", csv_text)
            latex = (output / "summary.tex").read_text()
            self.assertIn(r"toggle\_ff", latex)
            self.assertIn(r"\begin{tabular}", latex)
            self.assertIn(r"70$\to$40", latex)


if __name__ == "__main__":
    unittest.main()
