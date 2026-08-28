#!/usr/bin/env python3
"""Tests for absolute-epoch sequential waveform validation."""

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

import validate_sequential_energy_waveform as validator  # noqa: E402


ENABLE_ROWS = [
    (0, 0, 1),
    (1, 1, 0),
    (0, 0, 0),
    (0, 1, 0),
    (1, 0, 0),
    (1, 1, 0),
    (0, 0, 0),
    (0, 1, 0),
    (1, 0, 0),
    (1, 1, 0),
    (0, 0, 0),
    (0, 1, 0),
    (1, 0, 0),
]


def write_trace(output, label: str, values: list[float]) -> None:
    output.extend(
        [
            "[TRACE]",
            f"data_labels={label}",
            "trace_function=2",
            "drawtrace=TRUE",
            "[TRACE_DATA]",
            " ".join(str(value) for value in values),
            "[#TRACE_DATA]",
            "[#TRACE]",
        ]
    )


def make_enable_fixture(root: Path, *, weak_update: int | None = None) -> dict[str, Path]:
    report = root / "layout.ifcn.json"
    state = root / "state.json"
    stimulus = root / "stimulus.vt"
    waveform = root / "run_energy.rst"
    clock = root / "clock_solution.tsv"

    report.write_text(json.dumps({"initiation_interval": 4}), encoding="utf-8")
    state.write_text(
        json.dumps(
            {
                "source_module": "enable_hold_ff",
                "cut_dag": {
                    "primary_inputs": [
                        {"event": "i0", "port": "d"},
                        {"event": "i1", "port": "en"},
                        {"event": "i2", "port": "rst"},
                    ]
                },
                "state_boundaries": [{"data_event": "d0"}],
            }
        ),
        encoding="utf-8",
    )
    stimulus.write_text(
        "i0,i1,i2\n"
        + "".join(
            f"{d},{enable},{reset}\n" for d, enable, reset in ENABLE_ROWS
        ),
        encoding="utf-8",
    )
    clock.write_text(
        "\n".join(
            [
                "ifcn.global-clock-solution.v1",
                "status\tSAT",
                "phase_count\t4",
                "ii\t4",
                "event\td0\t8",
                "event\ti0\t0",
                "event\ti1\t0",
                "event\ti2\t0",
            ]
        )
        + "\n",
        encoding="utf-8",
    )

    samples_per_slot = 10
    total_slots = len(ENABLE_ROWS) * 4
    probe = [0.0] * (total_slots * samples_per_slot)
    expected = [0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0]
    for update, wanted in enumerate(expected):
        start_slot = update * 4 + 9
        end_slot = update * 4 + 11
        if end_slot > total_slots:
            continue
        value = 0.02 if update == weak_update else 0.9 if wanted else -0.9
        probe[start_slot * samples_per_slot : end_slot * samples_per_slot] = [
            value
        ] * ((end_slot - start_slot) * samples_per_slot)

    lines = [
        "[SIMULATION_OUTPUT]",
        "[SIMULATION_DATA]",
        f"number_samples={len(probe)}",
        "number_of_traces=1",
        "[TRACES]",
    ]
    write_trace(lines, "feedback_probe_d0", probe)
    for phase in range(4):
        write_trace(lines, f"Clock {phase}", [0.0] * len(probe))
    lines.extend(["[#TRACES]", "[#SIMULATION_DATA]", "[#SIMULATION_OUTPUT]"])
    waveform.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return {
        "layout_report_path": report,
        "state_path": state,
        "stimulus_path": stimulus,
        "waveform_path": waveform,
    }


class SequentialEnergyWaveformValidationTest(unittest.TestCase):
    def test_absolute_epoch_aligns_enable_output_across_two_ii_boundaries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = make_enable_fixture(Path(temporary))
            result = validator.validate_waveform(**paths)

        self.assertEqual(result["status"], "diagnostic_pass")
        self.assertEqual(result["compared"], 11)
        self.assertEqual(result["matching"], 11)
        self.assertEqual(result["agreement"], 1.0)
        self.assertEqual(result["weak"], 0)
        self.assertAlmostEqual(result["min_margin"], 0.9)
        probe = result["per_probe"][0]
        self.assertEqual(probe["event_epoch"], 8)
        self.assertEqual(probe["clock_zone"], 0)
        self.assertEqual(probe["observable_updates"], list(range(11)))
        self.assertEqual(probe["unobservable_tail_updates"], 2)
        self.assertEqual(
            probe["observed_logic"], [0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1]
        )

    def test_weak_polarization_is_counted_as_a_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = make_enable_fixture(Path(temporary), weak_update=3)
            result = validator.validate_waveform(**paths)

        self.assertEqual(result["status"], "diagnostic_fail")
        self.assertEqual(result["compared"], 11)
        self.assertEqual(result["matching"], 10)
        self.assertEqual(result["agreement"], 10 / 11)
        self.assertEqual(result["weak"], 1)
        self.assertAlmostEqual(result["min_margin"], 0.02)
        self.assertIsNone(result["per_probe"][0]["observed_logic"][3])


if __name__ == "__main__":
    unittest.main()
