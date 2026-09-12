import os
import sys
import unittest
from dataclasses import replace

import numpy as np
import torch


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.universal_graph_policy import (  # noqa: E402
    ACTION_FEATURE_DIM,
    CLOCK_FEATURE_DIM,
    EDGE_FEATURE_DIM,
    NODE_FEATURE_DIM,
    GraphPolicyInput,
    UniversalGraphPolicy,
)
from src.universal_graph_ppo import (  # noqa: E402
    GraphPPOConfig,
    GraphRollout,
    GraphTransition,
    compute_graph_rollout_gae,
    graph_memory_policy_step,
    graph_ppo_update,
    replay_graph_rollout,
)


def make_input(node_count, action_count, seed):
    generator = torch.Generator().manual_seed(seed)
    edges = [(index, index + 1) for index in range(node_count - 1)]
    memberships = [
        (action_idx, (action_idx * 2) % node_count)
        for action_idx in range(1, action_count)
    ]
    return GraphPolicyInput(
        node_features=torch.randn((node_count, NODE_FEATURE_DIM), generator=generator),
        edge_index=torch.tensor(edges, dtype=torch.long).t().contiguous(),
        edge_features=torch.randn((len(edges), EDGE_FEATURE_DIM), generator=generator),
        clock_features=torch.randn((CLOCK_FEATURE_DIM,), generator=generator),
        action_types=torch.tensor(
            [0] + [1 + (index % 7) for index in range(action_count - 1)],
            dtype=torch.long,
        ),
        action_features=torch.randn((action_count, ACTION_FEATURE_DIM), generator=generator),
        action_target_index=(
            torch.tensor(memberships, dtype=torch.long).t().contiguous()
            if memberships
            else torch.empty((2, 0), dtype=torch.long)
        ),
        action_mask=torch.tensor(
            [True] + [index % 3 != 0 for index in range(1, action_count)],
            dtype=torch.bool,
        ),
    )


class GraphRolloutTest(unittest.TestCase):
    def test_truncation_bootstraps_but_termination_does_not(self):
        policy_input = make_input(3, 3, 1)
        truncated = GraphRollout(
            transitions=[
                GraphTransition(
                    policy_input=policy_input,
                    action=0,
                    old_log_prob=-0.5,
                    reward=1.0,
                    value=2.0,
                    truncated=True,
                )
            ],
            bootstrap_value=3.0,
        )
        terminal = GraphRollout(
            transitions=[
                GraphTransition(
                    policy_input=policy_input,
                    action=0,
                    old_log_prob=-0.5,
                    reward=1.0,
                    value=2.0,
                    terminated=True,
                )
            ],
            bootstrap_value=3.0,
        )
        truncated_advantage, truncated_return = compute_graph_rollout_gae(
            truncated,
            gamma=0.9,
            gae_lambda=0.95,
        )
        terminal_advantage, terminal_return = compute_graph_rollout_gae(
            terminal,
            gamma=0.9,
            gae_lambda=0.95,
        )
        self.assertTrue(np.allclose(truncated_advantage, [1.7]))
        self.assertTrue(np.allclose(truncated_return, [3.7]))
        self.assertTrue(np.allclose(terminal_advantage, [-1.0]))
        self.assertTrue(np.allclose(terminal_return, [1.0]))

    def test_exact_feedback_is_assigned_to_the_improving_action(self):
        policy_input = make_input(3, 3, 2)
        rollout = GraphRollout(
            transitions=[
                GraphTransition(policy_input, 0, -0.2, 1.0, 0.0),
                GraphTransition(policy_input, 0, -0.2, 2.0, 0.0),
            ]
        )
        self.assertTrue(rollout.add_exact_feedback(0, 5.0))
        self.assertEqual(rollout.transitions[0].reward, 6.0)
        self.assertEqual(rollout.transitions[1].reward, 2.0)
        self.assertFalse(rollout.add_exact_feedback(None, 5.0))


class RaggedGraphPPOTest(unittest.TestCase):
    def test_one_update_mixes_different_graph_and_action_sizes(self):
        torch.manual_seed(11)
        model = UniversalGraphPolicy(hidden_dim=32, message_passing_steps=2)
        optimizer = torch.optim.Adam(model.parameters(), lr=1e-3)
        rollouts = []
        for rollout_idx, (node_count, action_count) in enumerate(((3, 4), (9, 7))):
            base_input = make_input(node_count, action_count, 10 + rollout_idx)
            memory = model.initial_memory_state("cpu")
            transitions = []
            for step in range(2):
                policy_input = replace(base_input, memory_state=memory.detach().clone())
                action, log_prob, value, next_memory = graph_memory_policy_step(
                    model,
                    policy_input,
                    "cpu",
                    deterministic=False,
                )
                transitions.append(
                    GraphTransition(
                        policy_input=policy_input,
                        action=action,
                        old_log_prob=log_prob,
                        reward=float((rollout_idx + 1) * (step + 1)),
                        value=value,
                        truncated=step == 1,
                    )
                )
                memory = next_memory
            rollouts.append(
                GraphRollout(
                    transitions=transitions,
                    bootstrap_value=0.25 * (rollout_idx + 1),
                    circuit_id=f"circuit-{rollout_idx}",
                    initial_memory_state=model.initial_memory_state("cpu"),
                )
            )

        for rollout in rollouts:
            replay_log_probs, replay_values, replay_memory = replay_graph_rollout(
                model,
                rollout,
                "cpu",
            )
            self.assertTrue(
                torch.allclose(
                    replay_log_probs,
                    torch.tensor([item.old_log_prob for item in rollout.transitions]),
                    atol=1e-6,
                    rtol=1e-6,
                )
            )
            self.assertTrue(
                torch.allclose(
                    replay_values,
                    torch.tensor([item.value for item in rollout.transitions]),
                    atol=1e-6,
                    rtol=1e-6,
                )
            )
            self.assertEqual(tuple(replay_memory.shape), (model.memory_dim,))

        before = [parameter.detach().clone() for parameter in model.parameters()]
        memory_before = model.memory_cell.weight_hh.detach().clone()
        metrics = graph_ppo_update(
            model,
            optimizer,
            rollouts,
            GraphPPOConfig(
                ppo_epochs=2,
                minibatch_size=3,
                target_kl=0.0,
            ),
            "cpu",
        )
        after = list(model.parameters())

        self.assertEqual(metrics["sample_count"], 4)
        self.assertGreater(metrics["update_steps"], 0)
        self.assertTrue(any(not torch.allclose(old, new) for old, new in zip(before, after)))
        self.assertFalse(torch.allclose(memory_before, model.memory_cell.weight_hh))
        for name in (
            "policy_loss_mean",
            "value_loss_mean",
            "entropy_mean",
            "approx_kl_mean",
            "clipfrac_mean",
        ):
            self.assertTrue(math_is_finite(metrics[name]), name)


def math_is_finite(value):
    return bool(np.isfinite(float(value)))


if __name__ == "__main__":
    unittest.main()
