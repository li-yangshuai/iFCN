import math
import os
import sys
import tempfile
import unittest
from pathlib import Path

import torch


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.ifcn_layout_dataset import parse_ifcn  # noqa: E402
from src.ifcn_offline_learning import (  # noqa: E402
    build_ifcn_clock_features,
    build_offline_ifcn_sample,
    listwise_imitation_loss,
    offline_pretraining_loss,
)
from src.universal_graph_policy import GATE_TYPE_NAMES, UniversalGraphPolicy  # noqa: E402


def fixture():
    phase_rows = "\n".join(
        " ".join(f"({x},{y}):{(x + y) % 4};" for x in range(5))
        for y in range(3)
    )
    return f"""#circuit name: offline.v
#gates number: 3
#edges number: 2
#total layers: 3
#layout area: width: 5, height: 3, area: 15
#phase count: 4
#clock scheme consistency: success
#clock scheme conflicts: 0
#nodes info
0, in, input, (0,1);
1, gate, and, (2,1);
2, out, output, (4,1);
#nodes info
#paths info
(0,1): (0,1),(1,1),(2,1);
(1,2): (2,1),(3,1),(4,1);
#paths info
#phase map
{phase_rows}
#phase map
"""


def vertical_fixture():
    phase_rows = "\n".join(
        " ".join(f"({x},{y}):{(x + y) % 4};" for x in range(3))
        for y in range(5)
    )
    return f"""#circuit name: vertical.v
#gates number: 3
#edges number: 2
#total layers: 3
#layout area: width: 3, height: 5, area: 15
#phase count: 4
#clock scheme consistency: success
#clock scheme conflicts: 0
#nodes info
0, in, input, (1,4);
1, gate, and, (1,2);
2, out, output, (1,0);
#nodes info
#paths info
(0,1): (1,4),(1,3),(1,2);
(1,2): (1,2),(1,1),(1,0);
#paths info
#phase map
{phase_rows}
#phase map
"""


class IFCNOfflineLearningTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        path = Path(self.temporary.name) / "offline.ifcn"
        path.write_text(fixture(), encoding="utf-8")
        self.record = parse_ifcn(path)

    def tearDown(self):
        self.temporary.cleanup()

    def test_teacher_mask_matches_dynamic_action_count(self):
        sample = build_offline_ifcn_sample(
            self.record,
            best_area_for_topology=15.0,
            seed=7,
            perturb_scale=0.4,
        )
        sample.validate()
        self.assertEqual(
            sample.positive_action_mask.shape[0],
            sample.policy_input.action_types.shape[0],
        )
        self.assertTrue(torch.any(sample.positive_action_mask))
        self.assertTrue(torch.all(sample.policy_input.action_mask[sample.positive_action_mask]))

    def test_listwise_loss_rewards_any_positive_candidate(self):
        logits = torch.tensor([0.0, 1.0, -1.0, 2.0], requires_grad=True)
        positives = torch.tensor([False, True, False, True])
        loss = listwise_imitation_loss(logits, positives)
        self.assertTrue(torch.isfinite(loss))
        loss.backward()
        self.assertTrue(torch.isfinite(logits.grad).all())

    def test_ifcn_phase_projection_uses_online_clock_feature_slots(self):
        clock = build_ifcn_clock_features(self.record)
        self.assertEqual(tuple(clock.shape), (15,))
        self.assertEqual(float(clock[0]), 1.0)
        self.assertEqual(float(clock[3]), 1.0)
        self.assertEqual(float(clock[5]), 1.0)
        self.assertTrue(torch.equal(clock[6:], torch.zeros(9)))

        sample = build_offline_ifcn_sample(self.record, seed=17)
        phase_start = len(GATE_TYPE_NAMES) + 9
        node_features = sample.policy_input.node_features
        self.assertEqual(float(node_features[0, phase_start + 1]), 1.0)
        self.assertEqual(float(node_features[1, phase_start + 3]), 1.0)
        self.assertTrue(torch.all(node_features[:, phase_start + 4] == 1.0))
        self.assertAlmostEqual(float(sample.policy_input.edge_features[0, 5]), 0.5)
        self.assertEqual(float(sample.policy_input.edge_features[0, 9]), 1.0)

    def test_route_labels_follow_canonical_axis_swap_and_flip(self):
        path = Path(self.temporary.name) / "vertical.ifcn"
        path.write_text(vertical_fixture(), encoding="utf-8")
        record = parse_ifcn(path)
        sample = build_offline_ifcn_sample(record, seed=19)
        routes = sample.target_route_features
        self.assertTrue(torch.allclose(routes[:, 3], torch.ones(2)))
        self.assertTrue(torch.allclose(routes[:, 4], torch.zeros(2)))
        self.assertTrue(torch.allclose(routes[:, 5], torch.ones(2)))
        self.assertTrue(torch.allclose(routes[:, 6], torch.zeros(2)))
        self.assertTrue(torch.allclose(routes[:, 7], torch.ones(2)))
        self.assertTrue(torch.allclose(routes[:, 8], torch.zeros(2)))

    def test_one_offline_update_is_finite(self):
        torch.manual_seed(4)
        sample = build_offline_ifcn_sample(self.record, seed=11, perturb_scale=0.35)
        model = UniversalGraphPolicy(hidden_dim=32, message_passing_steps=2)
        optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)
        before = model.node_encoder[0].weight.detach().clone()
        loss, metrics = offline_pretraining_loss(model, sample, "cpu")
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()
        self.assertTrue(torch.isfinite(loss))
        self.assertFalse(torch.allclose(before, model.node_encoder[0].weight))
        self.assertTrue(all(torch.isfinite(torch.tensor(value)) for value in metrics.values()))

    def test_zero_declared_area_has_finite_neutral_quality_target(self):
        path = Path(self.temporary.name) / "zero_area.ifcn"
        path.write_text(
            fixture().replace(
                "#layout area: width: 5, height: 3, area: 15",
                "#layout area: width: 5, height: 3, area: 0",
            ),
            encoding="utf-8",
        )
        record = parse_ifcn(path)
        self.assertTrue(record.quality.valid_for_training)
        sample = build_offline_ifcn_sample(
            record,
            best_area_for_topology=math.inf,
            seed=13,
        )
        self.assertTrue(math.isfinite(sample.quality_target))
        self.assertEqual(sample.quality_target, 0.0)


if __name__ == "__main__":
    unittest.main()
