import os
import sys
import unittest
from dataclasses import replace

import torch


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.stochastic_clock import ClockFieldSpec, sample_clock_field  # noqa: E402
from src.universal_graph_policy import (  # noqa: E402
    ACTION_FEATURE_DIM,
    CLOCK_FEATURE_DIM,
    EDGE_FEATURE_DIM,
    EPISODE_FEATURE_DIM,
    NODE_FEATURE_DIM,
    RETRIEVAL_FEATURE_DIM,
    ROUTE_AUX_FEATURE_DIM,
    GraphPolicyInput,
    UniversalGraphPolicy,
    build_graph_policy_input,
)


def make_policy_input(node_count=4):
    generator = torch.Generator().manual_seed(91 + node_count)
    node_features = torch.randn((node_count, NODE_FEATURE_DIM), generator=generator)
    edges = [(index, index + 1) for index in range(node_count - 1)]
    edge_index = (
        torch.tensor(edges, dtype=torch.long).t().contiguous()
        if edges
        else torch.empty((2, 0), dtype=torch.long)
    )
    action_count = 3
    memberships = [(1, 0), (2, node_count - 1)]
    if node_count > 2:
        memberships.append((2, node_count // 2))
    return GraphPolicyInput(
        node_features=node_features,
        edge_index=edge_index,
        edge_features=torch.randn((len(edges), EDGE_FEATURE_DIM), generator=generator),
        clock_features=torch.linspace(0.0, 1.0, CLOCK_FEATURE_DIM),
        action_types=torch.tensor([0, 7, 5], dtype=torch.long),
        action_features=torch.randn((action_count, ACTION_FEATURE_DIM), generator=generator),
        action_target_index=torch.tensor(memberships, dtype=torch.long).t().contiguous(),
        action_mask=torch.tensor([True, False, True]),
    )


class UniversalGraphPolicyTest(unittest.TestCase):
    def setUp(self):
        torch.manual_seed(7)
        self.model = UniversalGraphPolicy(hidden_dim=48, message_passing_steps=2)
        self.model.eval()

    def test_same_parameters_accept_different_graph_and_action_sizes(self):
        small = make_policy_input(3)
        large = make_policy_input(11)
        small_logits, small_value = self.model(small)
        large_logits, large_value = self.model(large)

        self.assertEqual(tuple(small_logits.shape), (3,))
        self.assertEqual(tuple(large_logits.shape), (3,))
        self.assertEqual(small_value.ndim, 0)
        self.assertEqual(large_value.ndim, 0)
        self.assertTrue(torch.isfinite(small_logits[small.action_mask]).all())
        self.assertEqual(small_logits[1], torch.finfo(small_logits.dtype).min)

    def test_edgeless_single_node_graph_is_supported(self):
        policy_input = make_policy_input(1)
        logits, value = self.model(policy_input)
        self.assertEqual(tuple(policy_input.edge_features.shape), (0, EDGE_FEATURE_DIM))
        self.assertTrue(torch.isfinite(logits[policy_input.action_mask]).all())
        self.assertTrue(torch.isfinite(value))

    def test_node_permutation_does_not_change_action_logits(self):
        original = make_policy_input(6)
        order = torch.tensor([2, 5, 0, 4, 1, 3], dtype=torch.long)
        old_to_new = torch.empty_like(order)
        old_to_new[order] = torch.arange(order.numel())
        permuted = GraphPolicyInput(
            node_features=original.node_features[order],
            edge_index=old_to_new[original.edge_index],
            edge_features=original.edge_features.clone(),
            clock_features=original.clock_features.clone(),
            action_types=original.action_types.clone(),
            action_features=original.action_features.clone(),
            action_target_index=torch.stack(
                (
                    original.action_target_index[0],
                    old_to_new[original.action_target_index[1]],
                )
            ),
            action_mask=original.action_mask.clone(),
        )

        original_logits, original_value = self.model(original)
        permuted_logits, permuted_value = self.model(permuted)
        self.assertTrue(torch.allclose(original_logits, permuted_logits, atol=1e-6, rtol=1e-6))
        self.assertTrue(torch.allclose(original_value, permuted_value, atol=1e-6, rtol=1e-6))

    def test_forward_backward_is_finite(self):
        self.model.train()
        policy_input = make_policy_input(8)
        logits, value = self.model(policy_input)
        loss = -torch.log_softmax(logits, dim=0)[0] + value.square()
        loss.backward()
        gradients = [parameter.grad for parameter in self.model.parameters() if parameter.grad is not None]
        self.assertTrue(gradients)
        self.assertTrue(all(torch.isfinite(gradient).all() for gradient in gradients))

    def test_retrieval_and_episode_memory_change_policy_context(self):
        policy_input = make_policy_input(7)
        base_logits, _base_value, base_memory = self.model.forward_with_memory(policy_input)
        contextual_input = replace(
            policy_input,
            retrieval_features=torch.linspace(-1.0, 1.0, RETRIEVAL_FEATURE_DIM),
            episode_features=torch.linspace(1.0, -1.0, EPISODE_FEATURE_DIM),
            memory_state=torch.full((self.model.memory_dim,), 0.25),
        )
        contextual_logits, contextual_value, contextual_memory = self.model.forward_with_memory(
            contextual_input
        )
        self.assertEqual(tuple(contextual_memory.shape), (self.model.memory_dim,))
        self.assertTrue(torch.isfinite(contextual_memory).all())
        self.assertTrue(torch.isfinite(contextual_value))
        self.assertFalse(torch.allclose(base_memory, contextual_memory))
        self.assertFalse(
            torch.allclose(
                base_logits[policy_input.action_mask],
                contextual_logits[policy_input.action_mask],
            )
        )

    def test_offline_auxiliary_heads_follow_ragged_graph_size(self):
        policy_input = make_policy_input(5)
        predictions = self.model.auxiliary_predictions(policy_input)
        self.assertEqual(tuple(predictions["placement"].shape), (5, 2))
        self.assertEqual(
            tuple(predictions["route"].shape),
            (policy_input.edge_index.shape[1], ROUTE_AUX_FEATURE_DIM),
        )
        self.assertEqual(predictions["quality"].ndim, 0)


class FakeCircuit:
    effective_edges = [(10, 11), (11, 12)]

    @staticmethod
    def getNodeTypeString(node_id):
        return {10: "input", 11: "maj", 12: "output"}[int(node_id)]

    @staticmethod
    def get_fanins(node_id):
        return {10: [], 11: [10], 12: [11]}[int(node_id)]

    @staticmethod
    def get_fanouts(node_id):
        return {10: [11], 11: [12], 12: []}[int(node_id)]


class FakeEnv:
    node_ids = [10, 11, 12]
    ordered_layers = [[10], [11], [12]]
    current_positions = {10: (0, 0), 11: (2, 1), 12: (4, 2)}
    input_nodes = {10}
    output_nodes = {12}
    embedding_scores = {10: 0.1, 11: 0.5, 12: 0.9}
    action_defs = [
        ("noop", -1, 0),
        ("node_shift", 11, 1),
        ("gap", 0, -1),
        ("layer_block_shift", (1, 2), 2),
    ]
    circuit = FakeCircuit()
    coord_norm = 8.0
    phase_cycle = 4
    max_same_phase = 4
    padding = 2
    orientation = "left-right"

    @staticmethod
    def get_action_mask():
        return [True, True, True, False]


class EnvironmentAdapterTest(unittest.TestCase):
    def test_existing_environment_can_build_dynamic_graph_input(self):
        field = sample_clock_field(
            (-1, -1, 5, 3),
            ClockFieldSpec(seed=3, mode="stochastic-bands"),
        )
        policy_input = build_graph_policy_input(FakeEnv(), field)
        policy_input.validate()
        self.assertEqual(tuple(policy_input.node_features.shape), (3, NODE_FEATURE_DIM))
        self.assertEqual(tuple(policy_input.action_features.shape), (4, ACTION_FEATURE_DIM))
        self.assertEqual(tuple(policy_input.edge_features.shape), (2, EDGE_FEATURE_DIM))
        self.assertEqual(tuple(policy_input.clock_features.shape), (CLOCK_FEATURE_DIM,))
        self.assertEqual(policy_input.action_mask.tolist(), [True, True, True, False])
        self.assertGreater(policy_input.action_target_index.shape[1], 0)

    def test_node_features_are_invariant_to_global_translation(self):
        original = FakeEnv()
        shifted = FakeEnv()
        shifted.current_positions = {
            node_id: (coord[0] + 37, coord[1] - 19)
            for node_id, coord in original.current_positions.items()
        }

        original_input = build_graph_policy_input(original, None)
        shifted_input = build_graph_policy_input(shifted, None)
        self.assertTrue(
            torch.allclose(
                original_input.node_features,
                shifted_input.node_features,
                atol=1e-7,
                rtol=1e-7,
            )
        )


if __name__ == "__main__":
    unittest.main()
