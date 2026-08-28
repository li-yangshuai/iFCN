#!/usr/bin/env python3
"""Unit tests for the paired sequential energy experiment boundary."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = REPOSITORY_ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

import benchmark_sequential_paired_energy as paired  # noqa: E402


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def make_pair(root: Path, label: str, *, cut: str = "module x; endmodule\n", ii: int = 4) -> Path:
    design = root / label / "toggle_ff"
    variant = design / "cyclic_z3_adaptive"
    variant.mkdir(parents=True)
    (design / "cut.v").write_text(cut, encoding="utf-8")
    write_json(
        design / "state.json",
        {
            "source_module": "toggle_ff",
            "cut_dag": {
                "primary_inputs": [{"event": "i0", "port": "rst"}],
            },
            "state_boundaries": [{"data_event": "d0"}],
        },
    )
    (variant / "layout.ifcn").write_text("[LAYOUT]\n", encoding="utf-8")
    write_json(
        variant / "layout.ifcn.json",
        {
            "status": "success_physical_feedback_uncharacterized_state",
            "directed_cycle_present": True,
            "mapping_drc": True,
            "initiation_interval": ii,
            "bbox_area": 8,
            "route_steps": 7,
        },
    )
    return root / label


class PairedSequentialEnergyTest(unittest.TestCase):
    def test_pair_validation_checks_cut_state_and_ii(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            raw = make_pair(root, "raw")
            final = make_pair(root, "final")
            result = paired.validate_pair(raw, final, "toggle_ff", "cyclic_z3_adaptive")
            self.assertEqual(result.initiation_interval, 4)
            self.assertEqual(len(result.cut_sha256), 64)

            (final / "toggle_ff" / "cut.v").write_text(
                "module changed; endmodule\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "cut.v hashes differ"):
                paired.validate_pair(raw, final, "toggle_ff", "cyclic_z3_adaptive")

    def test_workload_is_one_plus_eight_plus_four_plus_two_drain(self) -> None:
        state = {
            "cut_dag": {"primary_inputs": [{"event": "i0", "port": "rst"}]},
            "state_boundaries": [{"data_event": "d0"}],
        }
        workload = paired.validate_workload(
            "toggle_ff", paired.DEFAULT_WORKLOADS["toggle_ff"], state
        )
        self.assertEqual(len(workload["startup"]), 1)
        self.assertEqual(len(workload["warmup"]), 8)
        self.assertEqual(len(workload["measured"]), 4)
        self.assertEqual(len(workload["drain"]), 2)
        self.assertEqual(len(workload["rows"]), 15)
        self.assertEqual(workload["event_port"], [("i0", "rst")])

    def test_enable_warmup_repeats_the_measured_pattern_twice(self) -> None:
        workload = paired.DEFAULT_WORKLOADS["enable_hold_ff"]
        self.assertEqual(workload["warmup"], workload["measured"] * 2)
        self.assertEqual(
            workload["drain"],
            [{"rst": 0, "en": 0, "d": 1}] * 2,
        )

    def test_drain_must_hold_the_final_measured_vector(self) -> None:
        state = {
            "cut_dag": {"primary_inputs": [{"event": "i0", "port": "rst"}]},
            "state_boundaries": [{"data_event": "d0"}],
        }
        workload = json.loads(json.dumps(paired.DEFAULT_WORKLOADS["toggle_ff"]))
        workload["drain"][-1] = {"rst": 1}
        with self.assertRaisesRegex(ValueError, "drain rows must repeat"):
            paired.validate_workload("toggle_ff", workload, state)

    def test_zero_input_design_is_excluded_without_running_energy(self) -> None:
        state = {
            "source_module": "johnson4_free_running",
            "cut_dag": {"primary_inputs": []},
            "state_boundaries": [
                {"data_event": "d0"},
                {"data_event": "d1"},
                {"data_event": "d2"},
                {"data_event": "d3"},
            ],
        }
        normalized = paired.validate_workload(
            "johnson4_free_running",
            paired.DEFAULT_WORKLOADS["johnson4_free_running"],
            state,
        )
        self.assertEqual(
            paired.workload_exclusion_reason(normalized),
            "excluded_no_state_initialization",
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pair = paired.PairInputs(
                design="johnson4_free_running",
                raw_layout=root / "raw.ifcn",
                final_layout=root / "final.ifcn",
                raw_report={"bbox_area": 21},
                final_report={"bbox_area": 15},
                state=state,
                cut_sha256="a" * 64,
                state_sha256="b" * 64,
                initiation_interval=4,
            )
            args = SimpleNamespace(validate_only=False, clock_period=1.0e-11)
            record = paired.make_design_record(
                pair,
                paired.DEFAULT_WORKLOADS["johnson4_free_running"],
                root / "out",
                args,
            )
        self.assertEqual(record["status"], "excluded_no_state_initialization")
        self.assertNotIn("paired_result", record)

    def test_parser_and_ii8_window_use_only_last_four_updates(self) -> None:
        rows = []
        for cycle in range(2, 31):
            rows.append(f"{cycle},{cycle},0,0,{cycle},{cycle}")
        report = "\n".join(
            [
                "[ENERGY_ANALYSIS]",
                "available=TRUE",
                "cycle_count=29",
                "total_bath_eV=1",
                "average_bath_eV=1",
                "[PER_CYCLE]",
                "cycle,E_bath_eV,E_clk_eV,E_io_eV,E_error_eV,E_bath_clk_eV",
                *rows,
                "[#PER_CYCLE]",
            ]
        )
        parsed = paired.parse_energy_report_text(report)
        window = paired.measured_bath_window(parsed["cycles"], 8)
        self.assertEqual(window["first_measured_cycle"], 19)
        self.assertEqual(window["last_measured_cycle"], 26)
        self.assertEqual(window["update_cycle_indices"], [[19, 20], [21, 22], [23, 24], [25, 26]])
        self.assertEqual(window["bath_eV_per_update"], [39.0, 43.0, 47.0, 51.0])
        self.assertEqual(window["mean_bath_eV_per_update"], 45.0)

    def test_window_rejects_a_measurement_that_reaches_partial_tail(self) -> None:
        cycles = [
            {"cycle": cycle, "E_bath_eV": 1.0}
            for cycle in range(2, 14)
        ]
        with self.assertRaisesRegex(ValueError, "post-measurement guard cycle"):
            paired.measured_bath_window(cycles, 4)

    def test_power_and_convergence_use_bath_energy_per_update(self) -> None:
        power = paired.bath_power_watts(1.0, 1.0e-11)
        self.assertAlmostEqual(power, 1.602176634e-8)
        summary = paired.convergence_summary(
            [
                {"time_step_s": 1.25e-16, "mean_bath_eV_per_update": 1.2},
                {"time_step_s": 6.25e-17, "mean_bath_eV_per_update": 1.01},
                {"time_step_s": 3.125e-17, "mean_bath_eV_per_update": 1.0},
            ],
            0.011,
        )
        self.assertTrue(summary["converged"])
        self.assertAlmostEqual(summary["relative_change_last_two"], 0.01)


if __name__ == "__main__":
    unittest.main()
