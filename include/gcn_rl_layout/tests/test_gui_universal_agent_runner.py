import importlib.util
import os
from pathlib import Path
import tempfile
import unittest


RUNNER_PATH = Path(__file__).resolve().parents[1] / "scripts" / "gui_universal_agent_runner.py"
SPEC = importlib.util.spec_from_file_location("gui_universal_agent_runner", RUNNER_PATH)
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)


class CheckpointResolutionTest(unittest.TestCase):
    def test_auto_selects_newest_exact_checkpoint(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            older = Path(temp_dir) / "older" / "universal_graph_ppo_best_exact.pt"
            newer = Path(temp_dir) / "newer" / "universal_graph_ppo_best_exact.pt"
            older.parent.mkdir()
            newer.parent.mkdir()
            older.write_bytes(b"old")
            newer.write_bytes(b"new")
            os.utime(older, (10, 10))
            os.utime(newer, (20, 20))

            self.assertEqual(
                runner.resolve_checkpoint("auto", results_root=temp_dir),
                str(newer.resolve()),
            )

    def test_explicit_checkpoint_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            checkpoint = Path(temp_dir) / "model.pt"
            checkpoint.write_bytes(b"model")
            self.assertEqual(
                runner.resolve_checkpoint(str(checkpoint)),
                str(checkpoint.resolve()),
            )


class CandidateSelectionTest(unittest.TestCase):
    @staticmethod
    def candidate(*, legal, area, field_index=0, trial_index=0):
        return {
            "legal": legal,
            "area": area,
            "failed_edges": 0 if legal else 2,
            "direction_violations": 0,
            "clock_violations": 0,
            "width": 10,
            "height": 10,
            "cost": area,
            "field_index": field_index,
            "trial_index": trial_index,
        }

    def test_legality_precedes_area(self):
        legal = self.candidate(legal=True, area=200)
        smaller_but_illegal = self.candidate(legal=False, area=10)
        self.assertLess(
            runner.candidate_selection_key(legal),
            runner.candidate_selection_key(smaller_but_illegal),
        )

    def test_area_orders_legal_candidates(self):
        smaller = self.candidate(legal=True, area=80)
        larger = self.candidate(legal=True, area=120)
        self.assertLess(
            runner.candidate_selection_key(smaller),
            runner.candidate_selection_key(larger),
        )


class CommandLineDefaultsTest(unittest.TestCase):
    def test_policy_is_deterministic_by_default(self):
        args = runner.parse_args(["--benchmark", "input.v", "--output-dir", "out"])
        self.assertTrue(args.deterministic)
        self.assertTrue(args.require_legal)
        self.assertTrue(args.clock_aligned_start)
        self.assertTrue(args.allow_exact_memory_retrieval)
        self.assertEqual(args.checkpoint, "auto")

    def test_clock_alignment_and_exact_retrieval_can_be_disabled(self):
        args = runner.parse_args(
            [
                "--benchmark",
                "input.v",
                "--output-dir",
                "out",
                "--no-clock-aligned-start",
                "--no-allow-exact-memory-retrieval",
            ]
        )
        self.assertFalse(args.clock_aligned_start)
        self.assertFalse(args.allow_exact_memory_retrieval)


if __name__ == "__main__":
    unittest.main()
