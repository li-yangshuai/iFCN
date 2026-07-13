#!/usr/bin/env python3
"""Evaluate a universal graph policy on circuits and clock fields held out from training."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import csv
import glob
import json
import os
import time
from types import SimpleNamespace

import numpy as np
import torch

from utils import add_project_root

add_project_root()

from src.stochastic_clock import (  # noqa: E402
    CAUSAL_CLOCK_MODES,
    ClockEvaluation,
    aggregate_clock_evaluations,
    sample_clock_field,
)
from src.layout_retrieval_memory import LayoutRetrievalMemory  # noqa: E402
from src.memory_policy_bridge import retrieve_policy_memory  # noqa: E402
from src.universal_graph_policy import (  # noqa: E402
    UniversalGraphPolicy,
    build_episode_feedback_features,
    build_graph_policy_input,
    predict_route_edge_priorities,
)
from src.universal_graph_ppo import graph_memory_policy_step  # noqa: E402
from train_layout_ppo import clone_positions, resolve_device, set_global_seed  # noqa: E402
from train_universal_graph_ppo import (  # noqa: E402
    accepted_policy_candidate,
    clock_aligned_start_positions,
    exact_evaluate_positions,
    field_bounds_for_positions,
    prepare_context,
    sample_field,
)


RUNTIME_DEFAULTS = {
    "seed": 20260710,
    "parse_mode": "auto",
    "disable_gcn_cache": False,
    "x_spacing": 2,
    "y_spacing": 2,
    "start_layout_strategy": "auto",
    "start_layout_orientation": "auto",
    "phase_count": 4,
    "padding": 3,
    "max_same_phase": 4,
    "clock_mode": "stochastic-bands",
    "secondary_advance_probability_min": 0.25,
    "secondary_advance_probability_max": 0.75,
    "clock_aligned_start": True,
    "steps_per_episode": 12,
    "rollback_worse_actions": False,
    "exact_eval_timeout_sec": 20,
    "cvar_alpha": 0.9,
    "aspect_ratio_limit": 4.0,
    "aspect_ratio_weight": 12.0,
    "max_span_weight": 1.0,
    "area_regression_weight": 20.0,
    "area_reward_weight": 2.0,
}


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Evaluate one dynamic graph-policy checkpoint on explicitly selected "
            "circuits and frozen causal clock realizations."
        )
    )
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--benchmarks", nargs="*", default=None)
    parser.add_argument("--benchmark-glob", default=None)
    parser.add_argument("--output-dir", default=None)
    parser.add_argument("--seed", type=int, default=20260711, help="Evaluation/clock seed.")
    parser.add_argument("--preprocess-seed", type=int, default=None)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--parse-mode", choices=("auto", "compact", "layered"), default=None)
    parser.add_argument("--disable-gcn-cache", action="store_true")
    parser.add_argument("--clock-field-samples", type=int, default=16)
    parser.add_argument("--policy-trials", type=int, default=1)
    parser.add_argument("--stochastic-actions", action="store_true")
    parser.add_argument("--steps-per-episode", type=int, default=None)
    parser.add_argument("--phase-count", type=int, choices=(3, 4), default=None)
    parser.add_argument("--padding", type=int, default=None)
    parser.add_argument("--max-same-phase", type=int, default=None)
    parser.add_argument("--clock-mode", choices=CAUSAL_CLOCK_MODES, default=None)
    parser.add_argument(
        "--clock-aligned-start",
        action=argparse.BooleanOptionalAction,
        default=None,
    )
    parser.add_argument("--secondary-advance-probability-min", type=float, default=None)
    parser.add_argument("--secondary-advance-probability-max", type=float, default=None)
    parser.add_argument("--exact-eval-timeout-sec", type=int, default=None)
    parser.add_argument("--cvar-alpha", type=float, default=None)
    parser.add_argument(
        "--retrieval-memory",
        default=None,
        help="Override the retrieval memory recorded in the checkpoint.",
    )
    parser.add_argument("--retrieval-top-k", type=int, default=4)
    parser.add_argument("--disable-retrieval-memory", action="store_true")
    parser.add_argument(
        "--allow-exact-memory-retrieval",
        action="store_true",
        help="Allow same-topology exemplars; disabled by default for held-out evaluation.",
    )
    parser.add_argument(
        "--require-unseen",
        action="store_true",
        help="Fail if any evaluation benchmark also appears in checkpoint training metadata.",
    )
    parser.add_argument(
        "--fail-fast",
        action="store_true",
        help="Abort on the first circuit preparation/evaluation error instead of recording it.",
    )
    return parser.parse_args()


def resolve_evaluation_benchmarks(args) -> list[str]:
    paths = list(args.benchmarks or ())
    if args.benchmark_glob:
        paths.extend(glob.glob(args.benchmark_glob))
    resolved = sorted({os.path.abspath(path) for path in paths if os.path.isfile(path)})
    if not resolved:
        raise FileNotFoundError(
            "no evaluation benchmark matched; pass --benchmarks or --benchmark-glob explicitly"
        )
    return resolved


def load_checkpoint(path: str, device: torch.device):
    checkpoint_path = os.path.abspath(path)
    if not os.path.isfile(checkpoint_path):
        raise FileNotFoundError(checkpoint_path)
    try:
        payload = torch.load(checkpoint_path, map_location=device, weights_only=False)
    except TypeError:  # PyTorch before the weights_only keyword.
        payload = torch.load(checkpoint_path, map_location=device)
    if not isinstance(payload, dict) or not payload.get("universal_dynamic_graph_policy"):
        raise ValueError("checkpoint is not a universal dynamic graph-policy checkpoint")
    model_config = dict(payload.get("model", {}))
    required = (
        "node_feature_dim",
        "edge_feature_dim",
        "clock_feature_dim",
        "action_feature_dim",
        "action_type_count",
        "retrieval_feature_dim",
        "episode_feature_dim",
        "memory_dim",
        "hidden_dim",
        "message_passing_steps",
    )
    missing = [name for name in required if name not in model_config]
    if missing:
        raise ValueError(f"checkpoint model metadata is incomplete: {missing}")
    model = UniversalGraphPolicy(
        node_feature_dim=int(model_config["node_feature_dim"]),
        edge_feature_dim=int(model_config["edge_feature_dim"]),
        clock_feature_dim=int(model_config["clock_feature_dim"]),
        action_feature_dim=int(model_config["action_feature_dim"]),
        action_type_count=int(model_config["action_type_count"]),
        retrieval_feature_dim=int(model_config["retrieval_feature_dim"]),
        episode_feature_dim=int(model_config["episode_feature_dim"]),
        memory_dim=int(model_config["memory_dim"]),
        hidden_dim=int(model_config["hidden_dim"]),
        message_passing_steps=int(model_config["message_passing_steps"]),
    ).to(device)
    model.load_state_dict(payload["model_state_dict"], strict=True)
    model.eval()
    return checkpoint_path, payload, model


def build_runtime_args(cli_args, checkpoint_payload) -> SimpleNamespace:
    settings = dict(RUNTIME_DEFAULTS)
    settings.update(checkpoint_payload.get("config", {}))
    settings["device"] = cli_args.device
    settings["disable_gcn_cache"] = bool(cli_args.disable_gcn_cache)
    overrides = {
        "seed": cli_args.preprocess_seed,
        "parse_mode": cli_args.parse_mode,
        "steps_per_episode": cli_args.steps_per_episode,
        "phase_count": cli_args.phase_count,
        "padding": cli_args.padding,
        "max_same_phase": cli_args.max_same_phase,
        "clock_mode": cli_args.clock_mode,
        "clock_aligned_start": cli_args.clock_aligned_start,
        "secondary_advance_probability_min": cli_args.secondary_advance_probability_min,
        "secondary_advance_probability_max": cli_args.secondary_advance_probability_max,
        "exact_eval_timeout_sec": cli_args.exact_eval_timeout_sec,
        "cvar_alpha": cli_args.cvar_alpha,
    }
    for name, value in overrides.items():
        if value is not None:
            settings[name] = value
    return SimpleNamespace(**settings)


def _evaluation_key(evaluation: ClockEvaluation) -> tuple[float, ...]:
    return (
        float(not evaluation.legal),
        float(evaluation.violation_count),
        float(evaluation.cost),
        float(evaluation.area),
    )


def _run_policy_trial(
    context,
    field,
    model,
    device,
    runtime_args,
    deterministic,
    frozen_memory,
    start_positions=None,
):
    env = context.env
    env.reset(start_positions=start_positions)
    actions = []
    reward_total = 0.0
    terminal_policy_candidate = None
    memory_state = model.initial_memory_state(device)
    episode_features = build_episode_feedback_features(env)
    for step in range(int(runtime_args.steps_per_episode)):
        policy_input = build_graph_policy_input(
            env,
            field,
            retrieval_features=frozen_memory.features,
            episode_features=episode_features,
            memory_state=memory_state,
            retrieval_node_hints=frozen_memory.node_hints,
        )
        action, log_probability, value, next_memory = graph_memory_policy_step(
            model,
            policy_input,
            device,
            deterministic=deterministic,
        )
        concrete_action = env.action_defs[action]
        concretize = getattr(env, "_concretize_action", None)
        if callable(concretize):
            concrete_action = concretize(concrete_action)
        _observation, reward, done, info = env.step(action)
        produced_candidate = accepted_policy_candidate(env, info, step)
        if produced_candidate is not None:
            terminal_policy_candidate = produced_candidate
        memory_state = next_memory
        episode_features = build_episode_feedback_features(
            env,
            info,
            reward=reward,
            step_index=step + 1,
            max_steps=runtime_args.steps_per_episode,
        )
        reward_total += float(reward)
        actions.append(
            {
                "step": int(step),
                "action_index": int(action),
                "action": repr(concrete_action),
                "log_probability": float(log_probability),
                "value": float(value),
                "reward": float(reward),
                "candidate_area": float(info.get("candidate_area", env.current_result["area"])),
                "accepted": bool(info.get("accepted_action", not info.get("invalid_action", False))),
            }
        )
        if done:
            break
    final_policy_input = build_graph_policy_input(
        env,
        field,
        retrieval_features=frozen_memory.features,
        episode_features=episode_features,
        memory_state=memory_state,
        retrieval_node_hints=frozen_memory.node_hints,
    )
    route_edge_priorities = predict_route_edge_priorities(
        model,
        env,
        final_policy_input,
        device,
    )
    terminal_positions = (
        terminal_policy_candidate["positions"]
        if terminal_policy_candidate is not None
        else clone_positions(env.best_positions)
    )
    terminal_selection_key = (
        tuple(env._selection_key(env.current_result, env.current_cost))
        if terminal_policy_candidate is not None
        else tuple(env.best_key)
    )
    terminal_proxy_area = (
        float(env.current_result["area"])
        if terminal_policy_candidate is not None
        else float(env.best_result["area"])
    )
    return {
        "positions": terminal_positions,
        "selection_key": terminal_selection_key,
        "proxy_area": terminal_proxy_area,
        "reward_total": float(reward_total),
        "actions": actions,
        "memory_entry_ids": list(frozen_memory.entry_ids),
        "memory_exact_count": int(frozen_memory.exact_count),
        "memory_best_similarity": float(frozen_memory.best_similarity),
        "route_edge_priorities": route_edge_priorities,
    }


def evaluate_circuit(
    context,
    model,
    device,
    runtime_args,
    cli_args,
    rng,
    retrieval_memory,
):
    policy_evaluations = []
    baseline_evaluations = []
    scenarios = []
    policy_better_count = 0

    for field_index in range(int(cli_args.clock_field_samples)):
        context.env.reset()
        field = sample_field(
            context.env.current_positions,
            context.env.orientation,
            runtime_args,
            rng,
        )
        start_positions = None
        if bool(runtime_args.clock_aligned_start):
            start_positions = clock_aligned_start_positions(
                context.env,
                field,
                runtime_args,
            )
            field = sample_clock_field(
                field_bounds_for_positions(start_positions, runtime_args),
                field.spec,
            )
        frozen_memory = retrieve_policy_memory(
            retrieval_memory,
            context.env,
            field,
            top_k=cli_args.retrieval_top_k,
            exclude_exact_topology=not bool(cli_args.allow_exact_memory_retrieval),
        )
        selected_trial = None
        for trial_index in range(int(cli_args.policy_trials)):
            trial = _run_policy_trial(
                context,
                field,
                model,
                device,
                runtime_args,
                deterministic=not cli_args.stochastic_actions,
                frozen_memory=frozen_memory,
                start_positions=start_positions,
            )
            trial["trial_index"] = int(trial_index)
            if selected_trial is None or trial["selection_key"] < selected_trial["selection_key"]:
                selected_trial = trial

        policy_metrics, policy_records = exact_evaluate_positions(
            context,
            selected_trial["positions"],
            [field],
            runtime_args,
            edge_priorities=selected_trial["route_edge_priorities"],
        )
        baseline_metrics, baseline_records = exact_evaluate_positions(
            context,
            context.warm_start["node_positions"],
            [field],
            runtime_args,
        )
        policy_evaluation = ClockEvaluation(**policy_records[0])
        baseline_evaluation = ClockEvaluation(**baseline_records[0])
        policy_evaluations.append(policy_evaluation)
        baseline_evaluations.append(baseline_evaluation)
        policy_is_better = _evaluation_key(policy_evaluation) < _evaluation_key(baseline_evaluation)
        policy_better_count += int(policy_is_better)
        scenarios.append(
            {
                "field_index": int(field_index),
                "field_seed": int(field.spec.seed),
                "field_hash": field.field_hash,
                "clock_spec": asdict(field.spec),
                "policy": asdict(policy_evaluation),
                "baseline": asdict(baseline_evaluation),
                "policy_better": bool(policy_is_better),
                "selected_trial": {
                    "trial_index": int(selected_trial["trial_index"]),
                    "proxy_area": float(selected_trial["proxy_area"]),
                    "reward_total": float(selected_trial["reward_total"]),
                    "positions": {
                        str(node_id): [int(coord[0]), int(coord[1])]
                        for node_id, coord in selected_trial["positions"].items()
                    },
                    "actions": selected_trial["actions"],
                    "memory_entry_ids": selected_trial["memory_entry_ids"],
                    "memory_exact_count": int(selected_trial["memory_exact_count"]),
                    "memory_best_similarity": float(
                        selected_trial["memory_best_similarity"]
                    ),
                },
                "policy_single_field_metrics": asdict(policy_metrics),
                "baseline_single_field_metrics": asdict(baseline_metrics),
            }
        )

    policy_metrics = aggregate_clock_evaluations(
        policy_evaluations,
        cvar_alpha=runtime_args.cvar_alpha,
    )
    baseline_metrics = aggregate_clock_evaluations(
        baseline_evaluations,
        cvar_alpha=runtime_args.cvar_alpha,
    )
    return {
        "policy_metrics": asdict(policy_metrics),
        "baseline_metrics": asdict(baseline_metrics),
        "policy_better_rate": float(policy_better_count) / float(len(scenarios)),
        "success_rate_delta": float(policy_metrics.success_rate - baseline_metrics.success_rate),
        "mean_cost_delta": float(policy_metrics.mean_cost - baseline_metrics.mean_cost),
        "robust_loss_delta": float(policy_metrics.robust_loss - baseline_metrics.robust_loss),
        "policy_evaluations": policy_evaluations,
        "baseline_evaluations": baseline_evaluations,
        "scenarios": scenarios,
    }


def write_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as output:
        json.dump(payload, output, ensure_ascii=False, indent=2)


def write_circuit_csv(path, circuit_rows):
    if not circuit_rows:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(circuit_rows[0].keys()))
        writer.writeheader()
        writer.writerows(circuit_rows)


def load_evaluation_memory(cli_args, checkpoint):
    if cli_args.disable_retrieval_memory:
        return None, None
    memory_path = cli_args.retrieval_memory or checkpoint.get("retrieval_memory")
    if not memory_path:
        return None, None
    memory_path = os.path.abspath(memory_path)
    if not os.path.isfile(memory_path):
        raise FileNotFoundError(
            f"retrieval memory recorded by checkpoint is missing: {memory_path}"
        )
    memory = LayoutRetrievalMemory.load(memory_path)
    return memory, memory_path


def main():
    cli_args = parse_args()
    if int(cli_args.clock_field_samples) <= 0 or int(cli_args.policy_trials) <= 0:
        raise ValueError("clock-field-samples and policy-trials must be positive")
    if int(cli_args.policy_trials) > 1 and not cli_args.stochastic_actions:
        raise ValueError("policy-trials > 1 requires --stochastic-actions")
    set_global_seed(cli_args.seed)
    rng = np.random.default_rng(cli_args.seed)
    device = resolve_device(cli_args.device)
    checkpoint_path, checkpoint, model = load_checkpoint(cli_args.checkpoint, device)
    retrieval_memory, retrieval_memory_path = load_evaluation_memory(cli_args, checkpoint)
    runtime_args = build_runtime_args(cli_args, checkpoint)
    benchmarks = resolve_evaluation_benchmarks(cli_args)
    training_benchmarks = {
        os.path.abspath(path) for path in checkpoint.get("benchmarks", ())
    }
    overlap = [path for path in benchmarks if path in training_benchmarks]
    if cli_args.require_unseen and overlap:
        raise ValueError(f"evaluation set overlaps checkpoint training set: {overlap}")

    output_dir = os.path.abspath(
        cli_args.output_dir
        or os.path.join(os.path.dirname(checkpoint_path), f"evaluation_seed_{cli_args.seed}")
    )
    os.makedirs(output_dir, exist_ok=True)
    print(
        f"[Universal-Eval] circuits={len(benchmarks)} fields={cli_args.clock_field_samples} "
        f"device={device} deterministic={not cli_args.stochastic_actions}"
    )

    started = time.perf_counter()
    circuit_payloads = []
    circuit_rows = []
    all_policy_evaluations = []
    all_baseline_evaluations = []
    evaluation_failures = []
    for circuit_index, benchmark in enumerate(benchmarks):
        seen_in_training = benchmark in training_benchmarks
        try:
            context = prepare_context(benchmark, runtime_args)
            result = evaluate_circuit(
                context,
                model,
                device,
                runtime_args,
                cli_args,
                rng,
                retrieval_memory,
            )
        except Exception as exc:
            if cli_args.fail_fast:
                raise
            failure = {
                "circuit_index": int(circuit_index),
                "benchmark": benchmark,
                "seen_in_training": bool(seen_in_training),
                "status": "error",
                "error_type": type(exc).__name__,
                "error": str(exc),
            }
            evaluation_failures.append(failure)
            circuit_payloads.append(failure)
            circuit_rows.append(
                {
                    "circuit_index": int(circuit_index),
                    "benchmark": benchmark,
                    "seen_in_training": bool(seen_in_training),
                    "status": "error",
                    "error": f"{type(exc).__name__}: {exc}",
                    "policy_success_rate": "",
                    "baseline_success_rate": "",
                    "success_rate_delta": "",
                    "policy_mean_cost": "",
                    "baseline_mean_cost": "",
                    "policy_cvar_violations": "",
                    "baseline_cvar_violations": "",
                    "policy_better_rate": "",
                }
            )
            print(
                f"[Universal-Eval] {os.path.basename(benchmark)} "
                f"status=error error={type(exc).__name__}: {exc}"
            )
            continue
        policy_metrics = result["policy_metrics"]
        baseline_metrics = result["baseline_metrics"]
        all_policy_evaluations.extend(result.pop("policy_evaluations"))
        all_baseline_evaluations.extend(result.pop("baseline_evaluations"))
        circuit_payload = {
            "circuit_index": int(circuit_index),
            "benchmark": benchmark,
            "seen_in_training": bool(seen_in_training),
            "status": "ok",
            **result,
        }
        circuit_payloads.append(circuit_payload)
        circuit_rows.append(
            {
                "circuit_index": int(circuit_index),
                "benchmark": benchmark,
                "seen_in_training": bool(seen_in_training),
                "status": "ok",
                "error": "",
                "policy_success_rate": float(policy_metrics["success_rate"]),
                "baseline_success_rate": float(baseline_metrics["success_rate"]),
                "success_rate_delta": float(result["success_rate_delta"]),
                "policy_mean_cost": float(policy_metrics["mean_cost"]),
                "baseline_mean_cost": float(baseline_metrics["mean_cost"]),
                "policy_cvar_violations": float(policy_metrics["cvar_violations"]),
                "baseline_cvar_violations": float(baseline_metrics["cvar_violations"]),
                "policy_better_rate": float(result["policy_better_rate"]),
            }
        )
        print(
            f"[Universal-Eval] {os.path.basename(benchmark)} "
            f"unseen={not seen_in_training} "
            f"success={policy_metrics['success_rate']:.3f} "
            f"baseline={baseline_metrics['success_rate']:.3f} "
            f"better={result['policy_better_rate']:.3f}"
        )

    if not all_policy_evaluations:
        raise RuntimeError("every evaluation circuit failed before producing exact results")
    overall_policy = aggregate_clock_evaluations(
        all_policy_evaluations,
        cvar_alpha=runtime_args.cvar_alpha,
    )
    overall_baseline = aggregate_clock_evaluations(
        all_baseline_evaluations,
        cvar_alpha=runtime_args.cvar_alpha,
    )
    summary = {
        "schema_version": 2,
        "checkpoint": checkpoint_path,
        "checkpoint_episode": int(checkpoint.get("episode", checkpoint.get("epoch", 0))),
        "evaluation_seed": int(cli_args.seed),
        "circuit_count": len(benchmarks),
        "evaluated_circuit_count": len(benchmarks) - len(evaluation_failures),
        "evaluation_failure_count": len(evaluation_failures),
        "evaluation_failures": evaluation_failures,
        "unseen_circuit_count": sum(path not in training_benchmarks for path in benchmarks),
        "clock_fields_per_circuit": int(cli_args.clock_field_samples),
        "policy_trials": int(cli_args.policy_trials),
        "deterministic_actions": bool(not cli_args.stochastic_actions),
        "clock_mode": str(runtime_args.clock_mode),
        "phase_count": int(runtime_args.phase_count),
        "retrieval_memory": retrieval_memory_path,
        "retrieval_memory_entries": (
            int(len(retrieval_memory)) if retrieval_memory is not None else 0
        ),
        "exact_memory_retrieval_allowed": bool(cli_args.allow_exact_memory_retrieval),
        "overall_policy_metrics": asdict(overall_policy),
        "overall_baseline_metrics": asdict(overall_baseline),
        "overall_success_rate_delta": float(
            overall_policy.success_rate - overall_baseline.success_rate
        ),
        "overall_robust_loss_delta": float(
            overall_policy.robust_loss - overall_baseline.robust_loss
        ),
        "elapsed_sec": float(time.perf_counter() - started),
    }
    write_json(os.path.join(output_dir, "universal_graph_evaluation.json"), {
        "summary": summary,
        "circuits": circuit_payloads,
    })
    write_json(os.path.join(output_dir, "universal_graph_evaluation_summary.json"), summary)
    write_circuit_csv(
        os.path.join(output_dir, "universal_graph_evaluation_circuits.csv"),
        circuit_rows,
    )
    print(f"[Universal-Eval] completed: {json.dumps(summary, ensure_ascii=False)}")


if __name__ == "__main__":
    main()
