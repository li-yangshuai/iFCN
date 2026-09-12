#!/usr/bin/env python3
"""Correctness boundaries for the sequential RTL experiment runner."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import run_sequential_rtl_experiments as runner  # noqa: E402


class GeometryFallbackStatusTest(unittest.TestCase):
    def test_sat_has_highest_precedence(self) -> None:
        self.assertEqual(
            runner.reduce_geometry_fallback_status(
                ["unknown", "unsat", "success"]
            ),
            "success",
        )

    def test_only_all_unsat_is_unsat(self) -> None:
        self.assertEqual(
            runner.reduce_geometry_fallback_status(["unsat", "unsat"]),
            "unsat",
        )
        self.assertEqual(
            runner.reduce_geometry_fallback_status(["unknown", "unsat"]),
            "unknown",
        )
        self.assertEqual(
            runner.reduce_geometry_fallback_status(["limit", "unsat"]),
            "limit",
        )


class GeometryCandidateCountTest(unittest.TestCase):
    def test_positive_count_is_required(self) -> None:
        self.assertEqual(
            runner.parse_geometry_candidate_count(
                "paper_cyclic_pnr=geometry_ready geometry_candidates=17"
            ),
            17,
        )
        for output in (
            "paper_cyclic_pnr=geometry_ready",
            "geometry_candidates=0",
        ):
            with self.subTest(output=output):
                with self.assertRaises(runner.ExperimentError):
                    runner.parse_geometry_candidate_count(output)

    def test_geometry_ladder_limits_are_configurable(self) -> None:
        arguments = runner.parse_args([
            "--max-geometry-ranks", "23",
            "--geometry-ladder-seconds", "45.5",
        ])
        self.assertEqual(arguments.max_geometry_ranks, 23)
        self.assertEqual(arguments.geometry_ladder_seconds, 45.5)

    def test_same_phase_limit_is_one_to_four_tiles(self) -> None:
        for value in ("0", "5"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(
                    SystemExit, "between 1 and 4 tiles"
                ):
                    runner.main(["--max-same-phase", value])


class LayoutMetricsTest(unittest.TestCase):
    def test_network_ids_are_not_treated_as_layout_coordinates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            layout = Path(temporary) / "layout.ifcn"
            layout.write_text(
                """#nodes info
100, source, input, (2,3);
200, sink, output, (5,7);
#nodes info

#paths info
(100,200): (2,3),(3,3),(4,3),(5,3),(5,4),(5,5),(5,6),(5,7);
#paths info
""",
                encoding="utf-8",
            )

            self.assertEqual(
                runner.layout_metrics(layout),
                {
                    "width": 4,
                    "height": 5,
                    "bbox_area": 20,
                    "routed_steps": 7,
                    "unique_route_sites": 8,
                },
            )


class ReusedArtifactTest(unittest.TestCase):
    def test_cleanup_and_success_gated_exposure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            layout = root / "layout.ifcn"
            report = root / "layout.ifcn.json"
            tex = root / "layout.tex"
            summary = root / "z3_summary.json"
            solution = root / "clock_solution.tsv"
            for path in (layout, report, tex, summary, solution):
                path.write_text("stale", encoding="utf-8")

            self.assertIsNone(
                runner.artifact_reference(layout, "unsat", root=root)
            )
            runner.clear_stage_artifacts(
                (layout, report, tex, summary, solution)
            )
            self.assertFalse(any(
                path.exists() for path in (layout, report, tex, summary, solution)
            ))

            layout.write_text("fresh", encoding="utf-8")
            self.assertEqual(
                runner.artifact_reference(layout, "success", root=root),
                "layout.ifcn",
            )
            self.assertIsNone(
                runner.artifact_reference(layout, "failed", root=root)
            )

    def test_failed_stage_does_not_read_stale_json(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            summary = Path(temporary) / "z3_summary.json"
            summary.write_text("not current valid json", encoding="utf-8")
            self.assertIsNone(runner.load_json_artifact(summary, "unsat"))
            summary.write_text('{"status": "SAT"}', encoding="utf-8")
            self.assertEqual(
                runner.load_json_artifact(summary, "success"),
                {"status": "SAT"},
            )


class SampledStateBenchmarkOracleTest(unittest.TestCase):
    @staticmethod
    def _seqir(
        width: int,
        nodes: list[dict[str, object]],
        register_data: list[str],
    ) -> dict[str, object]:
        return {
            "combinational_nodes": nodes,
            "registers": [
                {
                    "q": runner.indexed_signal("q", bit, width),
                    "d": register_data[bit],
                }
                for bit in range(width)
            ],
        }

    def test_complete_tff_oracle_is_exhaustive(self) -> None:
        seqir = self._seqir(
            1,
            [
                {
                    "op": "and",
                    "inputs": {"A": "logical_clk", "B": "t"},
                    "output": "toggle_enable",
                },
                {
                    "op": "xor",
                    "inputs": {"A": "q", "B": "toggle_enable"},
                    "output": "next_q",
                },
            ],
            ["next_q"],
        )

        self.assertEqual(
            runner.verify_transition_relation("tff_sampled", seqir),
            {"status": "pass", "cases": 8, "mismatches": 0, "examples": []},
        )

    def test_active_low_dominant_reset_dff_oracle_is_exhaustive(self) -> None:
        seqir = self._seqir(
            1,
            [
                {
                    "op": "mux",
                    "inputs": {"A": "q", "B": "d", "S": "logical_clk"},
                    "output": "capture_or_hold",
                },
                {
                    "op": "mux",
                    "inputs": {"A": "const.0", "B": "capture_or_hold", "S": "reset_n"},
                    "output": "next_q",
                },
            ],
            ["next_q"],
        )

        self.assertEqual(
            runner.verify_transition_relation("dff_reset_n_sampled", seqir),
            {"status": "pass", "cases": 16, "mismatches": 0, "examples": []},
        )

    def test_three_bit_siso_oracle_preserves_bit_order(self) -> None:
        nodes = []
        sources = ["serial_in", "q[0]", "q[1]"]
        for bit, source in enumerate(sources):
            nodes.append({
                "op": "mux",
                "inputs": {
                    "A": f"q[{bit}]",
                    "B": source,
                    "S": "logical_clk",
                },
                "output": f"next_q[{bit}]",
            })
        seqir = self._seqir(3, nodes, [f"next_q[{bit}]" for bit in range(3)])

        self.assertEqual(
            runner.verify_transition_relation("shift_register3_sampled", seqir),
            {"status": "pass", "cases": 32, "mismatches": 0, "examples": []},
        )

    def test_all_three_rtl_sources_are_runner_eligible(self) -> None:
        rtl_root = ROOT / "tests" / "benchmarks_f" / "SEQUENTIAL" / "rtl_v"
        references = runner.reference_functions()
        for design in (
            "tff_sampled",
            "dff_reset_n_sampled",
            "shift_register3_sampled",
        ):
            with self.subTest(design=design):
                rtl = rtl_root / f"{design}.v"
                self.assertTrue(rtl.is_file())
                self.assertEqual(runner.module_name(rtl), design)
                self.assertIn(design, references)


if __name__ == "__main__":
    unittest.main()
