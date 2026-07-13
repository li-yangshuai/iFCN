"""PPO utilities for variable-size circuit graphs and ragged action lists."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
import math
from statistics import fmean
from typing import Sequence

import numpy as np
import torch
from torch import nn

try:
    from .universal_graph_policy import GraphPolicyInput, UniversalGraphPolicy
except ImportError:  # Supports the project's current top-level ``src`` import style.
    from universal_graph_policy import GraphPolicyInput, UniversalGraphPolicy


@dataclass
class GraphTransition:
    policy_input: GraphPolicyInput
    action: int
    old_log_prob: float
    reward: float
    value: float
    terminated: bool = False
    truncated: bool = False


@dataclass
class GraphRollout:
    """One episode from one circuit/clock scenario."""

    transitions: list[GraphTransition] = field(default_factory=list)
    bootstrap_value: float = 0.0
    circuit_id: str = ""
    clock_field_hash: str = ""
    initial_memory_state: torch.Tensor | None = None

    def add_exact_feedback(self, transition_index: int | None, reward: float) -> bool:
        """Attach delayed exact feedback to the action that produced the candidate.

        Returns ``False`` for a reset-state candidate, because no policy action
        should receive credit for a layout that existed before the rollout.
        """

        if transition_index is None:
            return False
        transition_index = int(transition_index)
        if not 0 <= transition_index < len(self.transitions):
            raise IndexError("exact-feedback transition index is outside the rollout")
        self.transitions[transition_index].reward += float(reward)
        return True


@dataclass(frozen=True)
class GraphPPOConfig:
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_eps: float = 0.2
    value_clip_eps: float = 0.2
    value_coef: float = 0.5
    entropy_coef: float = 0.01
    max_grad_norm: float = 1.0
    ppo_epochs: int = 4
    minibatch_size: int = 32
    target_kl: float = 0.03

    def __post_init__(self) -> None:
        if not 0.0 <= float(self.gamma) <= 1.0:
            raise ValueError("gamma must be in [0, 1]")
        if not 0.0 <= float(self.gae_lambda) <= 1.0:
            raise ValueError("gae_lambda must be in [0, 1]")
        if float(self.clip_eps) <= 0.0:
            raise ValueError("clip_eps must be positive")
        if int(self.ppo_epochs) <= 0 or int(self.minibatch_size) <= 0:
            raise ValueError("ppo_epochs and minibatch_size must be positive")


def compute_graph_rollout_gae(
    rollout: GraphRollout,
    *,
    gamma: float,
    gae_lambda: float,
) -> tuple[np.ndarray, np.ndarray]:
    """Compute returns for one episode, bootstrapping only truncations."""

    transition_count = len(rollout.transitions)
    advantages = np.zeros(transition_count, dtype=np.float32)
    if transition_count == 0:
        return advantages, advantages.copy()

    gae = 0.0
    next_value = float(rollout.bootstrap_value)
    for index in reversed(range(transition_count)):
        transition = rollout.transitions[index]
        non_terminal = 0.0 if bool(transition.terminated) else 1.0
        delta = (
            float(transition.reward)
            + float(gamma) * next_value * non_terminal
            - float(transition.value)
        )
        gae = delta + float(gamma) * float(gae_lambda) * non_terminal * gae
        advantages[index] = float(gae)
        next_value = float(transition.value)
    values = np.asarray(
        [float(transition.value) for transition in rollout.transitions],
        dtype=np.float32,
    )
    return advantages, advantages + values


def graph_policy_step(
    model: UniversalGraphPolicy,
    policy_input: GraphPolicyInput,
    device: torch.device | str,
    *,
    deterministic: bool = False,
) -> tuple[int, float, float]:
    """Select one dynamic candidate and return action/log-prob/value."""

    device_input = policy_input.to(device)
    with torch.no_grad():
        distribution, value = model.distribution(device_input)
        action = (
            torch.argmax(distribution.logits)
            if deterministic
            else distribution.sample()
        )
        log_prob = distribution.log_prob(action)
    if not torch.isfinite(log_prob) or not torch.isfinite(value):
        raise FloatingPointError("non-finite graph policy output")
    return int(action.item()), float(log_prob.item()), float(value.item())


def graph_memory_policy_step(
    model: UniversalGraphPolicy,
    policy_input: GraphPolicyInput,
    device: torch.device | str,
    *,
    deterministic: bool = False,
) -> tuple[int, float, float, torch.Tensor]:
    """Select an action and advance the episode memory exactly once."""

    device_input = policy_input.to(device)
    with torch.no_grad():
        distribution, value, next_memory = model.distribution_with_memory(device_input)
        action = (
            torch.argmax(distribution.logits)
            if deterministic
            else distribution.sample()
        )
        log_prob = distribution.log_prob(action)
    if (
        not torch.isfinite(log_prob)
        or not torch.isfinite(value)
        or not torch.isfinite(next_memory).all()
    ):
        raise FloatingPointError("non-finite recurrent graph policy output")
    return (
        int(action.item()),
        float(log_prob.item()),
        float(value.item()),
        next_memory.detach(),
    )


def bootstrap_graph_value(
    model: UniversalGraphPolicy,
    next_input: GraphPolicyInput,
    device: torch.device | str,
) -> float:
    with torch.no_grad():
        _logits, value = model(next_input.to(device))
    if not torch.isfinite(value):
        raise FloatingPointError("non-finite graph critic bootstrap value")
    return float(value.item())


def _normalized_entropy(distribution, action_mask: torch.Tensor) -> torch.Tensor:
    valid_count = int(action_mask.sum().item())
    if valid_count <= 1:
        return distribution.entropy() * 0.0
    return distribution.entropy() / math.log(float(valid_count))


def _initial_rollout_memory(
    model: UniversalGraphPolicy,
    rollout: GraphRollout,
    device: torch.device | str,
) -> torch.Tensor:
    if rollout.initial_memory_state is None:
        return model.initial_memory_state(device)
    memory = rollout.initial_memory_state.to(device=device, dtype=next(model.parameters()).dtype)
    if memory.shape != (model.memory_dim,):
        raise ValueError("rollout initial memory dimension does not match the policy")
    return memory


def _replay_rollout_with_memory(
    model: UniversalGraphPolicy,
    rollout: GraphRollout,
    device: torch.device | str,
) -> tuple[list, list[torch.Tensor], torch.Tensor]:
    """Replay one complete episode in order so GRU state is never stale."""

    memory = _initial_rollout_memory(model, rollout, device)
    distributions = []
    values = []
    for transition in rollout.transitions:
        policy_input = replace(
            transition.policy_input.to(device),
            memory_state=memory,
        )
        distribution, value, memory = model.distribution_with_memory(policy_input)
        distributions.append(distribution)
        values.append(value)
    return distributions, values, memory


def replay_graph_rollout(
    model: UniversalGraphPolicy,
    rollout: GraphRollout,
    device: torch.device | str,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Return behavior-action log-probs/values from ordered recurrent replay."""

    with torch.no_grad():
        distributions, values, final_memory = _replay_rollout_with_memory(
            model,
            rollout,
            device,
        )
        log_probs = torch.stack(
            [
                distribution.log_prob(
                    torch.tensor(
                        int(transition.action),
                        dtype=torch.long,
                        device=device,
                    )
                )
                for distribution, transition in zip(distributions, rollout.transitions)
            ]
        ) if distributions else torch.empty(0, device=device)
        value_tensor = torch.stack(values) if values else torch.empty(0, device=device)
    return log_probs, value_tensor, final_memory.detach()


def graph_ppo_update(
    model: UniversalGraphPolicy,
    optimizer: torch.optim.Optimizer,
    rollouts: Sequence[GraphRollout],
    config: GraphPPOConfig,
    device: torch.device | str,
) -> dict[str, float | int | bool]:
    """Update one shared policy from multiple circuits and clock scenarios.

    Graphs and action lists remain ragged.  Each sample is forwarded separately
    inside a minibatch and the scalar PPO terms are stacked.  This is deliberately
    simple and correct; a future PyG batch implementation can optimize throughput
    without changing checkpoint semantics.
    """

    prepared_rollouts = []
    raw_advantage_values = []
    for rollout in rollouts:
        advantages, returns = compute_graph_rollout_gae(
            rollout,
            gamma=config.gamma,
            gae_lambda=config.gae_lambda,
        )
        if rollout.transitions:
            prepared_rollouts.append((rollout, advantages, returns))
            raw_advantage_values.extend(float(value) for value in advantages)
    if not raw_advantage_values:
        return {
            "policy_loss_mean": 0.0,
            "value_loss_mean": 0.0,
            "entropy_mean": 0.0,
            "approx_kl_mean": 0.0,
            "clipfrac_mean": 0.0,
            "update_steps": 0,
            "sample_count": 0,
            "stopped_for_kl": False,
        }

    raw_advantages = np.asarray(raw_advantage_values, dtype=np.float32)
    advantage_mean = float(raw_advantages.mean())
    advantage_std = max(1e-6, float(raw_advantages.std()))
    sequence_samples = [
        (
            rollout,
            (advantages - advantage_mean) / advantage_std,
            returns,
        )
        for rollout, advantages, returns in prepared_rollouts
    ]
    policy_losses: list[float] = []
    value_losses: list[float] = []
    entropies: list[float] = []
    approx_kls: list[float] = []
    clipfracs: list[float] = []
    stopped_for_kl = False

    for _epoch in range(int(config.ppo_epochs)):
        permutation = torch.randperm(len(sequence_samples)).tolist()
        sequence_minibatches = []
        current_indices = []
        current_transition_count = 0
        for sequence_index in permutation:
            sequence_length = len(sequence_samples[sequence_index][0].transitions)
            if (
                current_indices
                and current_transition_count + sequence_length > int(config.minibatch_size)
            ):
                sequence_minibatches.append(current_indices)
                current_indices = []
                current_transition_count = 0
            current_indices.append(sequence_index)
            current_transition_count += sequence_length
        if current_indices:
            sequence_minibatches.append(current_indices)

        for sequence_indices in sequence_minibatches:
            new_log_probs = []
            values = []
            normalized_entropies = []
            old_log_prob_values = []
            old_value_values = []
            advantage_values = []
            return_values = []
            for sequence_index in sequence_indices:
                rollout, normalized_advantages, returns = sequence_samples[sequence_index]
                distributions, replay_values, _final_memory = _replay_rollout_with_memory(
                    model,
                    rollout,
                    device,
                )
                for step_index, (transition, distribution, value) in enumerate(
                    zip(rollout.transitions, distributions, replay_values)
                ):
                    stored_input = transition.policy_input
                    if not bool(stored_input.action_mask[int(transition.action)]):
                        raise ValueError(
                            "rollout contains an action that is masked in its stored state"
                        )
                    action = torch.tensor(
                        int(transition.action),
                        dtype=torch.long,
                        device=device,
                    )
                    new_log_probs.append(distribution.log_prob(action))
                    values.append(value)
                    normalized_entropies.append(
                        _normalized_entropy(distribution, stored_input.action_mask.to(device))
                    )
                    old_log_prob_values.append(float(transition.old_log_prob))
                    old_value_values.append(float(transition.value))
                    advantage_values.append(float(normalized_advantages[step_index]))
                    return_values.append(float(returns[step_index]))

            new_log_prob = torch.stack(new_log_probs)
            value_prediction = torch.stack(values)
            entropy = torch.stack(normalized_entropies).mean()
            old_log_prob = torch.tensor(
                old_log_prob_values,
                dtype=torch.float32,
                device=device,
            )
            old_value = torch.tensor(
                old_value_values,
                dtype=torch.float32,
                device=device,
            )
            advantage = torch.tensor(
                advantage_values,
                dtype=torch.float32,
                device=device,
            )
            return_value = torch.tensor(
                return_values,
                dtype=torch.float32,
                device=device,
            )

            log_ratio = new_log_prob - old_log_prob
            if not torch.isfinite(log_ratio).all() or not torch.isfinite(value_prediction).all():
                raise FloatingPointError("non-finite PPO graph batch")
            stable_log_ratio = torch.clamp(log_ratio, -20.0, 20.0)
            ratio = torch.exp(stable_log_ratio)
            unclipped_objective = ratio * advantage
            clipped_objective = torch.clamp(
                ratio,
                1.0 - float(config.clip_eps),
                1.0 + float(config.clip_eps),
            ) * advantage
            policy_loss = -torch.minimum(unclipped_objective, clipped_objective).mean()

            value_error = (return_value - value_prediction).pow(2)
            if float(config.value_clip_eps) > 0.0:
                clipped_value = old_value + torch.clamp(
                    value_prediction - old_value,
                    -float(config.value_clip_eps),
                    float(config.value_clip_eps),
                )
                clipped_error = (return_value - clipped_value).pow(2)
                value_loss = 0.5 * torch.maximum(value_error, clipped_error).mean()
            else:
                value_loss = 0.5 * value_error.mean()

            loss = (
                policy_loss
                + float(config.value_coef) * value_loss
                - float(config.entropy_coef) * entropy
            )
            if not torch.isfinite(loss):
                raise FloatingPointError("non-finite graph PPO loss")

            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), float(config.max_grad_norm))
            optimizer.step()

            with torch.no_grad():
                approx_kl = ((ratio - 1.0) - stable_log_ratio).mean()
                clipfrac = (
                    (ratio - 1.0).abs() > float(config.clip_eps)
                ).to(torch.float32).mean()
            policy_losses.append(float(policy_loss.item()))
            value_losses.append(float(value_loss.item()))
            entropies.append(float(entropy.item()))
            approx_kls.append(float(approx_kl.item()))
            clipfracs.append(float(clipfrac.item()))

            if float(config.target_kl) > 0.0 and float(approx_kl.item()) > float(config.target_kl):
                stopped_for_kl = True
                break
        if stopped_for_kl:
            break

    return {
        "policy_loss_mean": fmean(policy_losses) if policy_losses else 0.0,
        "value_loss_mean": fmean(value_losses) if value_losses else 0.0,
        "entropy_mean": fmean(entropies) if entropies else 0.0,
        "approx_kl_mean": fmean(approx_kls) if approx_kls else 0.0,
        "clipfrac_mean": fmean(clipfracs) if clipfracs else 0.0,
        "update_steps": len(policy_losses),
        "sample_count": len(raw_advantage_values),
        "stopped_for_kl": bool(stopped_for_kl),
    }
