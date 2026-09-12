import os
import sys
import unittest
from types import SimpleNamespace


MAIN_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../src/algorithm/main")
)
if MAIN_ROOT not in sys.path:
    sys.path.insert(0, MAIN_ROOT)

import pretrain_ifcn_memory_policy as pretrain  # noqa: E402


def record(topology_hash, layout_hash, area=10.0):
    return SimpleNamespace(
        topology_hash=topology_hash,
        layout_hash=layout_hash,
        quality=SimpleNamespace(area=area),
    )


class IFCNPretrainProtocolTest(unittest.TestCase):
    def test_metrics_are_macro_averaged_by_topology(self):
        metrics = pretrain.aggregate_metrics(
            [
                ("a", {"loss": 0.0, "accuracy": 0.0}),
                ("a", {"loss": 2.0, "accuracy": 1.0}),
                ("b", {"loss": 10.0, "accuracy": 1.0}),
            ]
        )
        self.assertEqual(metrics["loss"], 5.5)
        self.assertEqual(metrics["accuracy"], 0.75)
        self.assertEqual(metrics["evaluated_samples"], 3.0)
        self.assertEqual(metrics["evaluated_topologies"], 2.0)

    def test_epoch_sampling_assigns_equal_weight_to_each_topology(self):
        records = [
            record("a", "a0"),
            record("a", "a1"),
            record("a", "a2"),
            record("b", "b0"),
        ]
        selected = pretrain.topology_balanced_epoch_records(
            records,
            samples_per_topology=4,
            seed=31,
        )
        self.assertEqual(len(selected), 8)
        self.assertEqual(sum(item.topology_hash == "a" for item in selected), 4)
        self.assertEqual(sum(item.topology_hash == "b" for item in selected), 4)
        self.assertEqual(
            [item.layout_hash for item in selected],
            [
                item.layout_hash
                for item in pretrain.topology_balanced_epoch_records(
                    records,
                    samples_per_topology=4,
                    seed=31,
                )
            ],
        )

    def test_compact_filter_preserves_every_topology(self):
        records = [
            record("a", "a10", 10.0),
            record("a", "a30", 30.0),
            record("b", "b_unknown", None),
        ]
        selected = pretrain.compact_training_records(
            records,
            {"a": 10.0, "b": 1.0},
            max_area_ratio=1.5,
        )
        self.assertEqual({item.layout_hash for item in selected}, {"a10", "b_unknown"})

    def test_evaluation_seed_is_epoch_independent_and_layout_specific(self):
        first = pretrain.fixed_evaluation_seed(7, "layout-a", 0)
        self.assertEqual(first, pretrain.fixed_evaluation_seed(7, "layout-a", 0))
        self.assertNotEqual(first, pretrain.fixed_evaluation_seed(7, "layout-b", 0))
        self.assertNotEqual(first, pretrain.fixed_evaluation_seed(7, "layout-a", 1))


if __name__ == "__main__":
    unittest.main()
