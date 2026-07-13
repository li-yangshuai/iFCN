#!/usr/bin/env python3
"""Train one dynamic GNN+PPO policy across circuits and frozen clock fields."""

from __future__ import annotations

import argparse
import csv
from dataclasses import asdict, dataclass
import glob
import json
import os
import time

import numpy as np
import torch

from utils import add_project_root

add_project_root()

from src.circuit_parse import CircuitParser
from src.stochastic_clock import (
    CAUSAL_CLOCK_MODES,
    ClockEvaluation,
    ClockField,
    ClockFieldSpec,
    RobustClockMetrics,
    aggregate_clock_evaluations,
    sample_clock_field,
)
from src.layout_retrieval_memory import LayoutRetrievalMemory
from src.memory_policy_bridge import (
    remember_exact_layout,
    retrieve_policy_memory,
)
from src.universal_graph_policy import (
    UniversalGraphPolicy,
    build_episode_feedback_features,
    build_graph_policy_input,
    predict_route_edge_priorities,
)
from src.universal_graph_ppo import (
    GraphPPOConfig,
    GraphRollout,
    GraphTransition,
    bootstrap_graph_value,
    graph_memory_policy_step,
    graph_ppo_update,
)
from train_layout_ppo import (
    DEFAULT_AREA_REGRESSION_WEIGHT,
    DEFAULT_AREA_REWARD_WEIGHT,
    DEFAULT_ASPECT_RATIO_LIMIT,
    DEFAULT_ASPECT_RATIO_WEIGHT,
    DEFAULT_MAX_SPAN_WEIGHT,
    LEFT_RIGHT,
    TOP_DOWN,
    LayoutCompactionEnv,
    build_embedding_score_map,
    clone_positions,
    evaluate_layout_candidate_with_timeout,
    is_legal_layout_result,
    load_or_generate_gcn_layout,
    normalize_layers,
    resolve_device,
    scalarize_layout_result,
    select_fast_warm_start,
    set_global_seed,
)


DEFAULT_BENCHMARK_GLOB = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../../../../tests/benchmarks_f/TOY/*.v")
)
DEFAULT_OUTPUT_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "../../../results/universal_graph_ppo")
)


@dataclass
class CircuitContext:
    benchmark: str
    circuit: CircuitParser
    ordered_layers: list[list[int]]
    embedding_scores: dict[int, float]
    warm_start: dict
    env: LayoutCompactionEnv
    robust_best_positions: dict[int, tuple[int, int]] | None = None
    robust_best_key: tuple[float, ...] | None = None
    robust_best_record: dict | None = None


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Train a size-independent directed-GNN PPO actor on multiple circuits "
            "and frozen causal stochastic clock fields."
        )
    )
    parser.add_argument("--benchmark-glob", default=DEFAULT_BENCHMARK_GLOB)
    parser.add_argument("--benchmarks", nargs="*", default=None)
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--seed", type=int, default=20260710)
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--parse-mode", choices=("auto", "compact", "layered"), default="auto")
    parser.add_argument("--disable-gcn-cache", action="store_true")
    parser.add_argument("--x-spacing", type=int, default=2)
    parser.add_argument("--y-spacing", type=int, default=2)
    parser.add_argument(
        "--start-layout-strategy",
        choices=("auto", "fixed", "shifted", "adaptive", "gcn"),
        default="auto",
    )
    parser.add_argument(
        "--start-layout-orientation",
        choices=("auto", LEFT_RIGHT, TOP_DOWN),
        default="auto",
    )
    parser.add_argument("--phase-count", type=int, choices=(3, 4), default=4)
    parser.add_argument("--padding", type=int, default=3)
    parser.add_argument("--max-same-phase", type=int, default=4)
    parser.add_argument(
        "--clock-mode",
        choices=CAUSAL_CLOCK_MODES,
        default="stochastic-bands",
    )
    parser.add_argument("--secondary-advance-probability-min", type=float, default=0.25)
    parser.add_argument("--secondary-advance-probability-max", type=float, default=0.75)
    parser.add_argument(
        "--clock-aligned-start",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Construct a compact topology-monotone start placement for each "
            "sampled causal clock direction."
        ),
    )
    parser.add_argument("--episodes", type=int, default=1000)
    parser.add_argument("--episodes-per-update", type=int, default=16)
    parser.add_argument("--steps-per-episode", type=int, default=12)
    parser.add_argument("--elite-start-probability", type=float, default=0.20)
    parser.add_argument(
        "--rollback-worse-actions",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Disabled by default during training so PPO can cross local barriers.",
    )
    parser.add_argument("--hidden-dim", type=int, default=128)
    parser.add_argument("--message-passing-steps", type=int, default=3)
    parser.add_argument("--memory-dim", type=int, default=64)
    parser.add_argument(
        "--freeze-offline-backbone",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Keep IFCN-pretrained graph/route encoders fixed during PPO and train "
            "only episode memory plus actor/critic decision layers."
        ),
    )
    parser.add_argument(
        "--pretrained-policy",
        default=None,
        help="Optional IFCN offline-pretrained dynamic graph-policy checkpoint.",
    )
    parser.add_argument(
        "--retrieval-memory",
        default=None,
        help="Persistent layout retrieval JSON; inferred from the pretrained checkpoint when possible.",
    )
    parser.add_argument("--retrieval-top-k", type=int, default=4)
    parser.add_argument("--disable-retrieval-memory", action="store_true")
    parser.add_argument(
        "--exact-topology-memory",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Allow exact-topology IFCN exemplars during training.",
    )
    parser.add_argument(
        "--online-memory-min-success-rate",
        type=float,
        default=1.0,
        help="Write exact layouts back to memory at or above this multi-field success rate.",
    )
    parser.add_argument("--learning-rate", type=float, default=3e-4)
    parser.add_argument("--ppo-epochs", type=int, default=4)
    parser.add_argument("--minibatch-size", type=int, default=32)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--gae-lambda", type=float, default=0.95)
    parser.add_argument("--clip-eps", type=float, default=0.2)
    parser.add_argument("--value-clip-eps", type=float, default=0.2)
    parser.add_argument("--value-coef", type=float, default=0.5)
    parser.add_argument("--entropy-coef", type=float, default=0.01)
    parser.add_argument("--max-grad-norm", type=float, default=1.0)
    parser.add_argument("--target-kl", type=float, default=0.03)
    parser.add_argument(
        "--exact-feedback-interval",
        type=int,
        default=10,
        help=(
            "Exact-route a circuit's episode-best placement every N visits to that "
            "circuit; 0 disables."
        ),
    )
    parser.add_argument("--exact-field-samples", type=int, default=2)
    parser.add_argument("--exact-eval-timeout-sec", type=int, default=20)
    parser.add_argument("--cvar-alpha", type=float, default=0.9)
    parser.add_argument("--exact-legal-bonus", type=float, default=30.0)
    parser.add_argument("--exact-violation-penalty", type=float, default=8.0)
    parser.add_argument("--exact-area-reward-weight", type=float, default=4.0)
    parser.add_argument("--aspect-ratio-limit", type=float, default=DEFAULT_ASPECT_RATIO_LIMIT)
    parser.add_argument("--aspect-ratio-weight", type=float, default=DEFAULT_ASPECT_RATIO_WEIGHT)
    parser.add_argument("--max-span-weight", type=float, default=DEFAULT_MAX_SPAN_WEIGHT)
    parser.add_argument("--area-regression-weight", type=float, default=DEFAULT_AREA_REGRESSION_WEIGHT)
    parser.add_argument("--area-reward-weight", type=float, default=DEFAULT_AREA_REWARD_WEIGHT)
    parser.add_argument("--checkpoint-interval", type=int, default=100)
    parser.add_argument("--log-interval", type=int, default=10)
    return parser.parse_args()


def resolve_benchmarks(args) -> list[str]:
    paths = args.benchmarks if args.benchmarks else glob.glob(args.benchmark_glob)
    resolved = sorted({os.path.abspath(path) for path in paths if os.path.isfile(path)})
    if not resolved:
        raise FileNotFoundError("no Verilog benchmarks matched the requested inputs")
    return resolved


def prepare_context(path: str, args) -> CircuitContext:
    circuit = CircuitParser(path, parse_mode=args.parse_mode)
    embeddings, layer_container, _crossings, _edges = load_or_generate_gcn_layout(
        circuit,
        path,
        args.seed,
        use_cache=not args.disable_gcn_cache,
        device=args.device,
    )
    ordered_layers = normalize_layers(layer_container)
    embedding_scores = build_embedding_score_map(circuit, ordered_layers, embeddings)
    allowed_strategies = (
        None
        if args.start_layout_strategy == "auto"
        else (args.start_layout_strategy,)
    )
    allowed_orientations = (
        None
        if args.start_layout_orientation == "auto"
        else (args.start_layout_orientation,)
    )
    board_margin = max(2, int(args.padding) + 1)
    warm_start = select_fast_warm_start(
        circuit,
        ordered_layers,
        board_margin,
        args.x_spacing,
        args.y_spacing,
        embeddings,
        embedding_scores,
        allowed_strategies,
        allowed_orientations,
        "legal-area",
        args.aspect_ratio_limit,
        args.aspect_ratio_weight,
        args.max_span_weight,
        args.area_regression_weight,
    )
    if warm_start is None:
        raise RuntimeError(f"failed to build warm start for {path}")
    env = LayoutCompactionEnv(
        circuit=circuit,
        ordered_layers=ordered_layers,
        embedding_scores=embedding_scores,
        start_result=warm_start,
        phase_cycle=args.phase_count,
        padding=args.padding,
        max_same_phase=args.max_same_phase,
        max_steps=args.steps_per_episode,
        enable_secondary_squeeze=False,
        aspect_ratio_limit=args.aspect_ratio_limit,
        aspect_ratio_weight=args.aspect_ratio_weight,
        max_span_weight=args.max_span_weight,
        area_regression_weight=args.area_regression_weight,
        best_selection_mode="legal-area",
        area_reward_weight=args.area_reward_weight,
        train_eval_mode="placement",
        exact_eval_timeout_sec=args.exact_eval_timeout_sec,
        rollback_worse_actions=args.rollback_worse_actions,
    )
    return CircuitContext(
        benchmark=path,
        circuit=circuit,
        ordered_layers=ordered_layers,
        embedding_scores=embedding_scores,
        warm_start=warm_start,
        env=env,
    )


def field_bounds_for_positions(positions, args):
    xs = [int(coord[0]) for coord in positions.values()]
    ys = [int(coord[1]) for coord in positions.values()]
    action_radius = max(8, int(args.steps_per_episode) * 6)
    margin = int(args.padding) + action_radius + 4
    return min(xs) - margin, min(ys) - margin, max(xs) + margin, max(ys) + margin


def sample_field(positions, orientation, args, rng: np.random.Generator) -> ClockField:
    probability_min = min(
        float(args.secondary_advance_probability_min),
        float(args.secondary_advance_probability_max),
    )
    probability_max = max(
        float(args.secondary_advance_probability_min),
        float(args.secondary_advance_probability_max),
    )
    spec = ClockFieldSpec(
        seed=int(rng.integers(1, np.iinfo(np.int32).max)),
        phase_count=int(args.phase_count),
        mode=str(args.clock_mode),
        primary_axis="x" if orientation == LEFT_RIGHT else "y",
        primary_direction=1,
        secondary_direction=int(rng.choice((-1, 1))),
        secondary_advance_probability=float(rng.uniform(probability_min, probability_max)),
    )
    return sample_clock_field(field_bounds_for_positions(positions, args), spec)


def resample_compatible_field(
    reference: ClockField,
    positions,
    args,
    rng: np.random.Generator,
) -> ClockField:
    """Resample band texture without making one placement face opposite causality."""

    probability_min = min(
        float(args.secondary_advance_probability_min),
        float(args.secondary_advance_probability_max),
    )
    probability_max = max(
        float(args.secondary_advance_probability_min),
        float(args.secondary_advance_probability_max),
    )
    spec = ClockFieldSpec(
        seed=int(rng.integers(1, np.iinfo(np.int32).max)),
        phase_count=int(reference.spec.phase_count),
        mode=str(reference.spec.mode),
        primary_axis=str(reference.spec.primary_axis),
        primary_direction=int(reference.spec.primary_direction),
        secondary_direction=int(reference.spec.secondary_direction),
        secondary_advance_probability=float(rng.uniform(probability_min, probability_max)),
    )
    return sample_clock_field(field_bounds_for_positions(positions, args), spec)


def clock_aligned_start_positions(env, field: ClockField, args):
    """Place every logical edge monotonically in both causal field axes.

    The primary coordinate follows logic layers.  Within each layer, nodes are
    packed into the smallest free secondary coordinates that are no earlier
    than all already placed fanins.  This is substantially smaller than a full
    ``layer * max_width`` diagonal expansion while preserving causality.
    """

    layers = [list(map(int, layer)) for layer in env.ordered_layers]
    node_to_layer = {
        node_id: layer_index
        for layer_index, layer in enumerate(layers)
        for node_id in layer
    }
    effective_edges = [
        (int(source), int(target))
        for source, target in env.circuit.effective_edges
        if int(source) in node_to_layer and int(target) in node_to_layer
    ]
    predecessors = {node_id: [] for node_id in node_to_layer}
    for source, target in effective_edges:
        predecessors[target].append(source)

    logical_secondary: dict[int, int] = {}
    for layer_index, layer in enumerate(layers):
        original_rank = {node_id: rank for rank, node_id in enumerate(layer)}
        internal_successors = {node_id: [] for node_id in layer}
        internal_indegree = {node_id: 0 for node_id in layer}
        for source, target in effective_edges:
            if source in internal_successors and target in internal_successors:
                internal_successors[source].append(target)
                internal_indegree[target] += 1
        ready = sorted(
            (node_id for node_id in layer if internal_indegree[node_id] == 0),
            key=lambda node_id: original_rank[node_id],
        )
        ordered = []
        while ready:
            node_id = ready.pop(0)
            ordered.append(node_id)
            for target in internal_successors[node_id]:
                internal_indegree[target] -= 1
                if internal_indegree[target] == 0:
                    ready.append(target)
                    ready.sort(key=lambda value: original_rank[value])
        ordered.extend(node_id for node_id in layer if node_id not in ordered)

        used_coordinates: set[int] = set()
        for node_id in ordered:
            lower_bound = max(
                (
                    logical_secondary[parent]
                    for parent in predecessors[node_id]
                    if parent in logical_secondary
                ),
                default=0,
            )
            coordinate = int(lower_bound)
            while coordinate in used_coordinates:
                coordinate += 1
            logical_secondary[node_id] = coordinate
            used_coordinates.add(coordinate)

    direction = int(field.spec.secondary_direction)
    raw_secondary = {
        node_id: direction * coordinate
        for node_id, coordinate in logical_secondary.items()
    }
    margin = max(2, int(args.padding) + 1)
    minimum_secondary = min(raw_secondary.values(), default=0)
    positions = {}
    for node_id, layer_index in node_to_layer.items():
        primary = margin + 2 * int(layer_index)
        secondary = margin + raw_secondary[node_id] - minimum_secondary
        positions[node_id] = (
            (primary, secondary)
            if env.orientation == LEFT_RIGHT
            else (secondary, primary)
        )
    if len(set(positions.values())) != len(positions):
        raise RuntimeError("clock-aligned start placement contains node collisions")
    for source, target in effective_edges:
        source_secondary = (
            positions[source][1] if env.orientation == LEFT_RIGHT else positions[source][0]
        )
        target_secondary = (
            positions[target][1] if env.orientation == LEFT_RIGHT else positions[target][0]
        )
        if direction * (target_secondary - source_secondary) < 0:
            raise RuntimeError("clock-aligned placement violates secondary causality")
    return positions


def count_clock_violations(result, field: ClockField) -> int:
    return sum(
        not field.transition_allowed(path[index], path[index + 1])
        for path in result.get("routed_paths", {}).values()
        for index in range(len(path) - 1)
    )


def accepted_policy_candidate(env, info, transition_index):
    """Capture the terminal state produced by a real policy action.

    Exact routing is a terminal environment reward.  Invalid or rolled-back
    actions did not produce the current layout and must not receive that reward;
    the latest accepted action did, even when it failed to beat the reset-state
    placement proxy.
    """

    if (
        bool(info.get("invalid_action", False))
        or not bool(info.get("accepted_action", False))
        or bool(info.get("rollback_action", False))
    ):
        return None
    return {
        "transition_index": int(transition_index),
        "positions": clone_positions(env.current_positions),
    }


def exact_evaluate_positions(
    context: CircuitContext,
    positions,
    fields: list[ClockField],
    args,
    edge_priorities=None,
) -> tuple[RobustClockMetrics, list[dict]]:
    records = []
    evaluations = []
    orientation = str(context.env.orientation)
    for field in fields:
        candidate = {
            "strategy": "universal-graph-ppo",
            "orientation": orientation,
            "x_spacing": "n/a",
            "y_spacing": "n/a",
            "node_positions": clone_positions(positions),
            "routing_embedding_guidance": False,
            "routing_edge_priorities": edge_priorities,
            "clock_field": field,
        }
        started = time.perf_counter()
        result = evaluate_layout_candidate_with_timeout(
            candidate,
            context.circuit,
            args.phase_count,
            args.padding,
            args.max_same_phase,
            embedding_scores=None,
            timeout_sec=args.exact_eval_timeout_sec,
        )
        clock_violations = int(count_clock_violations(result, field))
        legal = bool(is_legal_layout_result(result) and clock_violations == 0)
        cost = scalarize_layout_result(
            result,
            aspect_ratio_limit=args.aspect_ratio_limit,
            aspect_ratio_weight=args.aspect_ratio_weight,
            max_span_weight=args.max_span_weight,
            area_reference=float(context.warm_start["area"]),
            area_regression_weight=args.area_regression_weight,
        )
        evaluation = ClockEvaluation(
            legal=legal,
            cost=float(cost),
            failed_edges=len(result.get("failed_edges", [])),
            direction_violations=int(result.get("direction_violation_count", 0)),
            clock_violations=clock_violations,
            area=float(result["area"]),
            runtime_sec=float(time.perf_counter() - started),
            field_hash=field.field_hash,
        )
        evaluations.append(evaluation)
        records.append(asdict(evaluation))
    metrics = aggregate_clock_evaluations(
        evaluations,
        cvar_alpha=args.cvar_alpha,
    )
    return metrics, records


def exact_feedback_reward(metrics: RobustClockMetrics, context: CircuitContext, args) -> float:
    warm_area = max(1.0, float(context.warm_start["area"]))
    area_improvement = (warm_area - float(metrics.mean_area)) / warm_area
    return float(
        float(args.exact_legal_bonus) * (2.0 * float(metrics.success_rate) - 1.0)
        - float(args.exact_violation_penalty) * float(metrics.cvar_violations)
        + float(args.exact_area_reward_weight) * 10.0 * area_improvement
    )


def serializable_metrics(metrics: RobustClockMetrics) -> dict:
    return asdict(metrics)


def configure_online_trainable_parameters(model, freeze_offline_backbone: bool):
    frozen_modules = (
        model.node_encoder,
        model.edge_encoder,
        model.message_blocks,
        model.clock_encoder,
        model.graph_encoder,
        model.retrieval_encoder,
        model.placement_head,
        model.route_head,
        model.quality_head,
    )
    if freeze_offline_backbone:
        for module in frozen_modules:
            for parameter in module.parameters():
                parameter.requires_grad_(False)
    parameters = [parameter for parameter in model.parameters() if parameter.requires_grad]
    if not parameters:
        raise RuntimeError("online policy has no trainable parameters")
    return parameters


def completed_exact_round(records, circuit_count: int, cvar_alpha: float):
    circuit_count = int(circuit_count)
    if circuit_count <= 0 or len(records) < circuit_count:
        return None
    tail = records[-circuit_count:]
    if len({int(record["context_index"]) for record in tail}) != circuit_count:
        return None
    if len({int(record["context_visit"]) for record in tail}) != 1:
        return None
    evaluations = [
        ClockEvaluation(**field)
        for record in tail
        for field in record["fields"]
    ]
    return aggregate_clock_evaluations(evaluations, cvar_alpha=float(cvar_alpha))


def save_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as output:
        json.dump(payload, output, ensure_ascii=False, indent=2)


def save_history(path, rows):
    if not rows:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def checkpoint_payload(
    model,
    optimizer,
    args,
    episode,
    contexts,
    history,
    *,
    retrieval_memory_path=None,
    retrieval_memory=None,
):
    return {
        "schema_version": 2,
        "universal_dynamic_graph_policy": True,
        "model_state_dict": model.state_dict(),
        "optimizer_state_dict": optimizer.state_dict(),
        "episode": int(episode),
        "model": {
            "node_feature_dim": int(model.node_feature_dim),
            "edge_feature_dim": int(model.edge_feature_dim),
            "clock_feature_dim": int(model.clock_feature_dim),
            "action_feature_dim": int(model.action_feature_dim),
            "action_type_count": int(model.action_type_count),
            "retrieval_feature_dim": int(model.retrieval_feature_dim),
            "episode_feature_dim": int(model.episode_feature_dim),
            "memory_dim": int(model.memory_dim),
            "hidden_dim": int(model.hidden_dim),
            "message_passing_steps": int(model.message_passing_steps),
        },
        "config": vars(args),
        "benchmarks": [context.benchmark for context in contexts],
        "retrieval_memory": (
            os.path.abspath(retrieval_memory_path) if retrieval_memory_path else None
        ),
        "retrieval_memory_entries": (
            int(len(retrieval_memory)) if retrieval_memory is not None else 0
        ),
        "retrieval_memory_topologies": (
            int(retrieval_memory.topology_count) if retrieval_memory is not None else 0
        ),
        "robust_best_records": {
            str(index): context.robust_best_record
            for index, context in enumerate(contexts)
            if context.robust_best_record is not None
        },
        "history_tail": history[-100:],
    }


def load_torch_checkpoint(path, device):
    try:
        return torch.load(path, map_location=device, weights_only=False)
    except TypeError:
        return torch.load(path, map_location=device)


def build_policy_model(args, device):
    if not args.pretrained_policy:
        return UniversalGraphPolicy(
            hidden_dim=args.hidden_dim,
            message_passing_steps=args.message_passing_steps,
            memory_dim=args.memory_dim,
        ).to(device), None
    checkpoint_path = os.path.abspath(args.pretrained_policy)
    payload = load_torch_checkpoint(checkpoint_path, device)
    if not isinstance(payload, dict) or not payload.get("universal_dynamic_graph_policy"):
        raise ValueError("pretrained checkpoint is not a universal dynamic graph policy")
    config = dict(payload.get("model", {}))
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
    missing = [name for name in required if name not in config]
    if missing:
        raise ValueError(f"pretrained model metadata is incomplete: {missing}")
    model = UniversalGraphPolicy(
        node_feature_dim=int(config["node_feature_dim"]),
        edge_feature_dim=int(config["edge_feature_dim"]),
        clock_feature_dim=int(config["clock_feature_dim"]),
        action_feature_dim=int(config["action_feature_dim"]),
        action_type_count=int(config["action_type_count"]),
        retrieval_feature_dim=int(config["retrieval_feature_dim"]),
        episode_feature_dim=int(config["episode_feature_dim"]),
        memory_dim=int(config["memory_dim"]),
        hidden_dim=int(config["hidden_dim"]),
        message_passing_steps=int(config["message_passing_steps"]),
    ).to(device)
    model.load_state_dict(payload["model_state_dict"], strict=True)
    print(f"[Universal-GNN] loaded IFCN-pretrained policy: {checkpoint_path}")
    return model, payload


def load_retrieval_memory(args, pretrained_payload):
    if args.disable_retrieval_memory:
        return None, None
    memory_path = args.retrieval_memory
    if not memory_path and pretrained_payload:
        memory_path = pretrained_payload.get("retrieval_memory")
    if not memory_path:
        return LayoutRetrievalMemory(), None
    memory_path = os.path.abspath(memory_path)
    memory = LayoutRetrievalMemory.load(memory_path)
    print(
        f"[Universal-GNN] loaded retrieval memory: {memory_path} "
        f"entries={len(memory)} topologies={memory.topology_count}"
    )
    return memory, memory_path


def main():
    args = parse_args()
    os.makedirs(args.output_dir, exist_ok=True)
    set_global_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = resolve_device(args.device)
    benchmarks = resolve_benchmarks(args)
    print(f"[Universal-GNN] preparing {len(benchmarks)} circuits on {device}")
    contexts = [prepare_context(path, args) for path in benchmarks]

    model, pretrained_payload = build_policy_model(args, device)
    retrieval_memory, source_memory_path = load_retrieval_memory(args, pretrained_payload)
    online_memory_path = os.path.join(args.output_dir, "layout_retrieval_memory_online.json")
    freeze_offline_backbone = bool(
        args.freeze_offline_backbone and pretrained_payload is not None
    )
    trainable_parameters = configure_online_trainable_parameters(
        model,
        freeze_offline_backbone,
    )
    print(
        f"[Universal-GNN] trainable_parameters="
        f"{sum(parameter.numel() for parameter in trainable_parameters)} "
        f"freeze_offline_backbone={freeze_offline_backbone}"
    )
    optimizer = torch.optim.AdamW(
        trainable_parameters,
        lr=args.learning_rate,
        weight_decay=1e-4,
    )
    ppo_config = GraphPPOConfig(
        gamma=args.gamma,
        gae_lambda=args.gae_lambda,
        clip_eps=args.clip_eps,
        value_clip_eps=args.value_clip_eps,
        value_coef=args.value_coef,
        entropy_coef=args.entropy_coef,
        max_grad_norm=args.max_grad_norm,
        ppo_epochs=args.ppo_epochs,
        minibatch_size=args.minibatch_size,
        target_kl=args.target_kl,
    )

    pending_rollouts: list[GraphRollout] = []
    history = []
    exact_records = []
    last_update = {}
    started = time.perf_counter()
    checkpoint_path = os.path.join(args.output_dir, "universal_graph_ppo.pt")
    best_checkpoint_path = os.path.join(
        args.output_dir,
        "universal_graph_ppo_best_exact.pt",
    )
    best_exact_key = None
    best_exact_metrics = None
    context_order = rng.permutation(len(contexts)).tolist()
    context_visit_counts = [0 for _context in contexts]

    for episode in range(1, int(args.episodes) + 1):
        cycle_index = (episode - 1) % len(contexts)
        if cycle_index == 0 and episode > 1:
            context_order = rng.permutation(len(contexts)).tolist()
        context_index = int(context_order[cycle_index])
        context = contexts[context_index]
        context_visit_counts[context_index] += 1
        context_visit = int(context_visit_counts[context_index])
        env = context.env
        elite_start = (
            context.robust_best_positions is not None
            and float(args.elite_start_probability) > 0.0
            and float(rng.random()) < float(args.elite_start_probability)
        )
        if elite_start:
            env.reset(start_positions=context.robust_best_positions)
        else:
            env.reset()
        field = sample_field(env.current_positions, env.orientation, args, rng)
        if bool(args.clock_aligned_start):
            aligned_positions = clock_aligned_start_positions(env, field, args)
            env.reset(start_positions=aligned_positions)
            field = sample_clock_field(
                field_bounds_for_positions(aligned_positions, args),
                field.spec,
            )
        frozen_memory = retrieve_policy_memory(
            retrieval_memory,
            env,
            field,
            top_k=args.retrieval_top_k,
            exclude_exact_topology=not bool(args.exact_topology_memory),
        )
        initial_memory_state = model.initial_memory_state(device)
        memory_state = initial_memory_state
        episode_features = build_episode_feedback_features(env)
        rollout = GraphRollout(
            circuit_id=os.path.basename(context.benchmark),
            clock_field_hash=field.field_hash,
            initial_memory_state=initial_memory_state.detach().cpu(),
        )
        terminal_policy_candidate = None
        episode_reward = 0.0

        for step_index in range(int(args.steps_per_episode)):
            policy_input = build_graph_policy_input(
                env,
                field,
                retrieval_features=frozen_memory.features,
                episode_features=episode_features,
                memory_state=memory_state,
                retrieval_node_hints=frozen_memory.node_hints,
            )
            action, log_prob, value, next_memory = graph_memory_policy_step(
                model,
                policy_input,
                device,
            )
            _flat_obs, reward, done, info = env.step(action)
            memory_state = next_memory
            episode_features = build_episode_feedback_features(
                env,
                info,
                reward=reward,
                step_index=step_index + 1,
                max_steps=args.steps_per_episode,
            )
            terminated = bool(info.get("terminated", False))
            rollout.transitions.append(
                GraphTransition(
                    policy_input=policy_input,
                    action=action,
                    old_log_prob=log_prob,
                    reward=float(reward),
                    value=float(value),
                    terminated=terminated,
                    truncated=bool(done and not terminated),
                )
            )
            produced_candidate = accepted_policy_candidate(
                env,
                info,
                len(rollout.transitions) - 1,
            )
            if produced_candidate is not None:
                terminal_policy_candidate = produced_candidate
            episode_reward += float(reward)
            if done:
                break

        if rollout.transitions and rollout.transitions[-1].truncated:
            next_input = build_graph_policy_input(
                env,
                field,
                retrieval_features=frozen_memory.features,
                episode_features=episode_features,
                memory_state=memory_state,
                retrieval_node_hints=frozen_memory.node_hints,
            )
            rollout.bootstrap_value = bootstrap_graph_value(model, next_input, device)

        robust_metrics = None
        exact_reward = 0.0
        if (
            int(args.exact_feedback_interval) > 0
            and context_visit % int(args.exact_feedback_interval) == 0
        ):
            exact_positions = (
                terminal_policy_candidate["positions"]
                if terminal_policy_candidate is not None
                else clone_positions(env.best_positions)
            )
            exact_transition_index = (
                int(terminal_policy_candidate["transition_index"])
                if terminal_policy_candidate is not None
                else None
            )
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
            exact_fields = [field]
            for _ in range(max(0, int(args.exact_field_samples) - 1)):
                exact_fields.append(
                    resample_compatible_field(field, exact_positions, args, rng)
                )
            robust_metrics, field_records = exact_evaluate_positions(
                context,
                exact_positions,
                exact_fields,
                args,
                edge_priorities=route_edge_priorities,
            )
            exact_reward = exact_feedback_reward(robust_metrics, context, args)
            applied = rollout.add_exact_feedback(exact_transition_index, exact_reward)
            if applied:
                episode_reward += exact_reward
            exact_record = {
                "episode": int(episode),
                "context_index": int(context_index),
                "context_visit": int(context_visit),
                "benchmark": context.benchmark,
                "reward": float(exact_reward),
                "reward_applied": bool(applied),
                "metrics": serializable_metrics(robust_metrics),
                "fields": field_records,
                "positions": {
                    str(node_id): [int(coord[0]), int(coord[1])]
                    for node_id, coord in exact_positions.items()
                },
            }
            exact_records.append(exact_record)
            robust_key = robust_metrics.selection_key() + (float(robust_metrics.mean_area),)
            if context.robust_best_key is None or robust_key < context.robust_best_key:
                context.robust_best_key = robust_key
                context.robust_best_positions = clone_positions(exact_positions)
                context.robust_best_record = exact_record
            if (
                retrieval_memory is not None
                and float(robust_metrics.success_rate)
                >= float(args.online_memory_min_success_rate)
            ):
                remember_exact_layout(
                    retrieval_memory,
                    env,
                    field,
                    exact_positions,
                    {
                        "legal": float(robust_metrics.success_rate >= 1.0),
                        "success_rate": float(robust_metrics.success_rate),
                        "area": float(robust_metrics.mean_area),
                        "failed_edges": float(robust_metrics.mean_violations),
                        "clock_violations": float(robust_metrics.cvar_violations),
                        "runtime_sec": float(robust_metrics.mean_runtime_sec),
                    },
                    metadata={
                        "source": "online_exact",
                        "benchmark": context.benchmark,
                        "episode": int(episode),
                        "field_hash": field.field_hash,
                    },
                )
            exact_round_metrics = completed_exact_round(
                exact_records,
                len(contexts),
                args.cvar_alpha,
            )
            if exact_round_metrics is not None:
                exact_round_key = exact_round_metrics.selection_key(
                    minimum_success_rate=1.0
                ) + (
                    float(exact_round_metrics.mean_area),
                    float(exact_round_metrics.mean_runtime_sec),
                )
                if best_exact_key is None or exact_round_key < best_exact_key:
                    best_exact_key = exact_round_key
                    best_exact_metrics = serializable_metrics(exact_round_metrics)
                    payload = checkpoint_payload(
                        model,
                        optimizer,
                        args,
                        episode,
                        contexts,
                        history,
                        retrieval_memory_path=(
                            online_memory_path
                            if retrieval_memory is not None
                            else source_memory_path
                        ),
                        retrieval_memory=retrieval_memory,
                    )
                    payload["best_exact_round_metrics"] = best_exact_metrics
                    payload["best_exact_round_key"] = list(best_exact_key)
                    torch.save(payload, best_checkpoint_path)
                    if retrieval_memory is not None:
                        retrieval_memory.save(online_memory_path)
                    print(
                        "[Universal-GNN] saved best exact checkpoint: "
                        f"episode={episode} "
                        f"success={exact_round_metrics.success_rate:.3f} "
                        f"violations={exact_round_metrics.mean_violations:.3f}"
                    )

        pending_rollouts.append(rollout)
        if (
            len(pending_rollouts) >= int(args.episodes_per_update)
            or episode == int(args.episodes)
        ):
            last_update = graph_ppo_update(
                model,
                optimizer,
                pending_rollouts,
                ppo_config,
                device,
            )
            pending_rollouts.clear()

        history.append(
            {
                "episode": int(episode),
                "context_index": int(context_index),
                "context_visit": int(context_visit),
                "benchmark": os.path.basename(context.benchmark),
                "steps": len(rollout.transitions),
                "episode_reward": float(episode_reward),
                "proxy_best_area": float(env.best_result["area"]),
                "clock_field_hash": field.field_hash,
                "memory_entries": len(frozen_memory.entry_ids),
                "memory_exact_entries": int(frozen_memory.exact_count),
                "memory_best_similarity": float(frozen_memory.best_similarity),
                "exact_reward": float(exact_reward),
                "exact_success_rate": (
                    float(robust_metrics.success_rate) if robust_metrics is not None else ""
                ),
                "exact_cvar_violations": (
                    float(robust_metrics.cvar_violations) if robust_metrics is not None else ""
                ),
                "update_steps": int(last_update.get("update_steps", 0)),
                "approx_kl": float(last_update.get("approx_kl_mean", 0.0)),
                "entropy": float(last_update.get("entropy_mean", 0.0)),
            }
        )

        if episode % max(1, int(args.log_interval)) == 0 or episode == 1:
            print(
                "[Universal-GNN] "
                f"episode={episode}/{args.episodes} "
                f"circuit={os.path.basename(context.benchmark)} "
                f"reward={episode_reward:.3f} "
                f"proxy_area={env.best_result['area']:.1f} "
                f"exact_success={history[-1]['exact_success_rate']} "
                f"kl={float(last_update.get('approx_kl_mean', 0.0)):.5f}"
            )

        if (
            episode % max(1, int(args.checkpoint_interval)) == 0
            or episode == int(args.episodes)
        ):
            torch.save(
                checkpoint_payload(
                    model,
                    optimizer,
                    args,
                    episode,
                    contexts,
                    history,
                    retrieval_memory_path=(
                        online_memory_path if retrieval_memory is not None else source_memory_path
                    ),
                    retrieval_memory=retrieval_memory,
                ),
                checkpoint_path,
            )
            if retrieval_memory is not None:
                retrieval_memory.save(online_memory_path)
            save_history(os.path.join(args.output_dir, "universal_graph_training.csv"), history)
            save_json(
                os.path.join(args.output_dir, "universal_graph_exact_records.json"),
                exact_records,
            )

    summary = {
        "episodes": int(args.episodes),
        "circuit_count": len(contexts),
        "elapsed_sec": float(time.perf_counter() - started),
        "device": str(device),
        "exact_evaluation_count": len(exact_records),
        "robust_best_count": sum(context.robust_best_record is not None for context in contexts),
        "last_update": last_update,
        "checkpoint": checkpoint_path,
        "best_exact_checkpoint": (
            best_checkpoint_path if best_exact_metrics is not None else None
        ),
        "best_exact_metrics": best_exact_metrics,
        "pretrained_policy": args.pretrained_policy,
        "retrieval_memory": (
            online_memory_path if retrieval_memory is not None else source_memory_path
        ),
        "retrieval_memory_entries": (
            int(len(retrieval_memory)) if retrieval_memory is not None else 0
        ),
    }
    save_json(os.path.join(args.output_dir, "universal_graph_summary.json"), summary)
    save_json(os.path.join(args.output_dir, "universal_graph_config.json"), vars(args))
    print(f"[Universal-GNN] completed: {json.dumps(summary, ensure_ascii=False)}")


if __name__ == "__main__":
    main()
