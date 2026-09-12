import math
import os
import sys
import unittest


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.stochastic_clock import (  # noqa: E402
    ClockEvaluation,
    ClockFieldSpec,
    aggregate_clock_evaluations,
    iter_packed_phase_blocks,
    sample_clock_field,
    validate_causal_field,
)


class StochasticClockFieldTest(unittest.TestCase):
    def test_causal_band_field_is_deterministic_and_valid(self):
        spec = ClockFieldSpec(
            seed=20260710,
            mode="stochastic-bands",
            primary_axis="x",
            primary_direction=1,
            secondary_direction=-1,
            secondary_advance_probability=0.45,
        )
        first = sample_clock_field((-3, -2, 10, 9), spec)
        second = sample_clock_field((-3, -2, 10, 9), spec)

        self.assertEqual(first.field_hash, second.field_hash)
        self.assertEqual(first.stages, second.stages)
        self.assertTrue(validate_causal_field(first)["valid"])
        self.assertTrue(first.causal)
        self.assertEqual(first.bounds, (-4, -4, 11, 11))

        min_x, min_y, max_x, max_y = first.bounds
        for x in range(min_x, max_x):
            for y in range(min_y, max_y + 1):
                self.assertEqual(first.stage_at((x + 1, y)) - first.stage_at((x, y)), 1)
                self.assertEqual(
                    (first.phase_at((x + 1, y)) - first.phase_at((x, y))) % 4,
                    1,
                )

    def test_packed_blocks_round_trip_all_phases(self):
        field = sample_clock_field(
            (0, 0, 7, 7),
            ClockFieldSpec(seed=17, mode="diagonal"),
        )
        block_count = 0
        for (origin_x, origin_y), packed in iter_packed_phase_blocks(field):
            block_count += 1
            for local_y in range(4):
                row_byte = (packed >> (8 * (3 - local_y))) & 0xFF
                for local_x in range(4):
                    decoded = (row_byte >> (2 * local_x)) & 0x3
                    self.assertEqual(
                        decoded,
                        field.phase_at((origin_x + local_x, origin_y + local_y)),
                    )
        self.assertEqual(block_count, 4)

    def test_raw_field_is_explicitly_non_causal(self):
        field = sample_clock_field(
            (0, 0, 7, 7),
            ClockFieldSpec(seed=5, mode="raw"),
        )
        self.assertFalse(field.causal)
        self.assertFalse(validate_causal_field(field)["valid"])

    def test_robust_objective_prefers_reliable_layout(self):
        reliable = aggregate_clock_evaluations(
            [
                ClockEvaluation(legal=True, cost=100.0 + seed, area=20.0)
                for seed in range(10)
            ],
            cvar_alpha=0.9,
        )
        fragile = aggregate_clock_evaluations(
            [
                ClockEvaluation(
                    legal=seed != 9,
                    cost=80.0,
                    failed_edges=int(seed == 9),
                    area=18.0,
                )
                for seed in range(10)
            ],
            cvar_alpha=0.9,
        )

        self.assertEqual(reliable.success_rate, 1.0)
        self.assertEqual(fragile.success_rate, 0.9)
        self.assertLess(reliable.selection_key(), fragile.selection_key())
        self.assertGreater(fragile.robust_loss, reliable.robust_loss)
        self.assertFalse(math.isnan(reliable.cvar_cost))


if __name__ == "__main__":
    unittest.main()

