import math
import os
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import numpy as np
import torch


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
MAIN_ROOT = os.path.join(ALGORITHM_ROOT, "main")
for import_root in (MAIN_ROOT, ALGORITHM_ROOT):
    if import_root not in sys.path:
        sys.path.insert(0, import_root)

import test_randomPhase as exact_router  # noqa: E402
import train_universal_graph_ppo as universal_trainer  # noqa: E402
from src.stochastic_clock import ClockFieldSpec, sample_clock_field  # noqa: E402
from src.universal_graph_policy import (  # noqa: E402
    ROUTE_AUX_FEATURE_DIM,
    UniversalGraphPolicy,
    predict_route_edge_priorities,
)


class PriorityCircuit:
    effective_edges = [(1, 3), (2, 3), (1, 4)]
    effective_nodes = [1, 2, 3, 4]
    same_layer_route_pairs = {}
    differ_layer_route_pairs = list(effective_edges)
    getOutputNodesIndex = [3, 4]
    getInputNodesIndex = []

    @staticmethod
    def get_fanouts(node_id):
        return {1: [3, 4], 2: [3], 3: [], 4: []}[int(node_id)]

    @staticmethod
    def get_fanins(node_id):
        return {1: [], 2: [], 3: [1, 2], 4: [1]}[int(node_id)]

    @staticmethod
    def get_layer_of_node(node_id):
        return {1: 0, 2: 0, 3: 1, 4: 1}[int(node_id)]

    @staticmethod
    def getNodeTypeString(node_id):
        return "output" if int(node_id) in (3, 4) else "input"


NODE_POSITIONS = {1: (0, 0), 2: (0, 3), 3: (4, 1), 4: (4, 4)}


class PriorityEnv:
    node_ids = [1, 2, 3, 4]
    circuit = PriorityCircuit()


class DevicePolicyInput:
    def to(self, _device):
        return self


class FixedRouteHead:
    def __init__(self, route_features):
        self.route_features = route_features

    def auxiliary_predictions(self, _policy_input):
        return {"route": self.route_features}


class FakeBoard:
    def __init__(self):
        self.nodeIndexToCoordMap = dict(NODE_POSITIONS)

    @staticmethod
    def savePath(_edge, _path):
        return None

    @staticmethod
    def computeLayoutArea():
        return 5, 5


class LearnedRoutePriorityTest(unittest.TestCase):
    def test_clock_aligned_start_is_collision_free_and_edge_monotone(self):
        edges = [(0, 2), (1, 2), (1, 3), (2, 4), (3, 4)]
        env = SimpleNamespace(
            ordered_layers=[[0, 1], [2, 3], [4]],
            orientation=exact_router.LEFT_RIGHT,
            circuit=SimpleNamespace(effective_edges=edges),
        )
        args = SimpleNamespace(padding=3)
        for direction in (-1, 1):
            field = sample_clock_field(
                (0, 0, 15, 15),
                ClockFieldSpec(
                    seed=5,
                    secondary_direction=direction,
                    secondary_advance_probability=0.5,
                ),
            )
            positions = universal_trainer.clock_aligned_start_positions(
                env,
                field,
                args,
            )
            self.assertEqual(len(set(positions.values())), len(positions))
            for source, target in edges:
                self.assertGreaterEqual(
                    direction * (positions[target][1] - positions[source][1]),
                    0,
                )

    def test_robust_field_resampling_keeps_causal_directions(self):
        reference = sample_clock_field(
            (0, 0, 7, 7),
            ClockFieldSpec(
                seed=7,
                primary_axis="y",
                primary_direction=-1,
                secondary_direction=-1,
                secondary_advance_probability=0.4,
            ),
        )
        args = SimpleNamespace(
            padding=3,
            steps_per_episode=8,
            secondary_advance_probability_min=0.25,
            secondary_advance_probability_max=0.75,
        )
        resampled = universal_trainer.resample_compatible_field(
            reference,
            {0: (4, 4), 1: (6, 6)},
            args,
            np.random.default_rng(11),
        )
        self.assertNotEqual(resampled.spec.seed, reference.spec.seed)
        self.assertEqual(resampled.spec.primary_axis, reference.spec.primary_axis)
        self.assertEqual(
            resampled.spec.primary_direction,
            reference.spec.primary_direction,
        )
        self.assertEqual(
            resampled.spec.secondary_direction,
            reference.spec.secondary_direction,
        )

    def test_online_freeze_preserves_offline_backbone_but_trains_gru_actor(self):
        model = UniversalGraphPolicy(hidden_dim=32, memory_dim=16)
        parameters = universal_trainer.configure_online_trainable_parameters(model, True)
        self.assertFalse(model.node_encoder[0].weight.requires_grad)
        self.assertFalse(model.route_head[0].weight.requires_grad)
        self.assertTrue(model.memory_cell.weight_hh.requires_grad)
        self.assertTrue(model.actor[0].weight.requires_grad)
        self.assertEqual(
            sum(parameter.numel() for parameter in parameters),
            sum(parameter.numel() for parameter in model.parameters() if parameter.requires_grad),
        )

    def test_exact_checkpoint_round_requires_every_circuit_once(self):
        field = {
            "legal": True,
            "cost": 10.0,
            "failed_edges": 0,
            "direction_violations": 0,
            "clock_violations": 0,
            "area": 20.0,
            "runtime_sec": 0.1,
            "field_hash": "field",
        }
        records = [
            {"context_index": 0, "context_visit": 4, "fields": [field]},
            {
                "context_index": 1,
                "context_visit": 4,
                "fields": [{**field, "legal": False, "failed_edges": 1}],
            },
        ]
        self.assertIsNone(
            universal_trainer.completed_exact_round(records[:1], 2, 0.9)
        )
        metrics = universal_trainer.completed_exact_round(records, 2, 0.9)
        self.assertIsNotNone(metrics)
        self.assertEqual(metrics.sample_count, 2)
        self.assertEqual(metrics.success_rate, 0.5)

    def test_exact_feedback_candidate_is_the_last_accepted_policy_state(self):
        env = SimpleNamespace(current_positions={1: (2, 3), 2: (4, 5)})
        candidate = universal_trainer.accepted_policy_candidate(
            env,
            {
                "invalid_action": False,
                "accepted_action": True,
                "rollback_action": False,
            },
            6,
        )
        env.current_positions[1] = (99, 99)
        self.assertEqual(candidate["transition_index"], 6)
        self.assertEqual(candidate["positions"], {1: (2, 3), 2: (4, 5)})

    def test_invalid_or_rolled_back_action_cannot_receive_exact_feedback(self):
        env = SimpleNamespace(current_positions={1: (2, 3)})
        self.assertIsNone(
            universal_trainer.accepted_policy_candidate(
                env,
                {"invalid_action": True, "accepted_action": True},
                0,
            )
        )
        self.assertIsNone(
            universal_trainer.accepted_policy_candidate(
                env,
                {"accepted_action": False, "rollback_action": True},
                1,
            )
        )

    def test_prediction_uses_normalized_edge_keys_and_finite_scores(self):
        route_features = torch.zeros(
            (len(PriorityCircuit.effective_edges), ROUTE_AUX_FEATURE_DIM),
            dtype=torch.float32,
        )
        route_features[0, [0, 1, 2, 9]] = torch.tensor([2.0, 4.0, 6.0, 8.0])
        route_features[1, [0, 1, 2, 9]] = torch.tensor([-1.0, 1.0, 2.0, 3.0])
        route_features[2, [0, 1, 2, 9]] = torch.tensor([0.5, 0.25, 0.75, 1.0])

        priorities = predict_route_edge_priorities(
            FixedRouteHead(route_features),
            PriorityEnv(),
            DevicePolicyInput(),
            "cpu",
        )

        self.assertEqual(list(priorities), PriorityCircuit.effective_edges)
        self.assertTrue(all(isinstance(edge, tuple) for edge in priorities))
        self.assertTrue(all(math.isfinite(score) for score in priorities.values()))
        self.assertAlmostEqual(priorities[(1, 3)], 11.5, places=6)

    def test_non_finite_learned_priority_is_rejected(self):
        route_features = torch.zeros(
            (len(PriorityCircuit.effective_edges), ROUTE_AUX_FEATURE_DIM),
            dtype=torch.float32,
        )
        route_features[1, 0] = float("nan")
        with self.assertRaisesRegex(FloatingPointError, "non-finite learned route priorities"):
            predict_route_edge_priorities(
                FixedRouteHead(route_features),
                PriorityEnv(),
                DevicePolicyInput(),
                "cpu",
            )

    def test_learned_order_is_first_and_heuristic_base_order_remains(self):
        base_order = exact_router.build_route_order(
            PriorityCircuit(),
            NODE_POSITIONS,
            exact_router.LEFT_RIGHT,
        )
        priorities = {(1, 3): -1.0, (2, 3): 5.0, (1, 4): 10.0}
        expected_learned_order = sorted(
            base_order,
            key=lambda edge: (-priorities[edge], int(edge[0]), int(edge[1])),
        )

        variants = exact_router.build_route_order_variants(
            PriorityCircuit(),
            NODE_POSITIONS,
            exact_router.LEFT_RIGHT,
            edge_priorities=priorities,
        )
        fallback_variants = exact_router.build_route_order_variants(
            PriorityCircuit(),
            NODE_POSITIONS,
            exact_router.LEFT_RIGHT,
            edge_priorities=None,
        )

        self.assertEqual(variants[0], expected_learned_order)
        self.assertIn(base_order, variants[1:])
        self.assertEqual(fallback_variants[0], base_order)
        self.assertTrue(any(variant != base_order for variant in fallback_variants[1:]))

    def test_exact_router_attempts_the_learned_variant_first(self):
        priorities = {(1, 3): -1.0, (2, 3): 5.0, (1, 4): 10.0}
        expected_order = sorted(
            PriorityCircuit.effective_edges,
            key=lambda edge: (-priorities[edge], int(edge[0]), int(edge[1])),
        )
        attempted_edges = []

        def route_edge(_board, _router, source, target, *_args, **_kwargs):
            attempted_edges.append((int(source), int(target)))
            return [(0, 0), (1, 0)]

        fake_binding = SimpleNamespace(MapPhaseAStar=lambda *_args, **_kwargs: object())
        with (
            patch.object(
                exact_router,
                "create_board_with_positions",
                side_effect=lambda *_args, **_kwargs: FakeBoard(),
            ),
            patch.object(
                exact_router,
                "route_single_edge_with_direction_constraints",
                side_effect=route_edge,
            ),
            patch.object(exact_router, "compute_direction_violation_count", return_value=0),
            patch.object(exact_router, "find_direction_violation_edges", return_value=set()),
            patch.object(exact_router, "iFCN_Lab", fake_binding),
            patch.object(exact_router, "DEFAULT_REPAIR_ORDER_ATTEMPTS", 0),
            patch.object(exact_router, "DEFAULT_LOCAL_RIPUP_ATTEMPTS", 0),
        ):
            _board, _paths, failed_edges = exact_router.route_edges_with_phase(
                FakeBoard(),
                PriorityCircuit(),
                phase_cycle=4,
                padding=2,
                max_same_phase=4,
                orientation=exact_router.LEFT_RIGHT,
                edge_priorities=priorities,
            )

        self.assertEqual(failed_edges, [])
        self.assertEqual(attempted_edges, expected_order)

    def test_candidate_priority_reaches_router_and_result_marks_guidance(self):
        priorities = {(1, 3): 4.5, (2, 3): -0.25}
        captured = {}

        def route_with_capture(board, *_args, **kwargs):
            captured["edge_priorities"] = kwargs.get("edge_priorities")
            return board, {}, []

        candidate = {
            "strategy": "universal-graph-ppo",
            "orientation": exact_router.LEFT_RIGHT,
            "x_spacing": "n/a",
            "y_spacing": "n/a",
            "node_positions": dict(NODE_POSITIONS),
            "routing_edge_priorities": priorities,
        }
        with (
            patch.object(exact_router, "create_board_with_positions", return_value=FakeBoard()),
            patch.object(exact_router, "route_edges_with_phase", side_effect=route_with_capture),
            patch.object(exact_router, "compute_io_edge_penalty", return_value=0),
            patch.object(exact_router, "compute_route_overhang_penalty", return_value=0),
            patch.object(exact_router, "compute_direction_violation_count", return_value=0),
        ):
            result = exact_router.evaluate_layout_candidate(
                candidate,
                PriorityCircuit(),
                phase_cycle=4,
                padding=2,
                max_same_phase=4,
            )

        self.assertEqual(captured["edge_priorities"], priorities)
        self.assertTrue(result["routing_edge_priority_guidance"])

    def test_universal_exact_candidate_carries_learned_priorities(self):
        priorities = {(1, 3): 1.25, (2, 3): -2.0}
        captured_candidates = []

        def fake_exact(candidate, *_args, **_kwargs):
            captured_candidates.append(candidate)
            return {
                "routed_paths": {},
                "failed_edges": [],
                "direction_violation_count": 0,
                "route_overhang_penalty": 0,
                "io_exposure_penalty": 0,
                "width": 5,
                "height": 5,
                "area": 25.0,
            }

        context = SimpleNamespace(
            env=SimpleNamespace(orientation=exact_router.LEFT_RIGHT),
            circuit=PriorityCircuit(),
            warm_start={"area": 30.0},
        )
        field = SimpleNamespace(
            field_hash="field-hash",
            transition_allowed=lambda _left, _right: True,
        )
        args = SimpleNamespace(
            phase_count=4,
            padding=2,
            max_same_phase=4,
            exact_eval_timeout_sec=1,
            aspect_ratio_limit=4.0,
            aspect_ratio_weight=1.0,
            max_span_weight=1.0,
            area_regression_weight=1.0,
            cvar_alpha=0.9,
        )

        with patch.object(
            universal_trainer,
            "evaluate_layout_candidate_with_timeout",
            side_effect=fake_exact,
        ):
            metrics, _records = universal_trainer.exact_evaluate_positions(
                context,
                NODE_POSITIONS,
                [field],
                args,
                edge_priorities=priorities,
            )

        self.assertEqual(len(captured_candidates), 1)
        self.assertEqual(
            captured_candidates[0]["routing_edge_priorities"],
            priorities,
        )
        self.assertEqual(metrics.success_rate, 1.0)


if __name__ == "__main__":
    unittest.main()
