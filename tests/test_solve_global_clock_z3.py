#!/usr/bin/env python3
"""Small exact-oracle checks for the optional Z3 clock backend."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import solve_global_clock_z3 as backend  # noqa: E402


class ScriptedSolver:
    """Delegate expressions to Z3 while controlling check() outcomes."""

    def __init__(self, z3, status: str) -> None:
        self.z3 = z3
        self.status = status
        self.delegate = z3.Solver()

    def set(self, **options) -> None:
        self.delegate.set(**options)

    def add(self, expression) -> None:
        self.delegate.add(expression)

    def check(self):
        if self.status == "unknown":
            return self.z3.unknown
        if self.status == "unsat":
            return self.z3.unsat
        if self.status == "sat":
            actual = self.delegate.check()
            if actual != self.z3.sat:
                raise AssertionError(f"scripted SAT problem is actually {actual}")
            return actual
        raise AssertionError(f"unsupported scripted status {self.status}")

    def reason_unknown(self) -> str:
        return "scripted timeout"

    def model(self):
        return self.delegate.model()


class ScriptedZ3:
    def __init__(self, z3, statuses: list[str]) -> None:
        self.z3 = z3
        self.statuses = iter(statuses)

    def Solver(self) -> ScriptedSolver:
        try:
            status = next(self.statuses)
        except StopIteration as error:
            raise AssertionError("solve requested more scripted solvers") from error
        return ScriptedSolver(self.z3, status)

    def __getattr__(self, name):
        return getattr(self.z3, name)


def toggle_problem(edges: int) -> dict:
    occurrences = [
        {"id": f"o{i}", "clock_resource": f"c{i}", "epoch_variable": f"e{i}"}
        for i in range(edges + 1)
    ]
    return {
        "schema": "ifcn.global-clock-problem.v1",
        "occurrence_granularity": "tile",
        "phase_count": 4,
        "max_consecutive_same_phase_occurrences": 4,
        "ii_candidates": [4],
        "events": ["q", "d"],
        "clock_resources": [
            {"id": f"c{i}", "sharing": "phase_shared_independent_epochs"}
            for i in range(edges + 1)
        ],
        "occurrences": occurrences,
        "routes": [{
            "id": "feedback", "source_event": "q", "sink_event": "d",
            "iteration_distance": 0,
            "occurrences": [item["id"] for item in occurrences],
        }],
        "timing_arcs": [{
            "id": "state", "source_event": "d", "sink_event": "q",
            "iteration_distance": 1, "latency_epochs": 0,
        }],
        "anchors": [{"event": "q", "epoch": 0}],
    }


def anchored_route_problem(edges: int, sink_epoch: int, max_run: int = 4) -> dict:
    occurrences = [
        {"id": f"o{i}", "clock_resource": f"c{i}", "epoch_variable": f"e{i}"}
        for i in range(edges + 1)
    ]
    return {
        "schema": "ifcn.global-clock-problem.v1",
        "phase_count": 4,
        "max_consecutive_same_phase_cells": max_run,
        "ii_candidates": [4],
        "events": ["source", "sink"],
        "clock_resources": [
            {"id": f"c{i}", "sharing": "phase_shared_independent_epochs"}
            for i in range(edges + 1)
        ],
        "occurrences": occurrences,
        "routes": [{
            "id": "route", "source_event": "source", "sink_event": "sink",
            "iteration_distance": 0,
            "occurrences": [item["id"] for item in occurrences],
        }],
        "timing_arcs": [],
        "anchors": [
            {"event": "source", "epoch": 0},
            {"event": "sink", "epoch": sink_epoch},
        ],
    }


class Z3ClockBackendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        try:
            cls.z3 = backend.load_z3(backend.DEFAULT_Z3_ROOT)
        except backend.SolveError as error:
            raise unittest.SkipTest(str(error))

    def test_four_edges_close_one_period(self) -> None:
        result = backend.solve(toggle_problem(4), self.z3, 5_000)
        self.assertEqual(result["status"], "SAT")
        self.assertEqual(result["ii"], 4)
        self.assertEqual(result["event_epoch"], {"q": 0, "d": 4})
        self.assertIn("status\tSAT", backend.render_solution(result))

    def test_three_edges_cannot_close_one_period(self) -> None:
        result = backend.solve(toggle_problem(3), self.z3, 5_000)
        self.assertEqual(result["status"], "UNSAT")

    def test_same_phase_run_counts_occurrences_not_edges(self) -> None:
        # Four same-phase occurrences (three hold edges) are legal; a fifth
        # is not.  In P&R each occurrence is one clock tile.
        legal = backend.solve(anchored_route_problem(3, 0), self.z3, 5_000)
        illegal = backend.solve(anchored_route_problem(4, 0), self.z3, 5_000)
        self.assertEqual(legal["status"], "SAT")
        self.assertEqual(illegal["status"], "UNSAT")

    def test_stale_qca_cell_occurrence_granularity_is_rejected(self) -> None:
        problem = toggle_problem(4)
        problem["occurrence_granularity"] = "qca_cell"
        with self.assertRaisesRegex(
            backend.SolveError, "occurrence granularity"
        ):
            backend.solve(problem, self.z3, 5_000)

    def test_shared_resource_requires_equal_modulo_phase(self) -> None:
        compatible = anchored_route_problem(4, 4, max_run=0)
        compatible["occurrences"][-1]["clock_resource"] = "c0"
        self.assertEqual(
            backend.solve(compatible, self.z3, 5_000)["status"], "SAT"
        )

        conflict = anchored_route_problem(1, 1, max_run=0)
        conflict["occurrences"][-1]["clock_resource"] = "c0"
        self.assertEqual(
            backend.solve(conflict, self.z3, 5_000)["status"], "UNSAT"
        )

    def test_signed_bitvector_model_preserves_negative_epochs(self) -> None:
        problem = anchored_route_problem(1, 0, max_run=0)
        problem["anchors"] = [
            {"event": "source", "epoch": -1},
            {"event": "sink", "epoch": 0},
        ]

        result = backend.solve(problem, self.z3, 5_000)

        self.assertEqual(result["status"], "SAT")
        self.assertEqual(result["event_epoch"], {"source": -1, "sink": 0})
        self.assertEqual(result["occurrence_epoch"]["o0"], -1)
        self.assertEqual(result["clock_resource_phase"]["c0"], 3)

    def test_non_power_of_two_phase_count_uses_exact_int_fallback(self) -> None:
        problem = anchored_route_problem(3, 3, max_run=0)
        problem["phase_count"] = 3
        problem["ii_candidates"] = [3]
        problem["occurrences"][-1]["clock_resource"] = "c0"

        result = backend.solve(problem, self.z3, 5_000)

        self.assertEqual(result["status"], "SAT")
        self.assertEqual(result["clock_resource_phase"]["c0"], 0)

    def test_exclusive_resource_rejects_independent_epochs(self) -> None:
        problem = anchored_route_problem(1, 1, max_run=0)
        problem["occurrences"][-1]["clock_resource"] = "c0"
        problem["clock_resources"][0]["sharing"] = "exclusive_or_aliased"
        with self.assertRaises(backend.SolveError):
            backend.solve(problem, self.z3, 5_000)

    def test_unknown_ii_does_not_hide_later_sat(self) -> None:
        problem = toggle_problem(4)
        problem["ii_candidates"] = [3, 4]
        scripted_z3 = ScriptedZ3(self.z3, ["unknown", "sat"])

        result = backend.solve(problem, scripted_z3, 5_000)

        self.assertEqual(result["status"], "SAT")
        self.assertEqual(result["ii"], 4)
        self.assertEqual(
            [(attempt["ii"], attempt["status"]) for attempt in result["attempted"]],
            [(3, "unknown"), (4, "sat")],
        )
        self.assertEqual(result["attempted"][0]["reason"], "scripted timeout")

    def test_unknown_is_reported_after_all_ii_candidates(self) -> None:
        problem = toggle_problem(4)
        problem["ii_candidates"] = [3, 4]
        scripted_z3 = ScriptedZ3(self.z3, ["unknown", "unsat"])

        result = backend.solve(problem, scripted_z3, 5_000)

        self.assertEqual(result["status"], "UNKNOWN")
        self.assertEqual(
            [(attempt["ii"], attempt["status"]) for attempt in result["attempted"]],
            [(3, "unknown"), (4, "unsat")],
        )
        self.assertEqual(result["reason"], "ii=3: scripted timeout")

    def test_unsat_requires_all_ii_candidates_to_be_unsat(self) -> None:
        problem = toggle_problem(4)
        problem["ii_candidates"] = [3, 4]
        scripted_z3 = ScriptedZ3(self.z3, ["unsat", "unsat"])

        result = backend.solve(problem, scripted_z3, 5_000)

        self.assertEqual(result["status"], "UNSAT")
        self.assertEqual(
            [(attempt["ii"], attempt["status"]) for attempt in result["attempted"]],
            [(3, "unsat"), (4, "unsat")],
        )


if __name__ == "__main__":
    unittest.main()
