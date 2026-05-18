import argparse
import ast
import csv
import json
import os
import platform
import re
import signal
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass

import numpy as np
import torch
import torch.nn as nn
from torch.distributions import Categorical

from utils import add_project_root

add_project_root()

MPLCONFIG_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/.matplotlib",
    )
)
os.environ.setdefault("MPLCONFIGDIR", MPLCONFIG_DIR)

from lib import iFCN_Lab
from src.circuit_parse import CircuitParser
from src.gcn_model_less_node import normal_generate, visualize_layered_graph_sorted
from src.toolkit import generate_gate_level_mapping_file
from test_randomPhase import (
    DEFAULT_LOCAL_BEAM_WIDTH,
    DEFAULT_LOCAL_BRANCH_WIDTH,
    DEFAULT_LOCAL_LOOKAHEAD_DEPTH,
    LEFT_RIGHT,
    TOP_DOWN,
    LayoutOnlyResult,
    build_desired_secondary_map,
    build_embedding_score_map,
    build_gcn_guided_node_positions,
    build_layout_candidates,
    build_layer_dict,
    build_node_positions_with_fixed_spacing,
    compute_io_edge_penalty,
    create_board_with_positions,
    evaluate_layout_candidate,
    get_gcn_cache_path,
    get_layout_memory_path,
    infer_layer_secondary_spacing,
    load_layout_memory_candidate,
    load_layout_memory_candidates,
    load_or_generate_gcn_layout,
    normalize_layers,
    save_layout_memory,
    select_best_layout,
    set_global_seed,
    solve_ordered_targets_with_gaps,
    summarize_node_position_spacing,
)


RL_OUTPUT_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/rl_layout",
    )
)
EXACT_EVAL_WORKER = os.path.join(os.path.dirname(__file__), "evaluate_layout_candidate_worker.py")
DEFAULT_HIDDEN_DIM = 256
DEFAULT_EPISODES = 160
DEFAULT_STEPS_PER_EPISODE = 10
DEFAULT_PPO_EPOCHS = 6
DEFAULT_MINIBATCH_SIZE = 64
DEFAULT_LEARNING_RATE = 3e-4
DEFAULT_GAMMA = 0.99
DEFAULT_GAE_LAMBDA = 0.95
DEFAULT_CLIP_EPS = 0.2
DEFAULT_ENTROPY_COEF = 0.01
DEFAULT_VALUE_COEF = 0.5
DEFAULT_MAX_GRAD_NORM = 1.0
INVALID_LAYOUT_DIM = 1024
INVALID_LAYOUT_AREA = float(INVALID_LAYOUT_DIM * INVALID_LAYOUT_DIM)
INVALID_LAYOUT_PENALTY = 10_000.0
DEFAULT_ASPECT_RATIO_LIMIT = 4.0
DEFAULT_ASPECT_RATIO_WEIGHT = 300.0
DEFAULT_MAX_SPAN_WEIGHT = 8.0
DEFAULT_AREA_REGRESSION_WEIGHT = 250.0
DEFAULT_EXPERIENCE_PRIOR_SCALE = 0.65
DEFAULT_AREA_REWARD_WEIGHT = 3.0


def default_benchmark_path():
    return ""


def parse_args():
    parser = argparse.ArgumentParser(
        description="Train a PPO agent that compacts a routed layout using GPU policy updates.",
    )
    parser.add_argument(
        "--benchmark",
        default=default_benchmark_path(),
        help="Path to the input Verilog benchmark.",
    )
    parser.add_argument(
        "--output-dir",
        default=RL_OUTPUT_DIR,
        help="Directory for RL training outputs.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=7,
        help="Random seed for both GCN warm start and PPO training.",
    )
    parser.add_argument(
        "--device",
        default="auto",
        choices=("auto", "cpu", "cuda"),
        help="Policy/value network device. Environment evaluation remains CPU-side.",
    )
    parser.add_argument(
        "--start-layout-strategy",
        default="auto",
        choices=("auto", "fixed", "shifted", "adaptive", "gcn"),
        help="Initial layout family used before RL local search begins.",
    )
    parser.add_argument(
        "--start-layout-orientation",
        default="auto",
        choices=("auto", LEFT_RIGHT, TOP_DOWN),
        help="Initial layout orientation used before RL local search begins.",
    )
    parser.add_argument(
        "--parse-mode",
        default="auto",
        choices=("auto", "compact", "layered"),
        help=(
            "Circuit parsing strategy. compact minimizes nodes; layered preserves layer "
            "redundancy to remove cross-layer routes; auto chooses layered for larger circuits."
        ),
    )
    parser.add_argument("--x-spacing", type=int, default=2)
    parser.add_argument("--y-spacing", type=int, default=2)
    parser.add_argument("--phase-cycle", type=int, default=4)
    parser.add_argument("--padding", type=int, default=1)
    parser.add_argument("--max-same-phase", type=int, default=4)
    parser.add_argument(
        "--clock-domain-randomization",
        action="store_true",
        help="Randomize clock/phase routing constraints per PPO episode.",
    )
    parser.add_argument(
        "--phase-cycle-min",
        type=int,
        default=None,
        help="Minimum phase cycle for clock-domain randomization. Defaults to --phase-cycle.",
    )
    parser.add_argument(
        "--phase-cycle-max",
        type=int,
        default=None,
        help="Maximum phase cycle for clock-domain randomization. Defaults to --phase-cycle.",
    )
    parser.add_argument(
        "--padding-min",
        type=int,
        default=None,
        help="Minimum routing padding for clock-domain randomization. Defaults to --padding.",
    )
    parser.add_argument(
        "--padding-max",
        type=int,
        default=None,
        help="Maximum routing padding for clock-domain randomization. Defaults to --padding.",
    )
    parser.add_argument(
        "--max-same-phase-min",
        type=int,
        default=None,
        help="Minimum max-same-phase for clock-domain randomization. Defaults to --max-same-phase.",
    )
    parser.add_argument(
        "--max-same-phase-max",
        type=int,
        default=None,
        help="Maximum max-same-phase for clock-domain randomization. Defaults to --max-same-phase.",
    )
    parser.add_argument(
        "--clock-random-seed",
        type=int,
        default=None,
        help="RNG seed for per-episode clock-domain randomization. Defaults to --seed.",
    )
    parser.add_argument(
        "--board-margin",
        type=int,
        default=None,
        help="Blank board margin before the first placed node. Defaults to padding + 1.",
    )
    parser.add_argument(
        "--single-start-candidate",
        action="store_true",
        help="Evaluate only the requested start strategy/orientation/spacing candidate.",
    )
    parser.add_argument(
        "--local-refine-rounds",
        type=int,
        default=8,
        help="Greedy local compaction steps applied to the warm-start layout before PPO begins. Set to 0 to disable.",
    )
    parser.add_argument(
        "--local-lookahead-depth",
        type=int,
        default=DEFAULT_LOCAL_LOOKAHEAD_DEPTH,
        help="Local compaction lookahead depth used before PPO starts.",
    )
    parser.add_argument(
        "--local-beam-width",
        type=int,
        default=DEFAULT_LOCAL_BEAM_WIDTH,
        help="Beam width used by the pre-PPO local compactor.",
    )
    parser.add_argument(
        "--local-branch-width",
        type=int,
        default=DEFAULT_LOCAL_BRANCH_WIDTH,
        help="Branch width used by the pre-PPO local compactor.",
    )
    parser.add_argument(
        "--local-max-evaluations",
        type=int,
        default=0,
        help="Maximum pre-PPO local-compaction candidate evaluations. Use 0 to disable the budget.",
    )
    parser.add_argument(
        "--post-primary-pack-rounds",
        type=int,
        default=6,
        help="Greedy post-RL primary-axis layer packing rounds. Use 0 to disable.",
    )
    parser.add_argument(
        "--post-area-pack-rounds",
        type=int,
        default=10,
        help="Greedy post-RL area-first packing rounds across both axes. Use 0 to disable.",
    )
    parser.add_argument(
        "--post-pack-max-evaluations",
        type=int,
        default=320,
        help="Maximum post-RL packing candidate evaluations. Use 0 to disable the budget.",
    )
    parser.add_argument(
        "--post-phase-strip-pack-rounds",
        type=int,
        default=3,
        help="Greedy post-route strip compaction rounds driven by routed occupancy and phase signatures. Use 0 to disable.",
    )
    parser.add_argument(
        "--post-phase-strip-pack-max-evaluations",
        type=int,
        default=160,
        help="Maximum exact-evaluation candidates explored by the post-route strip compactor.",
    )
    parser.add_argument(
        "--disable-gcn-cache",
        action="store_true",
        help="Disable on-disk GCN embedding/order cache and retrain every run.",
    )
    parser.add_argument(
        "--disable-layout-memory",
        action="store_true",
        help="Disable reading/writing the layout memory candidate for this benchmark.",
    )
    parser.add_argument(
        "--memory-only-inference",
        action="store_true",
        help="Load the best compatible layout memory and export it without PPO training.",
    )
    parser.add_argument("--episodes", type=int, default=DEFAULT_EPISODES)
    parser.add_argument("--steps-per-episode", type=int, default=DEFAULT_STEPS_PER_EPISODE)
    parser.add_argument("--ppo-epochs", type=int, default=DEFAULT_PPO_EPOCHS)
    parser.add_argument("--minibatch-size", type=int, default=DEFAULT_MINIBATCH_SIZE)
    parser.add_argument("--hidden-dim", type=int, default=DEFAULT_HIDDEN_DIM)
    parser.add_argument("--learning-rate", type=float, default=DEFAULT_LEARNING_RATE)
    parser.add_argument("--gamma", type=float, default=DEFAULT_GAMMA)
    parser.add_argument("--gae-lambda", type=float, default=DEFAULT_GAE_LAMBDA)
    parser.add_argument("--clip-eps", type=float, default=DEFAULT_CLIP_EPS)
    parser.add_argument("--entropy-coef", type=float, default=DEFAULT_ENTROPY_COEF)
    parser.add_argument("--value-coef", type=float, default=DEFAULT_VALUE_COEF)
    parser.add_argument("--max-grad-norm", type=float, default=DEFAULT_MAX_GRAD_NORM)
    parser.add_argument(
        "--disable-step-log",
        action="store_true",
        help="Skip writing per-step training trajectories.",
    )
    parser.add_argument(
        "--disable-training-plots",
        action="store_true",
        help="Skip writing reward/cost/area training plots.",
    )
    parser.add_argument(
        "--log-interval",
        type=int,
        default=10,
        help="Print PPO progress every N episodes.",
    )
    parser.add_argument(
        "--disable-secondary-squeeze",
        action="store_true",
        help="Disable the post-action secondary-axis squeeze that removes redundant slack.",
    )
    parser.add_argument(
        "--rollback-worse-actions",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Keep the current state when an action worsens the selected layout key.",
    )
    parser.add_argument(
        "--elite-start-probability",
        type=float,
        default=0.6,
        help="Probability after episode 1 of starting an episode from the current global best layout.",
    )
    parser.add_argument(
        "--early-stop-patience",
        type=int,
        default=0,
        help="Stop after this many episodes without a new global best. Use 0 to disable.",
    )
    parser.add_argument(
        "--early-stop-min-episodes",
        type=int,
        default=20,
        help="Minimum number of episodes before early stopping becomes active.",
    )
    parser.add_argument(
        "--aspect-ratio-limit",
        type=float,
        default=DEFAULT_ASPECT_RATIO_LIMIT,
        help="Soft maximum width/height or height/width ratio before aspect penalty starts.",
    )
    parser.add_argument(
        "--aspect-ratio-weight",
        type=float,
        default=DEFAULT_ASPECT_RATIO_WEIGHT,
        help="Quadratic penalty weight for excessively flat/tall layouts.",
    )
    parser.add_argument(
        "--max-span-weight",
        type=float,
        default=DEFAULT_MAX_SPAN_WEIGHT,
        help="Additional penalty weight on max(width, height) to discourage long skinny layouts.",
    )
    parser.add_argument(
        "--area-regression-weight",
        type=float,
        default=DEFAULT_AREA_REGRESSION_WEIGHT,
        help="Extra penalty per area cell when a candidate is larger than the warm-start layout.",
    )
    parser.add_argument(
        "--best-selection-mode",
        default="legal-area",
        choices=("cost", "legal-area", "legal-span-area", "legal-height-area"),
        help=(
            "Global/episode best criterion. legal-area keeps legality first, then minimizes area; "
            "legal-span-area minimizes the longest dimension before area; legal-height-area "
            "minimizes height before area."
        ),
    )
    parser.add_argument(
        "--area-reward-weight",
        type=float,
        default=DEFAULT_AREA_REWARD_WEIGHT,
        help="Additional reward scale for per-step area reduction.",
    )
    parser.add_argument(
        "--train-eval-mode",
        default="auto",
        choices=("auto", "exact", "placement"),
        help="Use exact routing during training, placement-only fast evaluation, or auto by circuit size.",
    )
    parser.add_argument(
        "--fast-eval-node-threshold",
        type=int,
        default=100,
        help="Auto mode uses placement-only training evaluation at or above this effective-node count.",
    )
    parser.add_argument(
        "--final-exact-validation",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Run one exact routed validation for the final best when training used placement evaluation.",
    )
    parser.add_argument(
        "--exact-validation-interval",
        type=int,
        default=0,
        help="In placement mode, exact-route validate the episode best every N episodes. Use 0 for final-only.",
    )
    parser.add_argument(
        "--placement-candidate-pool-size",
        type=int,
        default=64,
        help="Number of placement-mode candidates retained for final exact validation.",
    )
    parser.add_argument(
        "--final-exact-validation-candidates",
        type=int,
        default=24,
        help="Maximum placement-mode candidate count to exact-route at final validation.",
    )
    parser.add_argument(
        "--exact-eval-timeout-sec",
        type=int,
        default=60,
        help="Timeout for one exact routing evaluation. Timed-out candidates are treated as failed.",
    )
    parser.add_argument(
        "--require-legal-final",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Require failed_edges=0 before exporting layout artifacts.",
    )
    parser.add_argument(
        "--legal-repair-candidates",
        type=int,
        default=24,
        help="Extra exact-routing repair candidates to try when placement-trained layouts are not legal.",
    )
    parser.add_argument(
        "--legal-repair-timeout-multiplier",
        type=float,
        default=4.0,
        help="Multiplier applied to exact timeout during final legality repair search.",
    )
    parser.add_argument(
        "--legal-repair-max-padding",
        type=int,
        default=8,
        help="Maximum routing padding used by final legality repair search.",
    )
    parser.add_argument(
        "--auto-layered-node-threshold",
        type=int,
        default=80,
        help="In --parse-mode auto, node count at or above this value raises pressure to retry layered parsing.",
    )
    parser.add_argument(
        "--auto-layered-edge-threshold",
        type=int,
        default=120,
        help="In --parse-mode auto, edge count at or above this value raises pressure to retry layered parsing.",
    )
    parser.add_argument(
        "--auto-layered-min-success-rate",
        type=float,
        default=0.25,
        help="Retry layered parsing when compact exact-routing probe success rate falls below this ratio.",
    )
    parser.add_argument(
        "--auto-layered-max-probe-sec",
        type=float,
        default=0.0,
        help="Retry layered parsing when compact probing exceeds this many seconds. 0 derives it from exact timeout.",
    )
    parser.add_argument(
        "--strict-memory-updates",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Only write shared RL experience when final best is routed successfully and improves area.",
    )
    parser.add_argument(
        "--rl-experience-path",
        default=os.path.join(
            os.path.dirname(__file__),
            "../../../results/layout_memory/rl_action_experience.json",
        ),
        help="Shared cross-circuit action experience memory used as a policy prior.",
    )
    parser.add_argument(
        "--disable-rl-experience",
        action="store_true",
        help="Disable reading/writing shared action experience memory.",
    )
    parser.add_argument(
        "--experience-prior-scale",
        type=float,
        default=DEFAULT_EXPERIENCE_PRIOR_SCALE,
        help="Scale of action-prior logits loaded from cross-circuit experience memory.",
    )
    return parser.parse_args()


def resolve_device(device_arg):
    if device_arg == "auto":
        return torch.device("cuda" if torch.cuda.is_available() else "cpu")
    return torch.device(device_arg)


def resolve_int_range(min_value, max_value, default_value, lower_bound=0):
    range_min = default_value if min_value is None else min_value
    range_max = default_value if max_value is None else max_value
    range_min = max(int(lower_bound), int(range_min))
    range_max = max(int(lower_bound), int(range_max))
    if range_min > range_max:
        range_min, range_max = range_max, range_min
    return range_min, range_max


def clone_positions(node_positions):
    return {int(node_id): (int(coord[0]), int(coord[1])) for node_id, coord in node_positions.items()}


def scalarize_layout_result(
    result,
    aspect_ratio_limit=DEFAULT_ASPECT_RATIO_LIMIT,
    aspect_ratio_weight=DEFAULT_ASPECT_RATIO_WEIGHT,
    max_span_weight=DEFAULT_MAX_SPAN_WEIGHT,
    area_reference=None,
    area_regression_weight=DEFAULT_AREA_REGRESSION_WEIGHT,
):
    width = int(result["width"]) if np.isfinite(result["width"]) and int(result["width"]) > 0 else INVALID_LAYOUT_DIM
    height = int(result["height"]) if np.isfinite(result["height"]) and int(result["height"]) > 0 else INVALID_LAYOUT_DIM
    area_value = float(result["area"])
    if not np.isfinite(area_value) or area_value <= 0:
        area_value = max(INVALID_LAYOUT_AREA, float(width * height))
    direction_violation_count = int(result["direction_violation_count"]) if np.isfinite(result["direction_violation_count"]) and int(result["direction_violation_count"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
    route_overhang_penalty = int(result["route_overhang_penalty"]) if np.isfinite(result["route_overhang_penalty"]) and int(result["route_overhang_penalty"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
    io_exposure_penalty = int(result["io_exposure_penalty"]) if np.isfinite(result["io_exposure_penalty"]) and int(result["io_exposure_penalty"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
    aspect_ratio = max(
        float(width) / max(1.0, float(height)),
        float(height) / max(1.0, float(width)),
    )
    aspect_excess = max(0.0, aspect_ratio - max(1.0, float(aspect_ratio_limit)))
    aspect_penalty = float(aspect_ratio_weight) * aspect_excess * aspect_excess
    max_span_penalty = float(max_span_weight) * float(max(width, height))
    area_regression_penalty = 0.0
    if area_reference is not None:
        area_regression_penalty = (
            max(0.0, area_value - float(area_reference)) *
            max(0.0, float(area_regression_weight))
        )
    return (
        len(result["failed_edges"]) * 50_000.0 +
        direction_violation_count * 5_000.0 +
        route_overhang_penalty * 120.0 +
        io_exposure_penalty * 60.0 +
        area_value * 5.0 +
        (width + height) * 2.0 +
        max_span_penalty +
        aspect_penalty +
        area_regression_penalty
    )


def layout_selection_key(compact, cost, mode="legal-area"):
    if mode == "cost":
        return (float(cost),)
    width = int(compact["width"])
    height = int(compact["height"])
    legality_prefix = (
        int(len(compact["failed_edges"])),
        int(compact["direction_violation_count"]),
    )
    route_suffix = (
        int(compact["route_overhang_penalty"]),
        int(compact["io_exposure_penalty"]),
        float(cost),
    )
    if mode == "legal-span-area":
        return (
            *legality_prefix,
            int(max(width, height)),
            float(compact["area"]),
            int(width + height),
            *route_suffix,
        )
    if mode == "legal-height-area":
        return (
            *legality_prefix,
            int(height),
            float(compact["area"]),
            int(width),
            int(max(width, height)),
            *route_suffix,
        )
    return (
        *legality_prefix,
        float(compact["area"]),
        int(max(width, height)),
        int(width + height),
        *route_suffix,
    )


def node_position_area(node_positions):
    if not node_positions:
        return INVALID_LAYOUT_AREA
    xs = [int(coord[0]) for coord in node_positions.values()]
    ys = [int(coord[1]) for coord in node_positions.values()]
    width = max(xs) - min(xs) + 1
    height = max(ys) - min(ys) + 1
    return max(1, width) * max(1, height)


def expand_node_positions(node_positions, orientation, primary_scale=1, secondary_scale=1):
    if not node_positions:
        return {}
    primary_scale = max(1, int(primary_scale))
    secondary_scale = max(1, int(secondary_scale))

    def axis_primary(coord):
        return int(coord[0]) if orientation == LEFT_RIGHT else int(coord[1])

    def axis_secondary(coord):
        return int(coord[1]) if orientation == LEFT_RIGHT else int(coord[0])

    min_primary = min(axis_primary(coord) for coord in node_positions.values())
    min_secondary = min(axis_secondary(coord) for coord in node_positions.values())
    expanded = {}
    for node_id, coord in node_positions.items():
        primary = min_primary + (axis_primary(coord) - min_primary) * primary_scale
        secondary = min_secondary + (axis_secondary(coord) - min_secondary) * secondary_scale
        if orientation == LEFT_RIGHT:
            expanded[int(node_id)] = (int(primary), int(secondary))
        else:
            expanded[int(node_id)] = (int(secondary), int(primary))
    return expanded


def evaluate_placement_only_candidate(candidate, circuit):
    board = create_board_with_positions(circuit, candidate["node_positions"])
    width, height = board.computeLayoutArea()
    area = width * height if width > 0 and height > 0 else float("inf")
    io_exposure_penalty = compute_io_edge_penalty(
        circuit,
        candidate["node_positions"],
        board,
        candidate["orientation"],
    )
    return {
        "board": board,
        "node_positions": clone_positions(candidate["node_positions"]),
        "routed_paths": {},
        "failed_edges": [],
        "x_spacing": candidate["x_spacing"],
        "y_spacing": candidate["y_spacing"],
        "layout_strategy": candidate["strategy"],
        "layout_orientation": candidate["orientation"],
        "routing_embedding_guidance": bool(candidate.get("routing_embedding_guidance", False)),
        "width": width,
        "height": height,
        "area": area,
        "io_exposure_penalty": io_exposure_penalty,
        "route_overhang_penalty": 0,
        "direction_violation_count": 0,
        "placement_only_evaluation": True,
    }


class ExactEvaluationTimeout(RuntimeError):
    pass


def make_exact_failure_result(candidate, circuit, reason):
    board = create_board_with_positions(circuit, candidate["node_positions"])
    width, height = board.computeLayoutArea()
    result = {
        "board": board,
        "node_positions": clone_positions(candidate["node_positions"]),
        "routed_paths": {},
        "failed_edges": list(circuit.effective_edges),
        "x_spacing": candidate["x_spacing"],
        "y_spacing": candidate["y_spacing"],
        "layout_strategy": candidate["strategy"],
        "layout_orientation": candidate["orientation"],
        "routing_embedding_guidance": bool(candidate.get("routing_embedding_guidance", False)),
        "width": width if width > 0 else INVALID_LAYOUT_DIM,
        "height": height if height > 0 else INVALID_LAYOUT_DIM,
        "area": float(width * height) if width > 0 and height > 0 else INVALID_LAYOUT_AREA,
        "io_exposure_penalty": INVALID_LAYOUT_PENALTY,
        "route_overhang_penalty": INVALID_LAYOUT_PENALTY,
        "direction_violation_count": INVALID_LAYOUT_PENALTY,
        "exact_evaluation_error": str(reason),
    }
    return result


def materialize_result_board(circuit, result):
    if result.get("board") is not None:
        return result

    board = create_board_with_positions(circuit, result["node_positions"])
    routed_path_phases = result.get("routed_path_phases", {})
    placed_wire_coords = set()
    for edge, path in result.get("routed_paths", {}).items():
        normalized_edge = (int(edge[0]), int(edge[1]))
        normalized_path = [(int(coord[0]), int(coord[1])) for coord in path]
        phase_entries = routed_path_phases.get(normalized_edge, [])
        if not phase_entries:
            phase_entries = routed_path_phases.get(f"{normalized_edge[0]},{normalized_edge[1]}", [])
        for entry in phase_entries:
            coord = (int(entry[0]), int(entry[1]))
            phase = int(entry[2])
            if phase >= 0 and board.getPhase(coord) < 0:
                board.setPhase(coord, phase)
        for coord in normalized_path[1:-1]:
            if coord not in placed_wire_coords:
                board.placeWire(coord)
                placed_wire_coords.add(coord)
        board.savePath(normalized_edge, normalized_path)

    materialized = dict(result)
    materialized["board"] = board
    return materialized


def preserve_unassigned_export_phases(board, phase_cycle):
    """Keep unused cells at phase -1 in exported layouts."""
    if board is None:
        return


def deserialize_worker_result(payload, circuit):
    routed_paths = {}
    for key, path in payload.get("routed_paths", {}).items():
        src, dst = key.split(",", 1)
        routed_paths[(int(src), int(dst))] = [
            (int(coord[0]), int(coord[1])) for coord in path
        ]

    routed_path_phases = {}
    for key, path in payload.get("routed_path_phases", {}).items():
        src, dst = key.split(",", 1)
        routed_path_phases[(int(src), int(dst))] = [
            (int(entry[0]), int(entry[1]), int(entry[2])) for entry in path
        ]

    result = {
        "board": None,
        "node_positions": {
            int(node_id): (int(coord[0]), int(coord[1]))
            for node_id, coord in payload["node_positions"].items()
        },
        "routed_paths": routed_paths,
        "routed_path_phases": routed_path_phases,
        "failed_edges": [
            (int(edge[0]), int(edge[1])) for edge in payload.get("failed_edges", [])
        ],
        "x_spacing": payload["x_spacing"],
        "y_spacing": payload["y_spacing"],
        "layout_strategy": payload["layout_strategy"],
        "layout_orientation": payload["layout_orientation"],
        "routing_embedding_guidance": bool(payload.get("routing_embedding_guidance", False)),
        "width": int(payload["width"]),
        "height": int(payload["height"]),
        "area": float(payload["area"]),
        "io_exposure_penalty": int(payload["io_exposure_penalty"]),
        "route_overhang_penalty": int(payload["route_overhang_penalty"]),
        "direction_violation_count": int(payload["direction_violation_count"]),
        "exact_worker_runtime_sec": float(payload.get("worker_runtime_sec", 0.0)),
    }
    return materialize_result_board(circuit, result)


def evaluate_layout_candidate_in_subprocess(
    candidate,
    circuit,
    phase_cycle,
    padding,
    max_same_phase,
    embedding_scores=None,
    timeout_sec=0,
):
    payload = {
        "benchmark": os.path.abspath(circuit.filePath),
        "parse_mode": getattr(circuit, "parse_mode_resolved", "auto"),
        "candidate": {
            "strategy": candidate["strategy"],
            "orientation": candidate["orientation"],
            "x_spacing": candidate["x_spacing"],
            "y_spacing": candidate["y_spacing"],
            "node_positions": {
                str(node_id): [int(coord[0]), int(coord[1])]
                for node_id, coord in candidate["node_positions"].items()
            },
            "routing_embedding_guidance": bool(candidate.get("routing_embedding_guidance", False)),
        },
        "phase_cycle": int(phase_cycle),
        "padding": int(padding),
        "max_same_phase": int(max_same_phase),
        "embedding_scores": (
            {str(node_id): float(score) for node_id, score in embedding_scores.items()}
            if embedding_scores else
            None
        ),
    }
    try:
        completed = subprocess.run(
            [sys.executable, EXACT_EVAL_WORKER],
            input=json.dumps(payload),
            cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), "../../..")),
            capture_output=True,
            text=True,
            timeout=int(timeout_sec) if int(timeout_sec) > 0 else None,
            check=False,
        )
    except subprocess.TimeoutExpired:
        result = make_exact_failure_result(candidate, circuit, f"subprocess_timeout_{timeout_sec}s")
        result["exact_evaluation_timeout"] = True
        return result

    if completed.returncode != 0:
        error_excerpt = (completed.stderr or completed.stdout or "").strip()[-1000:]
        return make_exact_failure_result(
            candidate,
            circuit,
            f"subprocess_returncode_{completed.returncode}: {error_excerpt}",
        )

    try:
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as exc:
        return make_exact_failure_result(candidate, circuit, f"subprocess_json_error: {exc}")
    return deserialize_worker_result(payload, circuit)


def evaluate_layout_candidate_with_timeout(
    candidate,
    circuit,
    phase_cycle,
    padding,
    max_same_phase,
    embedding_scores=None,
    timeout_sec=0,
):
    def _handle_timeout(_signum, _frame):
        raise ExactEvaluationTimeout(f"exact routing evaluation exceeded {timeout_sec}s")

    if timeout_sec is None or int(timeout_sec) <= 0:
        return evaluate_layout_candidate(
            candidate,
            circuit,
            phase_cycle,
            padding,
            max_same_phase,
            embedding_scores=embedding_scores,
        )

    if int(getattr(circuit, "effective_nodes_num", 0)) >= 80:
        return evaluate_layout_candidate_in_subprocess(
            candidate,
            circuit,
            phase_cycle,
            padding,
            max_same_phase,
            embedding_scores=embedding_scores,
            timeout_sec=timeout_sec,
        )

    previous_handler = signal.getsignal(signal.SIGALRM)
    signal.signal(signal.SIGALRM, _handle_timeout)
    signal.alarm(int(timeout_sec))
    try:
        return evaluate_layout_candidate(
            candidate,
            circuit,
            phase_cycle,
            padding,
            max_same_phase,
            embedding_scores=embedding_scores,
        )
    except ExactEvaluationTimeout:
        result = make_exact_failure_result(candidate, circuit, f"signal_timeout_{timeout_sec}s")
        result["exact_evaluation_timeout"] = True
        return result
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous_handler)


def select_fast_warm_start(
    circuit,
    ordered_layers,
    board_margin,
    base_x_spacing,
    base_y_spacing,
    embeddings,
    embedding_scores,
    allowed_strategies,
    allowed_orientations,
    best_selection_mode,
    aspect_ratio_limit,
    aspect_ratio_weight,
    max_span_weight,
    area_regression_weight,
):
    candidate_pool = list(
        build_layout_candidates(
            circuit,
            ordered_layers,
            board_margin,
            board_margin,
            base_x_spacing,
            base_y_spacing,
            embeddings=embeddings,
            allowed_strategies=allowed_strategies,
            allowed_orientations=allowed_orientations,
        )
    )
    best_result = None
    best_key = None
    for candidate in candidate_pool:
        result = evaluate_placement_only_candidate(
            {
                **candidate,
                "routing_embedding_guidance": bool(
                    embedding_scores
                    and candidate.get("routing_embedding_guidance", candidate["strategy"] == "gcn")
                ),
            },
            circuit,
        )
        cost = scalarize_layout_result(
            result,
            aspect_ratio_limit=aspect_ratio_limit,
            aspect_ratio_weight=aspect_ratio_weight,
            max_span_weight=max_span_weight,
            area_reference=float(result["area"]),
            area_regression_weight=area_regression_weight,
        )
        key = layout_selection_key(result, cost, best_selection_mode)
        if best_key is None or key < best_key:
            result["score"] = key
            best_result = result
            best_key = key
    return best_result


def build_single_start_candidate(
    circuit,
    ordered_layers,
    board_margin,
    x_spacing,
    y_spacing,
    embeddings,
    strategy,
    orientation,
):
    strategy = "gcn" if strategy == "auto" else strategy
    orientation = TOP_DOWN if orientation == "auto" else orientation
    if strategy == "fixed":
        positions = build_node_positions_with_fixed_spacing(
            ordered_layers,
            x_spacing,
            y_spacing,
            board_margin,
            board_margin,
            orientation,
        )
    elif strategy == "gcn":
        positions = build_gcn_guided_node_positions(
            circuit,
            ordered_layers,
            embeddings,
            x_spacing,
            y_spacing,
            board_margin,
            board_margin,
            orientation,
        )
    else:
        candidates = list(
            build_layout_candidates(
                circuit,
                ordered_layers,
                board_margin,
                board_margin,
                x_spacing,
                y_spacing,
                embeddings=embeddings,
                allowed_strategies=(strategy,),
                allowed_orientations=(orientation,),
            )
        )
        if not candidates:
            raise ValueError(f"No single-start candidate for strategy={strategy}, orientation={orientation}")
        return candidates[0]
    actual_x_spacing, actual_y_spacing = summarize_node_position_spacing(positions)
    return {
        "strategy": strategy,
        "orientation": orientation,
        "x_spacing": actual_x_spacing,
        "y_spacing": actual_y_spacing,
        "node_positions": positions,
        "routing_embedding_guidance": strategy == "gcn",
    }


def describe_action(action_def):
    action_type, target, delta = action_def
    if action_type == "noop":
        return "noop"
    if isinstance(target, tuple):
        preview = ",".join(str(int(node_id)) for node_id in target[:4])
        if len(target) > 4:
            preview = f"{preview},..."
        return f"{action_type}:[{preview}]:{delta:+d}"
    return f"{action_type}:{target}:{delta:+d}"


def action_experience_key(action_type, target, delta):
    if action_type == "noop":
        return "noop"
    if action_type == "segment_shift":
        segment_size = len(target) if isinstance(target, tuple) else 1
        if segment_size <= 2:
            bucket = "small"
        elif segment_size <= 5:
            bucket = "medium"
        else:
            bucket = "large"
        return f"{action_type}:{bucket}:{int(delta):+d}"
    return f"{action_type}:{int(delta):+d}"


def action_prior_from_memory(action_defs, memory, scale):
    actions = memory.get("actions", {}) if isinstance(memory, dict) else {}
    priors = []
    for action_def in action_defs:
        key = action_experience_key(*action_def)
        entry = actions.get(key, {})
        count = max(0, int(entry.get("count", 0)))
        if count <= 0:
            priors.append(0.0)
            continue
        mean_delta = float(entry.get("delta_sum", 0.0)) / float(count)
        positive_rate = float(entry.get("positive_count", 0)) / float(count)
        improvement_rate = float(entry.get("improvement_bonus_count", 0)) / float(count)
        prior = mean_delta + 0.5 * positive_rate + 1.5 * improvement_rate
        priors.append(float(np.clip(prior * float(scale), -2.5, 2.5)))
    return np.asarray(priors, dtype=np.float32)


def read_experience_memory(path):
    if not path or not os.path.exists(path):
        return {"version": 1, "actions": {}, "updates": []}
    try:
        with open(path, "r", encoding="utf-8") as json_file:
            payload = json.load(json_file)
    except (OSError, json.JSONDecodeError):
        return {"version": 1, "actions": {}, "updates": []}
    payload.setdefault("version", 1)
    payload.setdefault("actions", {})
    payload.setdefault("updates", [])
    return payload


def write_experience_memory(path, payload):
    if not path:
        return None
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as json_file:
        json.dump(payload, json_file, ensure_ascii=False, indent=2)
    return path


def update_experience_memory(memory, step_rows, benchmark_label, summary):
    actions = memory.setdefault("actions", {})
    for row in step_rows:
        action_type = str(row["action_type"])
        if action_type == "segment_shift":
            segment_size = max(1, int(row.get("action_target_size", 1)))
            target = tuple(range(segment_size))
        else:
            target = -1
        key = action_experience_key(action_type, target, int(row["action_delta"]))
        entry = actions.setdefault(
            key,
            {
                "count": 0,
                "reward_sum": 0.0,
                "delta_sum": 0.0,
                "positive_count": 0,
                "improvement_bonus_count": 0,
            },
        )
        reward = float(row["reward"])
        reward_delta = float(row["reward_delta_clipped"])
        entry["count"] += 1
        entry["reward_sum"] += reward
        entry["delta_sum"] += reward_delta
        entry["positive_count"] += int(reward > 0.0)
        entry["improvement_bonus_count"] += int(float(row["reward_improvement_bonus"]) > 0.0)

    memory.setdefault("updates", []).append(
        {
            "benchmark": benchmark_label,
            "area_improvement_ratio": float(summary.get("area_improvement_ratio", 0.0)),
            "best_cost": float(summary.get("best_cost", 0.0)),
            "warm_start_cost": float(summary.get("warm_start_cost", 0.0)),
            "step_count": int(len(step_rows)),
            "recorded_at_local": time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime()),
        }
    )
    return memory


@dataclass
class Transition:
    obs: np.ndarray
    action: int
    logprob: float
    reward: float
    value: float
    done: bool
    action_mask: np.ndarray


class LayoutCompactionEnv:
    def __init__(
        self,
        circuit,
        ordered_layers,
        embedding_scores,
        start_result,
        phase_cycle,
        padding,
        max_same_phase,
        max_steps,
        enable_secondary_squeeze,
        aspect_ratio_limit,
        aspect_ratio_weight,
        max_span_weight,
        area_regression_weight,
        best_selection_mode="legal-area",
        area_reward_weight=DEFAULT_AREA_REWARD_WEIGHT,
        train_eval_mode="exact",
        exact_eval_timeout_sec=60,
        clock_domain_randomization=False,
        phase_cycle_range=None,
        padding_range=None,
        max_same_phase_range=None,
        clock_random_seed=None,
        experience_memory=None,
        experience_prior_scale=DEFAULT_EXPERIENCE_PRIOR_SCALE,
        rollback_worse_actions=True,
    ):
        self.circuit = circuit
        self.ordered_layers = [[int(node_id) for node_id in layer] for layer in ordered_layers]
        self.embedding_scores = dict(embedding_scores)
        self.orientation = start_result["layout_orientation"]
        self.base_phase_cycle = int(phase_cycle)
        self.base_padding = int(padding)
        self.base_max_same_phase = int(max_same_phase)
        self.phase_cycle = int(phase_cycle)
        self.padding = int(padding)
        self.max_same_phase = int(max_same_phase)
        self.max_steps = max_steps
        self.enable_secondary_squeeze = bool(enable_secondary_squeeze)
        self.aspect_ratio_limit = float(aspect_ratio_limit)
        self.aspect_ratio_weight = float(aspect_ratio_weight)
        self.max_span_weight = float(max_span_weight)
        self.area_regression_weight = float(area_regression_weight)
        self.best_selection_mode = str(best_selection_mode)
        self.area_reward_weight = float(area_reward_weight)
        self.train_eval_mode = str(train_eval_mode)
        self.exact_eval_timeout_sec = int(exact_eval_timeout_sec)
        self.clock_domain_randomization = bool(clock_domain_randomization)
        self.phase_cycle_range = phase_cycle_range or (self.base_phase_cycle, self.base_phase_cycle)
        self.padding_range = padding_range or (self.base_padding, self.base_padding)
        self.max_same_phase_range = max_same_phase_range or (
            self.base_max_same_phase,
            self.base_max_same_phase,
        )
        rng_seed = self.base_phase_cycle * 10_000 + self.base_padding * 100 + self.base_max_same_phase
        if clock_random_seed is not None:
            rng_seed = int(clock_random_seed)
        self.clock_rng = np.random.default_rng(rng_seed)
        self.experience_memory = experience_memory or {}
        self.experience_prior_scale = float(experience_prior_scale)
        self.rollback_worse_actions = bool(rollback_worse_actions)

        self.node_ids = [node_id for layer in self.ordered_layers for node_id in layer]
        self.layer_count = len(self.ordered_layers)
        self.node_count = len(self.node_ids)
        self.node_to_layer = {
            int(node_id): layer_idx
            for layer_idx, nodes in enumerate(self.ordered_layers)
            for node_id in nodes
        }
        self.rank_in_layer = {
            int(node_id): rank
            for nodes in self.ordered_layers
            for rank, node_id in enumerate(nodes)
        }
        self.layer_route_loads = self._build_layer_route_loads()
        self.max_route_load = max((load for load, _branching in self.layer_route_loads), default=1)
        self.max_branch_load = max((branching for _load, branching in self.layer_route_loads), default=1)
        self.max_layer_width = max((len(layer) for layer in self.ordered_layers), default=1)
        self.input_nodes = {int(node_id) for node_id in circuit.getInputNodesIndex}
        self.output_nodes = {int(node_id) for node_id in circuit.getOutputNodesIndex}

        self.initial_positions = clone_positions(start_result["node_positions"])
        self.initial_result = self._compact_result(start_result)
        self.initial_cost = self._score_result(start_result)
        self.coord_norm = max(
            24.0,
            float(max(max(coord) for coord in self.initial_positions.values()) + 8),
        )
        self.cost_scale = max(250.0, self.initial_cost / 25.0)
        self.eval_cache = {}
        self.action_defs = self._build_action_defs()
        self.action_dim = len(self.action_defs)
        self.eval_calls_total = 0
        self.eval_cache_hits_total = 0
        self.eval_calls_episode = 0
        self.eval_cache_hits_episode = 0

        self.current_positions = {}
        self.current_result = None
        self.current_cost = 0.0
        self.best_positions = {}
        self.best_result = None
        self.best_cost = 0.0
        self.best_key = None
        self.step_count = 0
        self.episode_best_cost = 0.0

        sample_obs = self.reset()
        self.obs_dim = int(sample_obs.shape[0])

    def _sample_clock_context(self):
        if not self.clock_domain_randomization:
            self.phase_cycle = self.base_phase_cycle
            self.padding = self.base_padding
            self.max_same_phase = self.base_max_same_phase
            return
        self.phase_cycle = int(self.clock_rng.integers(self.phase_cycle_range[0], self.phase_cycle_range[1] + 1))
        self.padding = int(self.clock_rng.integers(self.padding_range[0], self.padding_range[1] + 1))
        self.max_same_phase = int(
            self.clock_rng.integers(
                self.max_same_phase_range[0],
                self.max_same_phase_range[1] + 1,
            )
        )

    def _score_result(self, result):
        return scalarize_layout_result(
            result,
            aspect_ratio_limit=self.aspect_ratio_limit,
            aspect_ratio_weight=self.aspect_ratio_weight,
            max_span_weight=self.max_span_weight,
            area_reference=self.initial_result["area"],
            area_regression_weight=self.area_regression_weight,
        )

    def _selection_key(self, compact, cost):
        return layout_selection_key(compact, cost, self.best_selection_mode)

    def _build_layer_route_loads(self):
        if self.layer_count <= 1:
            return []

        node_to_layer = self.node_to_layer
        boundary_loads = [0] * (self.layer_count - 1)
        branching_loads = [0] * (self.layer_count - 1)

        for src, dst in self.circuit.effective_edges:
            src = int(src)
            dst = int(dst)
            src_layer = node_to_layer.get(src)
            dst_layer = node_to_layer.get(dst)
            if src_layer is None or dst_layer is None or dst_layer <= src_layer:
                continue
            for boundary_idx in range(src_layer, dst_layer):
                boundary_loads[boundary_idx] += 1

        for boundary_idx, (left_nodes, right_nodes) in enumerate(zip(self.ordered_layers, self.ordered_layers[1:])):
            branching_left = sum(
                1 for node_id in left_nodes if len(self.circuit.get_fanouts(int(node_id))) > 1
            )
            branching_right = sum(
                1 for node_id in right_nodes if len(self.circuit.get_fanins(int(node_id))) > 1
            )
            branching_loads[boundary_idx] = branching_left + branching_right

        return list(zip(boundary_loads, branching_loads))

    def _build_action_defs(self):
        action_defs = [("noop", -1, 0)]
        primary_deltas = (-6, -4, -2, -1, 1, 2, 4)
        secondary_deltas = (-4, -2, -1, 1, 2, 4)
        for boundary_idx in range(max(0, self.layer_count - 1)):
            for delta in primary_deltas:
                action_defs.append(("gap", boundary_idx, delta))
        for layer_idx in range(self.layer_count):
            for delta in primary_deltas:
                action_defs.append(("layer_primary_shift", layer_idx, delta))
        for block_size in (2, 3):
            if self.layer_count < block_size:
                continue
            for start_idx in range(0, self.layer_count - block_size + 1):
                block_layers = tuple(range(start_idx, start_idx + block_size))
                for delta in primary_deltas:
                    action_defs.append(("layer_block_primary_shift", block_layers, delta))
        for block_size in (2, 3):
            if self.layer_count < block_size:
                continue
            for start_idx in range(0, self.layer_count - block_size + 1):
                block_layers = tuple(range(start_idx, start_idx + block_size))
                for delta in secondary_deltas:
                    action_defs.append(("layer_block_shift", block_layers, delta))
        seen_segments = set()

        def add_segment_actions(segment_nodes):
            segment_nodes = tuple(int(node_id) for node_id in segment_nodes)
            if len(segment_nodes) < 2 or segment_nodes in seen_segments:
                return
            seen_segments.add(segment_nodes)
            for delta in secondary_deltas:
                action_defs.append(("segment_shift", segment_nodes, delta))

        for layer_nodes in self.ordered_layers:
            if len(layer_nodes) < 3:
                continue
            sorted_nodes = [
                int(node_id)
                for node_id in sorted(
                    layer_nodes,
                    key=lambda node_id: (
                        self._secondary_axis(self.initial_positions[int(node_id)]),
                        int(node_id),
                    ),
                )
            ]
            for cut_idx in range(1, len(sorted_nodes)):
                if cut_idx >= 2:
                    add_segment_actions(sorted_nodes[:cut_idx])
                if (len(sorted_nodes) - cut_idx) >= 2:
                    add_segment_actions(sorted_nodes[cut_idx:])

            max_middle_size = min(5, len(sorted_nodes) - 1)
            for segment_size in range(2, max_middle_size + 1):
                for start_idx in range(0, len(sorted_nodes) - segment_size + 1):
                    add_segment_actions(sorted_nodes[start_idx : start_idx + segment_size])

        for node_id in self.node_ids:
            related_groups = (self.circuit.get_fanins(int(node_id)), self.circuit.get_fanouts(int(node_id)))
            for related_nodes in related_groups:
                nodes_by_layer = {}
                for related_node in related_nodes:
                    related_node = int(related_node)
                    layer_idx = self.node_to_layer.get(related_node)
                    if layer_idx is None:
                        continue
                    nodes_by_layer.setdefault(layer_idx, []).append(related_node)
                for related_layer_nodes in nodes_by_layer.values():
                    if len(related_layer_nodes) < 2:
                        continue
                    related_layer_nodes = sorted(
                        set(int(value) for value in related_layer_nodes),
                        key=lambda value: (
                            self._secondary_axis(self.initial_positions[int(value)]),
                            int(value),
                        ),
                    )
                    add_segment_actions(related_layer_nodes)

        for layer_idx in range(self.layer_count):
            for delta in secondary_deltas:
                action_defs.append(("layer_shift", layer_idx, delta))
        for node_id in self.node_ids:
            for delta in secondary_deltas:
                action_defs.append(("node_shift", int(node_id), delta))
        return action_defs

    def _compact_result(self, result):
        width = int(result["width"]) if np.isfinite(result["width"]) and int(result["width"]) > 0 else INVALID_LAYOUT_DIM
        height = int(result["height"]) if np.isfinite(result["height"]) and int(result["height"]) > 0 else INVALID_LAYOUT_DIM
        area = float(result["area"])
        if not np.isfinite(area) or area <= 0:
            area = max(INVALID_LAYOUT_AREA, float(width * height))
        direction_violation_count = int(result["direction_violation_count"]) if np.isfinite(result["direction_violation_count"]) and int(result["direction_violation_count"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
        route_overhang_penalty = int(result["route_overhang_penalty"]) if np.isfinite(result["route_overhang_penalty"]) and int(result["route_overhang_penalty"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
        io_exposure_penalty = int(result["io_exposure_penalty"]) if np.isfinite(result["io_exposure_penalty"]) and int(result["io_exposure_penalty"]) >= 0 else int(INVALID_LAYOUT_PENALTY)
        return {
            "width": width,
            "height": height,
            "area": area,
            "failed_edges": list(result["failed_edges"]),
            "direction_violation_count": direction_violation_count,
            "route_overhang_penalty": route_overhang_penalty,
            "io_exposure_penalty": io_exposure_penalty,
            "layout_orientation": result["layout_orientation"],
            "layout_strategy": result["layout_strategy"],
            "x_spacing": result["x_spacing"],
            "y_spacing": result["y_spacing"],
        }

    def _primary_axis(self, coord):
        return coord[0] if self.orientation == LEFT_RIGHT else coord[1]

    def _secondary_axis(self, coord):
        return coord[1] if self.orientation == LEFT_RIGHT else coord[0]

    def _make_coord(self, primary_axis, secondary_axis):
        primary_axis = int(primary_axis)
        secondary_axis = int(secondary_axis)
        if self.orientation == LEFT_RIGHT:
            return primary_axis, secondary_axis
        return secondary_axis, primary_axis

    def _get_layer_primary_positions(self, node_positions):
        primary_positions = []
        for layer_nodes in self.ordered_layers:
            if not layer_nodes:
                primary_positions.append(0)
                continue
            primary_positions.append(self._primary_axis(node_positions[int(layer_nodes[0])]))
        return primary_positions

    def _signature(self, node_positions):
        clock_prefix = (
            ("clock", int(self.phase_cycle), int(self.padding), int(self.max_same_phase)),
        )
        return clock_prefix + tuple(
            (int(node_id), int(coord[0]), int(coord[1]))
            for node_id, coord in sorted(node_positions.items())
        )

    def evaluate_positions(self, node_positions):
        self.eval_calls_total += 1
        self.eval_calls_episode += 1
        signature = self._signature(node_positions)
        cached = self.eval_cache.get(signature)
        if cached is not None:
            self.eval_cache_hits_total += 1
            self.eval_cache_hits_episode += 1
            return (*cached, True)

        candidate = {
            "strategy": "rl",
            "orientation": self.orientation,
            "x_spacing": "n/a",
            "y_spacing": "n/a",
            "node_positions": clone_positions(node_positions),
        }

        if self.train_eval_mode == "placement":
            result = evaluate_placement_only_candidate(candidate, self.circuit)
        else:
            result = evaluate_layout_candidate_with_timeout(
                candidate,
                self.circuit,
                self.phase_cycle,
                self.padding,
                self.max_same_phase,
                embedding_scores=None,
                timeout_sec=self.exact_eval_timeout_sec,
            )
        compact = self._compact_result(result)
        cost = self._score_result(compact)
        payload = (result, compact, float(cost))
        self.eval_cache[signature] = payload
        return (*payload, False)

    def _evaluate_positions_exact(self, node_positions):
        candidate = {
            "strategy": "phase-strip-pack",
            "orientation": self.orientation,
            "x_spacing": "n/a",
            "y_spacing": "n/a",
            "node_positions": clone_positions(node_positions),
        }
        result = evaluate_layout_candidate_with_timeout(
            candidate,
            self.circuit,
            self.phase_cycle,
            self.padding,
            self.max_same_phase,
            embedding_scores=None,
            timeout_sec=self.exact_eval_timeout_sec,
        )
        compact = self._compact_result(result)
        cost = self._score_result(compact)
        return result, compact, float(cost)

    def _occupied_coords_from_result(self, result):
        occupied_coords = {
            (int(coord[0]), int(coord[1]))
            for coord in result.get("node_positions", {}).values()
        }
        for path in result.get("routed_paths", {}).values():
            occupied_coords.update((int(coord[0]), int(coord[1])) for coord in path)
        return occupied_coords

    def _phase_match_rank(self, left_phases, right_phases):
        left_phases = set(int(phase) for phase in left_phases)
        right_phases = set(int(phase) for phase in right_phases)
        if left_phases and right_phases:
            if left_phases == right_phases and len(left_phases) == 1:
                return 0
            if left_phases == right_phases:
                return 1
            if left_phases & right_phases:
                return 2
        return 3

    def _build_phase_strip_pack_candidates(self, result, positions):
        materialized = materialize_result_board(self.circuit, result)
        occupied_coords = self._occupied_coords_from_result(materialized)
        if not occupied_coords:
            occupied_coords = {
                (int(coord[0]), int(coord[1])) for coord in positions.values()
            }
        if not occupied_coords:
            return []

        board = materialized.get("board")
        axis_phase_sets = {0: {}, 1: {}}
        if board is not None:
            for coord in occupied_coords:
                phase = int(board.getPhase((int(coord[0]), int(coord[1]))))
                if phase < 0:
                    continue
                axis_phase_sets[0].setdefault(int(coord[0]), set()).add(phase)
                axis_phase_sets[1].setdefault(int(coord[1]), set()).add(phase)
        for phase_entries in materialized.get("routed_path_phases", {}).values():
            for x, y, phase in phase_entries:
                phase = int(phase)
                if phase < 0:
                    continue
                axis_phase_sets[0].setdefault(int(x), set()).add(phase)
                axis_phase_sets[1].setdefault(int(y), set()).add(phase)

        primary_axis_index = 0 if self.orientation == LEFT_RIGHT else 1
        candidate_rows = []
        phase_cycle = max(1, int(self.phase_cycle))
        node_coords = {
            (int(coord[0]), int(coord[1])) for coord in positions.values()
        }

        for axis_index in (1 - primary_axis_index, primary_axis_index):
            axis_phase_map = axis_phase_sets.get(axis_index, {})
            axis_priority = 0 if axis_index != primary_axis_index else 1
            axis_value_groups = (
                ("occupied_gap", sorted({int(coord[axis_index]) for coord in occupied_coords}), 0),
                ("node_gap", sorted({int(coord[axis_index]) for coord in node_coords}), 1),
            )

            for gap_label, axis_values, source_priority in axis_value_groups:
                if len(axis_values) <= 1:
                    continue
                for left_value, right_value in zip(axis_values, axis_values[1:]):
                    gap_len = int(right_value) - int(left_value) - 1
                    if gap_len <= 0:
                        continue

                    left_phases = axis_phase_map.get(int(left_value), set())
                    right_phases = axis_phase_map.get(int(right_value), set())
                    phase_match_rank = self._phase_match_rank(left_phases, right_phases)

                    delta_candidates = {1, int(gap_len)}
                    if gap_len >= phase_cycle:
                        delta_candidates.add(int(phase_cycle))
                        full_cycle_delta = int((gap_len // phase_cycle) * phase_cycle)
                        if full_cycle_delta > 0:
                            delta_candidates.add(full_cycle_delta)

                    for delta in sorted(delta_candidates, reverse=True):
                        if delta <= 0 or delta > gap_len:
                            continue
                        candidate_rows.append(
                            (
                                (
                                    axis_priority,
                                    source_priority,
                                    phase_match_rank,
                                    0 if delta == gap_len else 1,
                                    0 if (delta % phase_cycle) == 0 else 1,
                                    -int(delta),
                                    int(left_value),
                                ),
                                {
                                    "label": gap_label,
                                    "axis_index": int(axis_index),
                                    "cutoff": int(left_value) + int(gap_len),
                                    "delta": int(delta),
                                    "gap_len": int(gap_len),
                                    "phase_match_rank": int(phase_match_rank),
                                },
                            )
                        )

        return [payload for _priority, payload in sorted(candidate_rows, key=lambda item: item[0])]

    def _apply_suffix_axis_shift(self, positions, axis_index, cutoff, delta):
        axis_index = int(axis_index)
        cutoff = int(cutoff)
        delta = int(delta)
        if delta <= 0:
            return False

        moved = {}
        for node_id, coord in positions.items():
            coord = (int(coord[0]), int(coord[1]))
            if int(coord[axis_index]) <= cutoff:
                continue
            next_coord = list(coord)
            next_coord[axis_index] -= delta
            if next_coord[axis_index] < 1:
                return False
            moved[int(node_id)] = (int(next_coord[0]), int(next_coord[1]))

        if not moved:
            return False

        target_coords = {tuple(coord) for coord in moved.values()}
        if len(target_coords) != len(moved):
            return False
        for other_node_id, other_coord in positions.items():
            if int(other_node_id) in moved:
                continue
            if tuple(other_coord) in target_coords:
                return False

        primary_axis_index = 0 if self.orientation == LEFT_RIGHT else 1
        if axis_index == primary_axis_index:
            projected_layer_primary = []
            for layer_nodes in self.ordered_layers:
                layer_primary_values = {
                    int(moved.get(int(node_id), positions[int(node_id)])[axis_index])
                    for node_id in layer_nodes
                }
                if len(layer_primary_values) != 1:
                    return False
                projected_layer_primary.append(layer_primary_values.pop())
            for left_primary, right_primary in zip(projected_layer_primary, projected_layer_primary[1:]):
                if int(right_primary) <= int(left_primary):
                    return False

        for node_id, coord in moved.items():
            positions[int(node_id)] = coord
        return True

    def _is_gap_action_valid(self, boundary_idx, delta):
        if delta > 0:
            return True
        primary_positions = self._get_layer_primary_positions(self.current_positions)
        current_gap = primary_positions[boundary_idx + 1] - primary_positions[boundary_idx]
        return (current_gap + int(delta)) > 0

    def _is_primary_layer_shift_valid(self, layer_indices, delta, positions=None):
        positions = self.current_positions if positions is None else positions
        layer_indices = tuple(int(layer_idx) for layer_idx in layer_indices)
        shifted_layers = set(layer_indices)
        primary_positions = self._get_layer_primary_positions(positions)
        for layer_idx in shifted_layers:
            if layer_idx < 0 or layer_idx >= self.layer_count:
                return False
            primary_positions[layer_idx] += int(delta)
            if primary_positions[layer_idx] < 1:
                return False
        for left, right in zip(primary_positions, primary_positions[1:]):
            if right <= left:
                return False

        target_coords = {}
        shifted_nodes = set()
        for layer_idx in shifted_layers:
            for node_id in self.ordered_layers[layer_idx]:
                node_id = int(node_id)
                shifted_nodes.add(node_id)
                coord = positions[node_id]
                target_coords[node_id] = self._make_coord(
                    self._primary_axis(coord) + int(delta),
                    self._secondary_axis(coord),
                )

        occupied_targets = {tuple(coord) for coord in target_coords.values()}
        for other_node_id, other_coord in positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in occupied_targets:
                return False
        return True

    def _is_layer_shift_valid(self, layer_idx, delta):
        nodes = self.ordered_layers[layer_idx]
        target_secondaries = [
            self._secondary_axis(self.current_positions[int(node_id)]) + delta
            for node_id in nodes
        ]
        return min(target_secondaries, default=1) >= 1

    def _is_layer_group_shift_valid(self, layer_indices, delta, positions=None):
        positions = self.current_positions if positions is None else positions
        shifted_nodes = set()
        for layer_idx in layer_indices:
            layer_idx = int(layer_idx)
            if layer_idx < 0 or layer_idx >= self.layer_count:
                return False
            shifted_nodes.update(int(node_id) for node_id in self.ordered_layers[layer_idx])

        target_coords = set()
        for node_id in shifted_nodes:
            coord = positions[node_id]
            primary_axis = self._primary_axis(coord)
            target_secondary = self._secondary_axis(coord) + int(delta)
            if target_secondary < 1:
                return False
            target_coord = self._make_coord(primary_axis, target_secondary)
            if target_coord in target_coords:
                return False
            target_coords.add(target_coord)

        for other_node_id, other_coord in positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in target_coords:
                return False
        return True

    def _is_segment_shift_valid(self, node_ids, delta):
        shifted_nodes = {int(node_id) for node_id in node_ids}
        target_coords = {}
        for node_id in shifted_nodes:
            coord = self.current_positions[node_id]
            primary_axis = self._primary_axis(coord)
            target_secondary = self._secondary_axis(coord) + delta
            if target_secondary < 1:
                return False
            target_coords[node_id] = self._make_coord(primary_axis, target_secondary)

        occupied_targets = {tuple(coord) for coord in target_coords.values()}
        for other_node_id, other_coord in self.current_positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in occupied_targets:
                return False
        return True

    def _is_node_shift_valid(self, node_id, delta):
        node_id = int(node_id)
        coord = self.current_positions[node_id]
        primary_axis = self._primary_axis(coord)
        target_secondary = self._secondary_axis(coord) + delta
        if target_secondary < 1:
            return False
        target_coord = self._make_coord(primary_axis, target_secondary)
        for other_node_id, other_coord in self.current_positions.items():
            if int(other_node_id) == node_id:
                continue
            if tuple(other_coord) == target_coord:
                return False
        return True

    def _is_action_valid(self, action_def):
        action_type, target, delta = action_def
        if action_type == "noop":
            return True
        if action_type == "gap":
            return self._is_gap_action_valid(target, delta)
        if action_type == "layer_primary_shift":
            return self._is_primary_layer_shift_valid((target,), delta)
        if action_type == "layer_block_primary_shift":
            return self._is_primary_layer_shift_valid(target, delta)
        if action_type == "layer_block_shift":
            return self._is_layer_group_shift_valid(target, delta)
        if action_type == "segment_shift":
            return self._is_segment_shift_valid(target, delta)
        if action_type == "layer_shift":
            return self._is_layer_shift_valid(target, delta)
        if action_type == "node_shift":
            return self._is_node_shift_valid(target, delta)
        return False

    def get_action_mask(self):
        mask = np.zeros(self.action_dim, dtype=bool)
        for idx, action_def in enumerate(self.action_defs):
            mask[idx] = self._is_action_valid(action_def)
        return mask

    def get_action_prior_logits(self):
        return action_prior_from_memory(
            self.action_defs,
            self.experience_memory,
            self.experience_prior_scale,
        )

    def _apply_gap_action(self, positions, boundary_idx, delta):
        primary_positions = self._get_layer_primary_positions(positions)
        if delta < 0 and (primary_positions[boundary_idx + 1] - primary_positions[boundary_idx] + int(delta)) <= 0:
            return False

        for layer_idx in range(boundary_idx + 1, self.layer_count):
            for node_id in self.ordered_layers[layer_idx]:
                coord = positions[int(node_id)]
                primary_axis = self._primary_axis(coord) + delta
                secondary_axis = self._secondary_axis(coord)
                if primary_axis < 1:
                    return False
                positions[int(node_id)] = self._make_coord(primary_axis, secondary_axis)
        return True

    def _apply_primary_layer_shift(self, positions, layer_indices, delta):
        layer_indices = tuple(int(layer_idx) for layer_idx in layer_indices)
        if not self._is_primary_layer_shift_valid(layer_indices, delta, positions=positions):
            return False
        for layer_idx in layer_indices:
            for node_id in self.ordered_layers[layer_idx]:
                coord = positions[int(node_id)]
                positions[int(node_id)] = self._make_coord(
                    self._primary_axis(coord) + int(delta),
                    self._secondary_axis(coord),
                )
        return True

    def _apply_layer_shift(self, positions, layer_idx, delta):
        for node_id in self.ordered_layers[layer_idx]:
            coord = positions[int(node_id)]
            primary_axis = self._primary_axis(coord)
            secondary_axis = self._secondary_axis(coord) + delta
            if secondary_axis < 1:
                return False
            positions[int(node_id)] = self._make_coord(primary_axis, secondary_axis)
        return True

    def _apply_layer_group_shift(self, positions, layer_indices, delta):
        if not self._is_layer_group_shift_valid(layer_indices, delta, positions=positions):
            return False
        for layer_idx in layer_indices:
            for node_id in self.ordered_layers[int(layer_idx)]:
                node_id = int(node_id)
                coord = positions[node_id]
                positions[node_id] = self._make_coord(
                    self._primary_axis(coord),
                    self._secondary_axis(coord) + int(delta),
                )
        return True

    def _apply_segment_shift(self, positions, node_ids, delta):
        shifted_nodes = {int(node_id) for node_id in node_ids}
        target_coords = {}
        for node_id in shifted_nodes:
            coord = positions[node_id]
            primary_axis = self._primary_axis(coord)
            secondary_axis = self._secondary_axis(coord) + delta
            if secondary_axis < 1:
                return False
            target_coords[node_id] = self._make_coord(primary_axis, secondary_axis)

        occupied_targets = {tuple(coord) for coord in target_coords.values()}
        for other_node_id, other_coord in positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in occupied_targets:
                return False

        for node_id, coord in target_coords.items():
            positions[int(node_id)] = coord
        return True

    def _apply_node_shift(self, positions, node_id, delta):
        node_id = int(node_id)
        coord = positions[node_id]
        primary_axis = self._primary_axis(coord)
        secondary_axis = self._secondary_axis(coord) + delta
        if secondary_axis < 1:
            return False
        target_coord = self._make_coord(primary_axis, secondary_axis)
        if any(
            int(other_node_id) != node_id and tuple(other_coord) == target_coord
            for other_node_id, other_coord in positions.items()
        ):
            return False
        positions[node_id] = target_coord
        return True

    def _apply_action(self, positions, action_idx):
        action_type, target, delta = self.action_defs[int(action_idx)]
        if action_type == "noop":
            return True
        if action_type == "gap":
            return self._apply_gap_action(positions, target, delta)
        if action_type == "layer_primary_shift":
            return self._apply_primary_layer_shift(positions, (target,), delta)
        if action_type == "layer_block_primary_shift":
            return self._apply_primary_layer_shift(positions, target, delta)
        if action_type == "layer_block_shift":
            return self._apply_layer_group_shift(positions, target, delta)
        if action_type == "segment_shift":
            return self._apply_segment_shift(positions, target, delta)
        if action_type == "layer_shift":
            return self._apply_layer_shift(positions, target, delta)
        if action_type == "node_shift":
            return self._apply_node_shift(positions, target, delta)
        return False

    def _squeeze_secondary_axis(self, positions):
        if not self.enable_secondary_squeeze or not positions:
            return

        desired_secondary_map = build_desired_secondary_map(
            self.circuit,
            self.ordered_layers,
            positions,
            self.orientation,
            self.embedding_scores,
        )

        for layer_nodes in self.ordered_layers:
            if len(layer_nodes) <= 1:
                continue

            sorted_nodes = [
                int(node_id)
                for node_id in sorted(
                    layer_nodes,
                    key=lambda node_id: (
                        self._secondary_axis(positions[int(node_id)]),
                        int(node_id),
                    ),
                )
            ]
            current_secondaries = [
                self._secondary_axis(positions[int(node_id)])
                for node_id in sorted_nodes
            ]
            base_spacing = infer_layer_secondary_spacing(
                sorted_nodes,
                positions,
                self.orientation,
            )
            targets = [
                0.55 * float(current_secondary) +
                0.45 * float(desired_secondary_map.get(int(node_id), current_secondary))
                for node_id, current_secondary in zip(sorted_nodes, current_secondaries)
            ]
            min_gaps = [max(1, min(2, int(base_spacing))) for _ in range(len(sorted_nodes) - 1)]
            squeezed_secondaries = solve_ordered_targets_with_gaps(
                targets,
                min_gaps,
                axis_origin=1,
            )
            for node_id, new_secondary in zip(sorted_nodes, squeezed_secondaries):
                coord = positions[int(node_id)]
                positions[int(node_id)] = self._make_coord(
                    self._primary_axis(coord),
                    new_secondary,
                )

        min_primary = min((self._primary_axis(coord) for coord in positions.values()), default=1)
        min_secondary = min((self._secondary_axis(coord) for coord in positions.values()), default=1)
        shift_primary = max(0, int(min_primary) - 1)
        shift_secondary = max(0, int(min_secondary) - 1)
        if shift_primary <= 0 and shift_secondary <= 0:
            return

        for node_id, coord in list(positions.items()):
            positions[int(node_id)] = self._make_coord(
                self._primary_axis(coord) - shift_primary,
                self._secondary_axis(coord) - shift_secondary,
            )

    def greedy_primary_pack(self, positions, best_compact, best_cost, max_rounds=4):
        best_positions = clone_positions(positions)
        best_result = None
        best_compact = dict(best_compact)
        best_cost = float(best_cost)
        best_key = self._selection_key(best_compact, best_cost)
        action_candidates = []
        compression_deltas = (-4, -2, -1)
        for boundary_idx in range(max(0, self.layer_count - 1)):
            for delta in compression_deltas:
                action_candidates.append(("gap", boundary_idx, delta))
        for layer_idx in range(1, self.layer_count):
            for delta in compression_deltas:
                action_candidates.append(("layer_primary_shift", layer_idx, delta))
        for block_size in (2, 3):
            if self.layer_count < block_size:
                continue
            for start_idx in range(1, self.layer_count - block_size + 1):
                for delta in compression_deltas:
                    action_candidates.append(
                        ("layer_block_primary_shift", tuple(range(start_idx, start_idx + block_size)), delta)
                    )

        for _round in range(max(0, int(max_rounds))):
            round_improved = False
            for action_def in action_candidates:
                candidate_positions = clone_positions(best_positions)
                if not self._apply_action_to_positions(candidate_positions, action_def):
                    continue
                self._squeeze_secondary_axis(candidate_positions)
                candidate_result, candidate_compact, candidate_cost, _cache_hit = self.evaluate_positions(
                    candidate_positions
                )
                candidate_key = self._selection_key(candidate_compact, candidate_cost)
                if candidate_key < best_key:
                    best_positions = candidate_positions
                    best_result = candidate_result
                    best_compact = candidate_compact
                    best_cost = float(candidate_cost)
                    best_key = candidate_key
                    round_improved = True
                    break
            if not round_improved:
                break
        return best_result, best_compact, best_cost, best_positions

    def _projected_secondary_span(self, positions, node_ids, delta):
        shifted_nodes = {int(node_id) for node_id in node_ids}
        projected_secondaries = []
        for node_id, coord in positions.items():
            secondary_axis = self._secondary_axis(coord)
            if int(node_id) in shifted_nodes:
                secondary_axis += int(delta)
            projected_secondaries.append(secondary_axis)
        if not projected_secondaries:
            return 0
        return int(max(projected_secondaries) - min(projected_secondaries))

    def _node_bbox_key(self, positions):
        if not positions:
            return (0, 0, 0)
        xs = [int(coord[0]) for coord in positions.values()]
        ys = [int(coord[1]) for coord in positions.values()]
        width = max(xs) - min(xs) + 1
        height = max(ys) - min(ys) + 1
        return (int(width * height), int(max(width, height)), int(width + height))

    def _area_pack_key(self, compact, cost):
        width = int(compact["width"])
        height = int(compact["height"])
        route_penalty = int(compact["route_overhang_penalty"]) + int(compact["io_exposure_penalty"])
        return (
            int(len(compact["failed_edges"])),
            int(compact["direction_violation_count"]),
            float(compact["area"]),
            int(max(width, height)),
            int(width + height),
            route_penalty,
            float(cost),
        )

    def _build_area_pack_candidates(self, positions):
        candidates = []
        sequence = 0

        def add(priority, action_def):
            nonlocal sequence
            candidates.append((tuple(priority) + (sequence,), action_def))
            sequence += 1

        primary_positions = self._get_layer_primary_positions(positions)
        compression_deltas = (-4, -2, -1)
        secondary_deltas = (-2, -1, 1, 2)
        for boundary_idx in range(max(0, self.layer_count - 1)):
            current_gap = int(primary_positions[boundary_idx + 1]) - int(primary_positions[boundary_idx])
            valid_deltas = sorted(
                {
                    -min(abs(delta), max(0, current_gap - 1))
                    for delta in compression_deltas
                    if current_gap > 1
                }
            )
            for delta in valid_deltas:
                if delta < 0:
                    add((0, abs(delta), boundary_idx), ("gap", boundary_idx, delta))

        for layer_idx in range(1, self.layer_count):
            for delta in compression_deltas:
                add((1, abs(delta), layer_idx), ("layer_primary_shift", layer_idx, delta))

        for block_size in (2, 3):
            if self.layer_count < block_size:
                continue
            for start_idx in range(1, self.layer_count - block_size + 1):
                for delta in compression_deltas:
                    add(
                        (2, abs(delta), start_idx, block_size),
                        ("layer_block_primary_shift", tuple(range(start_idx, start_idx + block_size)), delta),
                    )

        secondary_values = [self._secondary_axis(coord) for coord in positions.values()]
        current_secondary_span = max(secondary_values) - min(secondary_values) if secondary_values else 0

        for layer_idx, layer_nodes in enumerate(self.ordered_layers):
            layer_nodes = tuple(int(node_id) for node_id in layer_nodes)
            for delta in secondary_deltas:
                projected_span = self._projected_secondary_span(positions, layer_nodes, delta)
                if projected_span <= current_secondary_span:
                    add((3, projected_span, abs(delta), layer_idx, delta), ("layer_shift", layer_idx, delta))

        for block_size in (2, 3):
            if self.layer_count < block_size:
                continue
            for start_idx in range(0, self.layer_count - block_size + 1):
                block_layers = tuple(range(start_idx, start_idx + block_size))
                block_nodes = tuple(
                    int(node_id)
                    for layer_idx in block_layers
                    for node_id in self.ordered_layers[layer_idx]
                )
                for delta in secondary_deltas:
                    projected_span = self._projected_secondary_span(positions, block_nodes, delta)
                    if projected_span <= current_secondary_span:
                        add(
                            (4, projected_span, abs(delta), start_idx, block_size, delta),
                            ("layer_block_shift", block_layers, delta),
                        )

        for action_def in self.action_defs:
            action_type, target, delta = action_def
            if action_type == "segment_shift":
                projected_span = self._projected_secondary_span(positions, target, delta)
                if projected_span <= current_secondary_span:
                    add((5, projected_span, abs(delta), len(target), delta), action_def)
            elif action_type == "node_shift":
                node_id = int(target)
                projected_span = self._projected_secondary_span(positions, (node_id,), delta)
                if projected_span <= current_secondary_span:
                    add((6, projected_span, abs(delta), node_id, delta), action_def)

        return [action_def for _priority, action_def in sorted(candidates)]

    def greedy_area_pack(self, positions, best_compact, best_cost, max_rounds=6, max_evaluations=160):
        best_positions = clone_positions(positions)
        best_result = None
        best_compact = dict(best_compact)
        best_cost = float(best_cost)
        best_pack_key = self._area_pack_key(best_compact, best_cost)
        best_bbox_key = self._node_bbox_key(best_positions)
        evaluation_count = 0
        evaluation_budget = int(max_evaluations) if max_evaluations is not None else 0

        for _round in range(max(0, int(max_rounds))):
            if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                break
            round_improved = False
            for action_def in self._build_area_pack_candidates(best_positions):
                if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                    break
                candidate_positions = clone_positions(best_positions)
                if not self._apply_action_to_positions(candidate_positions, action_def):
                    continue
                self._squeeze_secondary_axis(candidate_positions)
                candidate_bbox_key = self._node_bbox_key(candidate_positions)
                if candidate_bbox_key > best_bbox_key:
                    continue
                candidate_result, candidate_compact, candidate_cost, _cache_hit = self.evaluate_positions(
                    candidate_positions
                )
                evaluation_count += 1
                candidate_pack_key = self._area_pack_key(candidate_compact, candidate_cost)
                if candidate_pack_key < best_pack_key:
                    best_positions = candidate_positions
                    best_result = candidate_result
                    best_compact = candidate_compact
                    best_cost = float(candidate_cost)
                    best_pack_key = candidate_pack_key
                    best_bbox_key = candidate_bbox_key
                    round_improved = True
                    break
            if not round_improved:
                break
        return best_result, best_compact, best_cost, best_positions, evaluation_count

    def greedy_phase_strip_pack(
        self,
        positions,
        best_result,
        best_compact,
        best_cost,
        max_rounds=4,
        max_evaluations=96,
        ):
        best_positions = clone_positions(positions)
        improved = False
        if (
            best_result is None or
            best_result.get("board") is None or
            bool(best_result.get("placement_only_evaluation", False))
        ):
            best_result, best_compact, best_cost = self._evaluate_positions_exact(best_positions)
        else:
            best_result = materialize_result_board(self.circuit, best_result)
            best_compact = self._compact_result(best_result)
            best_cost = self._score_result(best_compact)

        best_pack_key = self._area_pack_key(best_compact, best_cost)
        evaluation_count = 0
        evaluation_budget = int(max_evaluations) if max_evaluations is not None else 0

        for _round in range(max(0, int(max_rounds))):
            if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                break
            round_improved = False
            for candidate in self._build_phase_strip_pack_candidates(best_result, best_positions):
                if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                    break
                candidate_positions = clone_positions(best_positions)
                if not self._apply_suffix_axis_shift(
                    candidate_positions,
                    candidate["axis_index"],
                    candidate["cutoff"],
                    candidate["delta"],
                ):
                    continue
                self._squeeze_secondary_axis(candidate_positions)
                candidate_result, candidate_compact, candidate_cost = self._evaluate_positions_exact(
                    candidate_positions
                )
                evaluation_count += 1
                candidate_pack_key = self._area_pack_key(candidate_compact, candidate_cost)
                if candidate_pack_key < best_pack_key:
                    best_positions = candidate_positions
                    best_result = candidate_result
                    best_compact = candidate_compact
                    best_cost = float(candidate_cost)
                    best_pack_key = candidate_pack_key
                    improved = True
                    round_improved = True
                    break
            if not round_improved:
                break

        return best_result, best_compact, best_cost, best_positions, evaluation_count, improved

    def _apply_action_to_positions(self, positions, action_def):
        action_type, target, delta = action_def
        if action_type == "noop":
            return True
        if action_type == "gap":
            return self._apply_gap_action(positions, target, delta)
        if action_type == "layer_primary_shift":
            return self._apply_primary_layer_shift(positions, (target,), delta)
        if action_type == "layer_block_primary_shift":
            return self._apply_primary_layer_shift(positions, target, delta)
        if action_type == "layer_block_shift":
            return self._apply_layer_group_shift(positions, target, delta)
        if action_type == "segment_shift":
            return self._apply_segment_shift(positions, target, delta)
        if action_type == "layer_shift":
            return self._apply_layer_shift(positions, target, delta)
        if action_type == "node_shift":
            return self._apply_node_shift(positions, target, delta)
        return False

    def _build_observation(self):
        compact = self.current_result
        primary_positions = self._get_layer_primary_positions(self.current_positions)
        primary_gaps = [
            right - left for left, right in zip(primary_positions, primary_positions[1:])
        ]

        global_features = [
            float(self.step_count) / max(1.0, float(self.max_steps)),
            float(compact["width"]) / self.coord_norm,
            float(compact["height"]) / self.coord_norm,
            float(compact["area"]) / max(1.0, float(self.initial_result["area"])),
            float(len(compact["failed_edges"])) / max(1.0, float(len(self.circuit.effective_edges))),
            float(compact["direction_violation_count"]) / max(1.0, float(self.node_count)),
            float(compact["route_overhang_penalty"]) / self.coord_norm,
            float(compact["io_exposure_penalty"]) / self.coord_norm,
            float(self.current_cost) / max(1.0, self.initial_cost),
            float(self.best_cost) / max(1.0, self.initial_cost),
            float(self.phase_cycle) / max(1.0, float(self.phase_cycle_range[1])),
            float(self.padding) / max(1.0, float(self.padding_range[1])),
            float(self.max_same_phase) / max(1.0, float(max(1, self.max_same_phase_range[1]))),
        ]

        layer_features = []
        for layer_idx, layer_nodes in enumerate(self.ordered_layers):
            secondary_values = [
                self._secondary_axis(self.current_positions[int(node_id)]) for node_id in layer_nodes
            ]
            layer_scores = [
                self.embedding_scores.get(int(node_id), 0.5) for node_id in layer_nodes
            ]
            route_load, branch_load = (
                self.layer_route_loads[layer_idx]
                if layer_idx < len(self.layer_route_loads)
                else (0, 0)
            )
            layer_features.extend(
                [
                    float(primary_positions[layer_idx]) / self.coord_norm,
                    float(np.mean(secondary_values)) / self.coord_norm if secondary_values else 0.0,
                    float(max(secondary_values) - min(secondary_values)) / self.coord_norm if len(secondary_values) > 1 else 0.0,
                    float(len(layer_nodes)) / max(1.0, float(self.max_layer_width)),
                    float(np.mean(layer_scores)) if layer_scores else 0.5,
                    float(max(layer_scores) - min(layer_scores)) if len(layer_scores) > 1 else 0.0,
                    float(route_load) / max(1.0, float(self.max_route_load)),
                    float(branch_load) / max(1.0, float(self.max_branch_load)),
                    float(primary_gaps[layer_idx]) / self.coord_norm if layer_idx < len(primary_gaps) else 0.0,
                ]
            )

        node_features = []
        for node_id in self.node_ids:
            coord = self.current_positions[int(node_id)]
            layer_nodes = self.ordered_layers[self.node_to_layer[int(node_id)]]
            node_features.extend(
                [
                    float(coord[0]) / self.coord_norm,
                    float(coord[1]) / self.coord_norm,
                    self.embedding_scores.get(int(node_id), 0.5),
                    float(len(self.circuit.get_fanins(int(node_id)))) / 4.0,
                    float(len(self.circuit.get_fanouts(int(node_id)))) / 4.0,
                    float(self.node_to_layer[int(node_id)]) / max(1.0, float(self.layer_count - 1)),
                    float(self.rank_in_layer[int(node_id)]) / max(1.0, float(len(layer_nodes) - 1)),
                    1.0 if int(node_id) in self.input_nodes else 0.0,
                    1.0 if int(node_id) in self.output_nodes else 0.0,
                ]
            )

        obs = np.asarray(global_features + layer_features + node_features, dtype=np.float32)
        return obs

    def reset(self, start_positions=None, start_compact=None, start_cost=None):
        self._sample_clock_context()
        if start_positions is None:
            self.current_positions = clone_positions(self.initial_positions)
        else:
            self.current_positions = clone_positions(start_positions)

        if start_positions is not None and not self.clock_domain_randomization and start_compact is not None and start_cost is not None:
            self.current_result = dict(start_compact)
            self.current_cost = float(start_cost)
        elif self.clock_domain_randomization or start_positions is not None:
            _result, compact, cost, _cache_hit = self.evaluate_positions(self.current_positions)
            self.current_result = compact
            self.current_cost = float(cost)
        else:
            self.current_result = dict(self.initial_result)
            self.current_cost = float(self.initial_cost)
        self.best_positions = clone_positions(self.current_positions)
        self.best_result = dict(self.current_result)
        self.best_cost = float(self.current_cost)
        self.best_key = self._selection_key(self.best_result, self.best_cost)
        self.step_count = 0
        self.episode_best_cost = float(self.current_cost)
        self.eval_calls_episode = 0
        self.eval_cache_hits_episode = 0
        return self._build_observation()

    def step(self, action_idx):
        self.step_count += 1
        reward_step_penalty = -0.02
        reward_invalid_penalty = 0.0
        reward_delta_raw = 0.0
        reward_delta_clipped = 0.0
        reward_area_delta_clipped = 0.0
        reward_improvement_bonus = 0.0
        reward_rollback_penalty = 0.0
        reward = reward_step_penalty
        action_idx = int(action_idx)
        action_def = self.action_defs[action_idx]
        action_type, target, delta = action_def
        info = {
            "invalid_action": False,
            "action_index": action_idx,
            "action_type": action_type,
            "action_target": target,
            "action_delta": delta,
            "action_label": describe_action(action_def),
            "phase_cycle": int(self.phase_cycle),
            "padding": int(self.padding),
            "max_same_phase": int(self.max_same_phase),
            "best_cost": float(self.best_cost),
            "current_cost": float(self.current_cost),
            "reward_step_penalty": reward_step_penalty,
            "reward_invalid_penalty": reward_invalid_penalty,
            "reward_delta_raw": reward_delta_raw,
            "reward_delta_clipped": reward_delta_clipped,
            "reward_area_delta_clipped": reward_area_delta_clipped,
            "reward_improvement_bonus": reward_improvement_bonus,
            "reward_rollback_penalty": reward_rollback_penalty,
            "accepted_action": True,
            "rollback_action": False,
        }

        if not self._is_action_valid(action_def):
            reward_invalid_penalty = -0.35
            reward += reward_invalid_penalty
            info["invalid_action"] = True
            info["reward_invalid_penalty"] = reward_invalid_penalty
            done = self.step_count >= self.max_steps
            return self._build_observation(), reward, done, info

        previous_positions = clone_positions(self.current_positions)
        previous_result = dict(self.current_result)
        previous_key = self._selection_key(previous_result, self.current_cost)
        candidate_positions = clone_positions(self.current_positions)
        if not self._apply_action(candidate_positions, action_idx):
            reward_invalid_penalty = -0.35
            reward += reward_invalid_penalty
            info["invalid_action"] = True
            info["reward_invalid_penalty"] = reward_invalid_penalty
            done = self.step_count >= self.max_steps
            return self._build_observation(), reward, done, info
        self._squeeze_secondary_axis(candidate_positions)

        previous_cost = float(self.current_cost)
        previous_area = float(self.current_result["area"])
        result, compact, cost, cache_hit = self.evaluate_positions(candidate_positions)
        reward_delta_raw = (previous_cost - cost) / self.cost_scale
        reward_delta_clipped = float(np.clip(reward_delta_raw, -4.0, 4.0))
        area_scale = max(1.0, float(self.initial_result["area"]) / 10.0)
        reward_area_delta_clipped = float(
            np.clip((previous_area - float(compact["area"])) / area_scale, -4.0, 4.0)
        )

        candidate_key = self._selection_key(compact, cost)
        accepted_action = True
        if self.rollback_worse_actions and candidate_key > previous_key:
            accepted_action = False
            reward_delta_clipped = -0.10
            reward_area_delta_clipped = 0.0
            reward_rollback_penalty = -0.05
            self.current_positions = previous_positions
            self.current_result = previous_result
            self.current_cost = previous_cost
        else:
            self.current_positions = candidate_positions
            self.current_result = compact
            self.current_cost = cost

        reward += reward_delta_clipped
        reward += float(self.area_reward_weight) * reward_area_delta_clipped
        reward += reward_rollback_penalty

        if self.best_key is None or candidate_key < self.best_key:
            self.best_cost = float(cost)
            self.best_result = compact
            self.best_key = candidate_key
            self.best_positions = clone_positions(candidate_positions)
            self.episode_best_cost = min(self.episode_best_cost, cost)
            reward_improvement_bonus = 1.0
            reward += reward_improvement_bonus

        done = self.step_count >= self.max_steps
        current_compact = self.current_result
        info.update(
            {
                "best_cost": float(self.best_cost),
                "current_cost": float(self.current_cost),
                "previous_cost": previous_cost,
                "width": int(current_compact["width"]),
                "height": int(current_compact["height"]),
                "failed_edges": len(current_compact["failed_edges"]),
                "area": float(current_compact["area"]),
                "direction_violation_count": int(current_compact["direction_violation_count"]),
                "route_overhang_penalty": int(current_compact["route_overhang_penalty"]),
                "io_exposure_penalty": int(current_compact["io_exposure_penalty"]),
                "reward_invalid_penalty": reward_invalid_penalty,
                "reward_delta_raw": float(reward_delta_raw),
                "reward_delta_clipped": reward_delta_clipped,
                "reward_area_delta_clipped": reward_area_delta_clipped,
                "reward_improvement_bonus": reward_improvement_bonus,
                "reward_rollback_penalty": reward_rollback_penalty,
                "accepted_action": bool(accepted_action),
                "rollback_action": bool(not accepted_action),
                "candidate_cost": float(cost),
                "candidate_width": int(compact["width"]),
                "candidate_height": int(compact["height"]),
                "candidate_area": float(compact["area"]),
                "candidate_failed_edges": len(compact["failed_edges"]),
                "eval_cache_hit": bool(cache_hit),
                "phase_cycle": int(self.phase_cycle),
                "padding": int(self.padding),
                "max_same_phase": int(self.max_same_phase),
                "eval_calls_episode": int(self.eval_calls_episode),
                "eval_cache_hits_episode": int(self.eval_cache_hits_episode),
                "eval_calls_total": int(self.eval_calls_total),
                "eval_cache_hits_total": int(self.eval_cache_hits_total),
            }
        )
        return self._build_observation(), reward, done, info


class PolicyValueNet(nn.Module):
    def __init__(self, obs_dim, action_dim, hidden_dim):
        super().__init__()
        self.trunk = nn.Sequential(
            nn.Linear(obs_dim, hidden_dim),
            nn.LayerNorm(hidden_dim),
            nn.GELU(),
            nn.Linear(hidden_dim, hidden_dim),
            nn.GELU(),
        )
        self.policy_head = nn.Linear(hidden_dim, action_dim)
        self.value_head = nn.Linear(hidden_dim, 1)

    def forward(self, obs):
        hidden = self.trunk(obs)
        logits = self.policy_head(hidden)
        value = self.value_head(hidden).squeeze(-1)
        return logits, value


def masked_categorical(logits, action_mask):
    if action_mask.ndim == 1:
        action_mask = action_mask.unsqueeze(0)
    if logits.ndim == 1:
        logits = logits.unsqueeze(0)
    valid_rows = torch.any(action_mask, dim=-1, keepdim=True)
    if not torch.all(valid_rows):
        fallback_mask = torch.zeros_like(action_mask, dtype=torch.bool)
        fallback_mask[..., 0] = True
        action_mask = torch.where(valid_rows, action_mask, fallback_mask)
    invalid_mask = ~action_mask
    masked_logits = torch.nan_to_num(logits, nan=0.0, posinf=1e6, neginf=-1e6)
    masked_logits = masked_logits.masked_fill(invalid_mask, -1e9)
    return Categorical(logits=masked_logits)


def compute_gae(rewards, values, dones, gamma, gae_lambda):
    advantages = np.zeros_like(rewards, dtype=np.float32)
    gae = 0.0
    next_value = 0.0
    for idx in reversed(range(len(rewards))):
        non_terminal = 1.0 - float(dones[idx])
        delta = rewards[idx] + gamma * next_value * non_terminal - values[idx]
        gae = delta + gamma * gae_lambda * non_terminal * gae
        advantages[idx] = gae
        next_value = values[idx]
    returns = advantages + values
    return advantages, returns


def select_action(model, obs, action_mask, device, action_prior_logits=None):
    obs_tensor = torch.as_tensor(obs, dtype=torch.float32, device=device).unsqueeze(0)
    mask_tensor = torch.as_tensor(action_mask, dtype=torch.bool, device=device).unsqueeze(0)
    with torch.no_grad():
        logits, value = model(obs_tensor)
        logits = torch.nan_to_num(logits, nan=0.0, posinf=1e6, neginf=-1e6)
        if action_prior_logits is not None:
            prior_tensor = torch.as_tensor(
                action_prior_logits,
                dtype=torch.float32,
                device=device,
            ).unsqueeze(0)
            logits = logits + prior_tensor
        value = torch.nan_to_num(value, nan=0.0, posinf=1e6, neginf=-1e6)
        dist = masked_categorical(logits, mask_tensor)
        action = dist.sample()
        logprob = dist.log_prob(action)
    return (
        int(action.item()),
        float(logprob.item()),
        float(value.item()),
    )


def ppo_update(model, optimizer, transitions, args, device, action_prior_logits=None):
    obs_batch = torch.as_tensor(
        np.stack([transition.obs for transition in transitions]),
        dtype=torch.float32,
        device=device,
    )
    action_batch = torch.as_tensor(
        [transition.action for transition in transitions],
        dtype=torch.long,
        device=device,
    )
    old_logprob_batch = torch.as_tensor(
        [transition.logprob for transition in transitions],
        dtype=torch.float32,
        device=device,
    )
    reward_batch = np.asarray([transition.reward for transition in transitions], dtype=np.float32)
    value_batch = np.asarray([transition.value for transition in transitions], dtype=np.float32)
    done_batch = np.asarray([transition.done for transition in transitions], dtype=np.float32)
    action_mask_batch = torch.as_tensor(
        np.stack([transition.action_mask for transition in transitions]),
        dtype=torch.bool,
        device=device,
    )

    advantages, returns = compute_gae(
        reward_batch,
        value_batch,
        done_batch,
        args.gamma,
        args.gae_lambda,
    )
    advantages = (advantages - advantages.mean()) / max(1e-6, advantages.std())
    advantage_batch = torch.as_tensor(advantages, dtype=torch.float32, device=device)
    return_batch = torch.as_tensor(returns, dtype=torch.float32, device=device)

    batch_size = obs_batch.shape[0]
    minibatch_size = min(args.minibatch_size, batch_size)
    permutation = np.arange(batch_size)
    policy_losses = []
    value_losses = []
    entropies = []
    approx_kls = []
    clipfracs = []
    prior_tensor = None
    if action_prior_logits is not None:
        prior_tensor = torch.as_tensor(action_prior_logits, dtype=torch.float32, device=device).unsqueeze(0)

    for _epoch in range(args.ppo_epochs):
        np.random.shuffle(permutation)
        for start in range(0, batch_size, minibatch_size):
            batch_indices = permutation[start : start + minibatch_size]
            batch_indices = torch.as_tensor(batch_indices, dtype=torch.long, device=device)

            logits, values = model(obs_batch[batch_indices])
            logits = torch.nan_to_num(logits, nan=0.0, posinf=1e6, neginf=-1e6)
            if prior_tensor is not None:
                logits = logits + prior_tensor
            values = torch.nan_to_num(values, nan=0.0, posinf=1e6, neginf=-1e6)
            dist = masked_categorical(logits, action_mask_batch[batch_indices])
            entropy = dist.entropy().mean()
            new_logprob = dist.log_prob(action_batch[batch_indices])
            new_logprob = torch.nan_to_num(new_logprob, nan=0.0, posinf=1e6, neginf=-1e6)
            ratios = torch.exp(new_logprob - old_logprob_batch[batch_indices])
            ratios = torch.nan_to_num(ratios, nan=1.0, posinf=10.0, neginf=0.0)

            unclipped = ratios * advantage_batch[batch_indices]
            clipped = torch.clamp(ratios, 1.0 - args.clip_eps, 1.0 + args.clip_eps) * advantage_batch[batch_indices]
            policy_loss = -torch.min(unclipped, clipped).mean()
            value_loss = 0.5 * (return_batch[batch_indices] - values).pow(2).mean()
            loss = policy_loss + args.value_coef * value_loss - args.entropy_coef * entropy
            approx_kl = (old_logprob_batch[batch_indices] - new_logprob).mean()
            clipfrac = ((ratios - 1.0).abs() > args.clip_eps).float().mean()
            if not torch.isfinite(loss):
                continue

            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), args.max_grad_norm)
            optimizer.step()

            policy_losses.append(float(policy_loss.item()))
            value_losses.append(float(value_loss.item()))
            entropies.append(float(entropy.item()))
            approx_kls.append(float(approx_kl.item()))
            clipfracs.append(float(clipfrac.item()))

    return {
        "policy_loss_mean": float(np.mean(policy_losses)) if policy_losses else 0.0,
        "value_loss_mean": float(np.mean(value_losses)) if value_losses else 0.0,
        "entropy_mean": float(np.mean(entropies)) if entropies else 0.0,
        "approx_kl_mean": float(np.mean(approx_kls)) if approx_kls else 0.0,
        "clipfrac_mean": float(np.mean(clipfracs)) if clipfracs else 0.0,
        "update_steps": len(policy_losses),
    }


def export_layout_artifacts(
    circuit,
    ordered_layers,
    best_result,
    best_positions,
    output_dir,
    run_time_sec,
    artifact_stem,
    benchmark_label,
    phase_cycle,
):
    os.makedirs(output_dir, exist_ok=True)
    file_stem = f"{artifact_stem}_rl_layout"
    x_spacing, y_spacing = summarize_node_position_spacing(best_positions)

    layout_result = LayoutOnlyResult(
        circuit,
        best_result["board"],
        best_positions,
        best_result["width"],
        best_result["height"],
        best_result["routed_paths"],
        best_result["failed_edges"],
        x_spacing,
        y_spacing,
        "ppo-rl",
        run_time_sec,
    )

    preserve_unassigned_export_phases(best_result["board"], phase_cycle)
    iFCN_Lab.MapChessboard.outputTexFile(best_result["board"], file_stem, output_dir)
    visualize_layered_graph_sorted(
        circuit,
        build_layer_dict(circuit.layer_nodes, sort_each_layer=True),
        circuit.effective_edges,
        benchmark_label,
        output_dir,
        file_suffix="_original_layers",
        title="Original Layering",
        verbose=False,
    )
    visualize_layered_graph_sorted(
        circuit,
        build_layer_dict(ordered_layers),
        circuit.effective_edges,
        benchmark_label,
        output_dir,
        file_suffix="_reordered_layers",
        title="GCN + Barycenter Reordered Layering",
        verbose=False,
    )
    visualize_layered_graph_sorted(
        circuit,
        build_layer_dict(ordered_layers),
        circuit.effective_edges,
        benchmark_label,
        output_dir,
        node_positions=best_positions,
        file_suffix="_rl_layout",
        title="PPO Refined Layout",
        verbose=False,
    )
    generate_gate_level_mapping_file(
        layout_result,
        output_dir=output_dir,
        filename_stem=file_stem,
        phase_cycle=phase_cycle,
        verbose=False,
    )


def write_training_history(output_dir, circuit_name, rows):
    stem = os.path.splitext(circuit_name)[0]
    csv_path = os.path.join(output_dir, f"{stem}_rl_training.csv")
    fieldnames = list(rows[0].keys()) if rows else []
    return write_csv_rows(csv_path, rows, fieldnames=fieldnames)


def write_csv_rows(output_path, rows, fieldnames=None):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    if fieldnames is None:
        fieldnames = list(rows[0].keys()) if rows else []
    with open(output_path, "w", newline="", encoding="utf-8") as csv_file:
        if not fieldnames:
            csv_file.write("")
            return output_path
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return output_path


def write_json_file(output_path, payload):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as json_file:
        json.dump(payload, json_file, ensure_ascii=False, indent=2)
    return output_path


def compute_series_slope(values):
    values = np.asarray(values, dtype=float)
    if values.size < 2:
        return 0.0
    x = np.arange(values.size, dtype=float)
    return float(np.polyfit(x, values, deg=1)[0])


def build_parser_safe_verilog(source_path, output_dir):
    with open(source_path, "r", encoding="utf-8") as source_file:
        lines = source_file.readlines()

    module_lines = []
    input_lines = []
    output_lines = []
    wire_lines = []
    assign_lines = []
    other_lines = []
    saw_endmodule = False

    for raw_line in lines:
        stripped = raw_line.strip()
        if not stripped:
            continue
        if stripped.startswith("module "):
            module_lines.append(stripped)
        elif stripped.startswith("input "):
            input_lines.append(stripped)
        elif stripped.startswith("output "):
            output_lines.append(stripped)
        elif stripped.startswith("wire "):
            wire_lines.append(stripped)
        elif stripped.startswith("assign "):
            assign_lines.append(stripped)
        elif stripped == "endmodule":
            saw_endmodule = True
        else:
            other_lines.append(stripped)

    if not module_lines or not input_lines or not output_lines or not assign_lines or other_lines:
        raise ValueError("Unsupported Verilog shape for parser-safe fallback.")

    temp_wires = []
    temp_counter = [0]

    def new_temp(prefix):
        temp_counter[0] += 1
        wire_name = f"ps{temp_counter[0]}"
        temp_wires.append(wire_name)
        return wire_name

    def add_assign(lhs, expr, statements):
        statements.append((lhs, expr))

    def lower_expr(node, prefix, statements):
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return str(int(node.value))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            operand_ref = lower_expr(node.operand, prefix, statements)
            if operand_ref.startswith("~"):
                temp_name = new_temp(prefix)
                add_assign(temp_name, operand_ref, statements)
                return temp_name
            return f"~{operand_ref}"
        if isinstance(node, ast.BinOp):
            left_ref = lower_expr(node.left, prefix, statements)
            right_ref = lower_expr(node.right, prefix, statements)
            if isinstance(node.op, ast.BitAnd):
                temp_name = new_temp(prefix)
                add_assign(temp_name, f"{left_ref} & {right_ref}", statements)
                return temp_name
            if isinstance(node.op, ast.BitOr):
                temp_name = new_temp(prefix)
                add_assign(temp_name, f"{left_ref} | {right_ref}", statements)
                return temp_name
            if isinstance(node.op, ast.BitXor):
                or_name = new_temp(prefix)
                and_name = new_temp(prefix)
                not_name = new_temp(prefix)
                xor_name = new_temp(prefix)
                add_assign(or_name, f"{left_ref} | {right_ref}", statements)
                add_assign(and_name, f"{left_ref} & {right_ref}", statements)
                add_assign(not_name, f"~{and_name}", statements)
                add_assign(xor_name, f"{or_name} & {not_name}", statements)
                return xor_name
        raise ValueError(f"Unsupported expression in parser-safe fallback: {ast.dump(node)}")

    def topo_sort_assigns(assign_pairs):
        assign_order = [lhs for lhs, _ in assign_pairs]
        assign_expr = {lhs: expr for lhs, expr in assign_pairs}
        defined_names = set(assign_expr)
        dep_map = {}
        dependents = {lhs: set() for lhs in assign_order}
        remaining_deps = {}

        for lhs, expr in assign_pairs:
            refs = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expr))
            refs.discard(lhs)
            deps = {ref for ref in refs if ref in defined_names}
            dep_map[lhs] = deps
            remaining_deps[lhs] = len(deps)
            for dep in deps:
                dependents.setdefault(dep, set()).add(lhs)

        ready = [lhs for lhs in assign_order if remaining_deps[lhs] == 0]
        sorted_pairs = []

        while ready:
            lhs = ready.pop(0)
            sorted_pairs.append((lhs, assign_expr[lhs]))
            for dependent in sorted(dependents.get(lhs, ())):
                remaining_deps[dependent] -= 1
                if remaining_deps[dependent] == 0:
                    ready.append(dependent)

        if len(sorted_pairs) != len(assign_pairs):
            return assign_pairs
        return sorted_pairs

    lowered_assigns = []
    for assign_line in assign_lines:
        body = assign_line[len("assign "):].rstrip(";").strip()
        lhs, expr = body.split("=", 1)
        lhs = lhs.strip()
        expr = expr.strip()
        parsed_expr = ast.parse(expr, mode="eval")
        lowered_ref = lower_expr(parsed_expr.body, lhs, lowered_assigns)
        add_assign(lhs, lowered_ref, lowered_assigns)

    lowered_assigns = topo_sort_assigns(lowered_assigns)
    lowered_assign_lines = [f"assign {lhs} = {expr};" for lhs, expr in lowered_assigns]

    output_lines_clean = output_lines[:]
    if temp_wires:
        output_lines_clean = output_lines_clean + [f"wire {', '.join(temp_wires)};"]

    stem = os.path.splitext(os.path.basename(source_path))[0]
    parser_safe_path = os.path.join(output_dir, f"{stem}_parser_safe.v")
    rendered_lines = module_lines + input_lines + output_lines_clean + wire_lines + lowered_assign_lines
    if saw_endmodule:
        rendered_lines.append("endmodule")
    with open(parser_safe_path, "w", encoding="utf-8") as output_file:
        output_file.write("\n".join(rendered_lines) + "\n")
    return parser_safe_path


def load_circuit_with_fallback(benchmark_path, output_dir, parse_mode="auto"):
    try:
        return CircuitParser(benchmark_path, parse_mode=parse_mode), benchmark_path, False
    except RuntimeError as exc:
        if "Invalid vertex name provided for edge creation." not in str(exc):
            raise
        parser_safe_path = build_parser_safe_verilog(benchmark_path, output_dir)
        print(
            "[Parser] Rewrote unsupported benchmark to parser-safe AOIG form: "
            f"{parser_safe_path}"
        )
        return CircuitParser(parser_safe_path, parse_mode=parse_mode), parser_safe_path, True


def command_with_parse_mode(parse_mode):
    argv = list(sys.argv[1:])
    replaced = False
    for idx, token in enumerate(argv):
        if token == "--parse-mode" and idx + 1 < len(argv):
            argv[idx + 1] = parse_mode
            replaced = True
            break
        if token.startswith("--parse-mode="):
            argv[idx] = f"--parse-mode={parse_mode}"
            replaced = True
            break
    if not replaced:
        argv.extend(["--parse-mode", parse_mode])
    return [sys.executable, os.path.abspath(__file__), *argv]


def auto_layered_switch_reasons(args, circuit, warm_start, probe_stats, resolved_train_eval_mode):
    if str(args.parse_mode) != "auto":
        return []
    if str(getattr(circuit, "parse_mode_resolved", "compact")) != "compact":
        return []
    if bool(args.memory_only_inference):
        return []

    reasons = []
    effective_nodes = int(getattr(circuit, "effective_nodes_num", 0))
    effective_edges = int(getattr(circuit, "effective_edges_num", 0))
    node_pressure = (
        effective_nodes >= int(args.auto_layered_node_threshold) or
        effective_edges >= int(args.auto_layered_edge_threshold)
    )
    node_pressure_text = (
        "node/edge pressure "
        f"(nodes={effective_nodes}, edges={effective_edges}, "
        f"thresholds={args.auto_layered_node_threshold}/{args.auto_layered_edge_threshold})"
    )

    failed_edges = int(len(warm_start.get("failed_edges", []))) if warm_start else effective_edges
    if failed_edges > 0:
        reasons.append(f"best compact warm start still has failed_edges={failed_edges}")

    if resolved_train_eval_mode == "placement" and node_pressure:
        reasons.append(
            f"{node_pressure_text}; placement-only training was selected before exact success is known"
        )

    if probe_stats:
        candidate_count = int(probe_stats.get("candidate_count", 0))
        legal_count = int(probe_stats.get("legal_count", 0))
        timeout_count = int(probe_stats.get("timeout_count", 0))
        success_rate = float(legal_count) / float(candidate_count) if candidate_count else 0.0
        min_success = float(np.clip(args.auto_layered_min_success_rate, 0.0, 1.0))
        if candidate_count and success_rate < min_success:
            reasons.append(
                "compact routing success rate too low "
                f"({legal_count}/{candidate_count}={success_rate:.2f}, min={min_success:.2f})"
            )
        if timeout_count > 0:
            reasons.append(f"compact exact-routing probe timed out for {timeout_count} candidate(s)")

        max_probe_sec = float(args.auto_layered_max_probe_sec)
        if max_probe_sec <= 0.0:
            max_probe_sec = max(8.0, float(args.exact_eval_timeout_sec) * 0.75)
        total_eval_sec = float(probe_stats.get("total_eval_sec", 0.0))
        if total_eval_sec > max_probe_sec and (node_pressure or legal_count == 0):
            reasons.append(
                "compact probe runtime too long "
                f"({total_eval_sec:.1f}s > {max_probe_sec:.1f}s)"
            )

    return reasons


def build_layout_snapshot(result):
    snapshot = {
        "layout_strategy": str(result["layout_strategy"]),
        "layout_orientation": str(result["layout_orientation"]),
        "x_spacing": result["x_spacing"],
        "y_spacing": result["y_spacing"],
        "width": int(result["width"]),
        "height": int(result["height"]),
        "area": float(result["area"]),
        "failed_edge_count": int(len(result["failed_edges"])),
        "failed_edges": [[int(src), int(dst)] for src, dst in result["failed_edges"]],
        "direction_violation_count": int(result["direction_violation_count"]),
        "route_overhang_penalty": int(result["route_overhang_penalty"]),
        "io_exposure_penalty": int(result["io_exposure_penalty"]),
    }
    if "node_positions" in result:
        snapshot["node_positions"] = [
            {
                "node_id": int(node_id),
                "x": int(coord[0]),
                "y": int(coord[1]),
            }
            for node_id, coord in sorted(result["node_positions"].items())
        ]
    return snapshot


def build_candidate_pool_snapshot(candidate_pool):
    snapshot = []
    for item in candidate_pool:
        snapshot.append(
            {
                "label": str(item["label"]),
                "clock_context": [int(value) for value in item["clock_context"]],
                "preferred_embedding_guidance": bool(item.get("preferred_embedding_guidance", False)),
                "cost": float(item["cost"]),
                "compact": {
                    "width": int(item["compact"]["width"]),
                    "height": int(item["compact"]["height"]),
                    "area": float(item["compact"]["area"]),
                    "failed_edges": int(len(item["compact"]["failed_edges"])),
                    "direction_violation_count": int(item["compact"]["direction_violation_count"]),
                    "route_overhang_penalty": int(item["compact"]["route_overhang_penalty"]),
                    "io_exposure_penalty": int(item["compact"]["io_exposure_penalty"]),
                },
                "node_positions": [
                    {
                        "node_id": int(node_id),
                        "x": int(coord[0]),
                        "y": int(coord[1]),
                    }
                    for node_id, coord in sorted(item["positions"].items())
                ],
            }
        )
    return snapshot


def build_run_config(
    args,
    circuit,
    benchmark_path,
    resolved_benchmark_path,
    output_dir,
    device,
    memory_only,
    parser_safe_generated,
    warm_start,
    crossings_per_layer,
):
    cuda_device_name = None
    if device.type == "cuda" and torch.cuda.is_available():
        cuda_device_name = torch.cuda.get_device_name(device)

    return {
        "benchmark": os.path.abspath(benchmark_path),
        "resolved_benchmark": os.path.abspath(resolved_benchmark_path),
        "parser_safe_generated": bool(parser_safe_generated),
        "benchmark_filename": circuit.fileName,
        "output_dir": os.path.abspath(output_dir),
        "seed": int(args.seed),
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "python_version": platform.python_version(),
        "platform": platform.platform(),
        "torch_version": torch.__version__,
        "cuda_available": bool(torch.cuda.is_available()),
        "cuda_device_name": cuda_device_name,
        "device_requested": args.device,
        "device_resolved": str(device),
        "parse_mode_requested": str(args.parse_mode),
        "parse_mode_resolved": str(getattr(circuit, "parse_mode_resolved", args.parse_mode)),
        "parse_cache_key": str(getattr(circuit, "parse_cache_key", "")),
        "start_layout_strategy": args.start_layout_strategy,
        "start_layout_orientation": args.start_layout_orientation,
        "x_spacing": int(args.x_spacing),
        "y_spacing": int(args.y_spacing),
        "phase_cycle": int(args.phase_cycle),
        "padding": int(args.padding),
        "max_same_phase": int(args.max_same_phase),
        "clock_domain_randomization": bool(args.clock_domain_randomization),
        "phase_cycle_range": list(
            resolve_int_range(args.phase_cycle_min, args.phase_cycle_max, args.phase_cycle, lower_bound=2)
        ),
        "padding_range": list(
            resolve_int_range(args.padding_min, args.padding_max, args.padding, lower_bound=0)
        ),
        "max_same_phase_range": list(
            resolve_int_range(
                args.max_same_phase_min,
                args.max_same_phase_max,
                args.max_same_phase,
                lower_bound=0,
            )
        ),
        "clock_random_seed": int(args.clock_random_seed) if args.clock_random_seed is not None else int(args.seed),
        "board_margin": int(args.board_margin) if args.board_margin is not None else None,
        "single_start_candidate": bool(args.single_start_candidate),
        "local_refine_rounds": int(args.local_refine_rounds),
        "local_lookahead_depth": int(args.local_lookahead_depth),
        "local_beam_width": int(args.local_beam_width),
        "local_branch_width": int(args.local_branch_width),
        "local_max_evaluations": int(args.local_max_evaluations),
        "post_primary_pack_rounds": int(args.post_primary_pack_rounds),
        "post_area_pack_rounds": int(args.post_area_pack_rounds),
        "post_pack_max_evaluations": int(args.post_pack_max_evaluations),
        "post_phase_strip_pack_rounds": int(args.post_phase_strip_pack_rounds),
        "post_phase_strip_pack_max_evaluations": int(args.post_phase_strip_pack_max_evaluations),
        "disable_gcn_cache": bool(args.disable_gcn_cache),
        "disable_layout_memory": bool(args.disable_layout_memory),
        "memory_only_inference": bool(args.memory_only_inference),
        "memory_only_warm_start": bool(memory_only),
        "episodes": int(args.episodes),
        "steps_per_episode": int(args.steps_per_episode),
        "ppo_epochs": int(args.ppo_epochs),
        "minibatch_size": int(args.minibatch_size),
        "hidden_dim": int(args.hidden_dim),
        "learning_rate": float(args.learning_rate),
        "gamma": float(args.gamma),
        "gae_lambda": float(args.gae_lambda),
        "clip_eps": float(args.clip_eps),
        "entropy_coef": float(args.entropy_coef),
        "value_coef": float(args.value_coef),
        "max_grad_norm": float(args.max_grad_norm),
        "disable_step_log": bool(args.disable_step_log),
        "disable_training_plots": bool(args.disable_training_plots),
        "disable_secondary_squeeze": bool(args.disable_secondary_squeeze),
        "rollback_worse_actions": bool(args.rollback_worse_actions),
        "elite_start_probability": float(np.clip(args.elite_start_probability, 0.0, 1.0)),
        "log_interval": int(args.log_interval),
        "early_stop_patience": int(args.early_stop_patience),
        "early_stop_min_episodes": int(args.early_stop_min_episodes),
        "aspect_ratio_limit": float(args.aspect_ratio_limit),
        "aspect_ratio_weight": float(args.aspect_ratio_weight),
        "max_span_weight": float(args.max_span_weight),
        "area_regression_weight": float(args.area_regression_weight),
        "best_selection_mode": str(args.best_selection_mode),
        "area_reward_weight": float(args.area_reward_weight),
        "train_eval_mode_requested": str(args.train_eval_mode),
        "fast_eval_node_threshold": int(args.fast_eval_node_threshold),
        "final_exact_validation": bool(args.final_exact_validation),
        "exact_validation_interval": int(args.exact_validation_interval),
        "placement_candidate_pool_size": int(args.placement_candidate_pool_size),
        "final_exact_validation_candidates": int(args.final_exact_validation_candidates),
        "exact_eval_timeout_sec": int(args.exact_eval_timeout_sec),
        "require_legal_final": bool(args.require_legal_final),
        "legal_repair_candidates": int(args.legal_repair_candidates),
        "legal_repair_timeout_multiplier": float(args.legal_repair_timeout_multiplier),
        "legal_repair_max_padding": int(args.legal_repair_max_padding),
        "auto_layered_node_threshold": int(args.auto_layered_node_threshold),
        "auto_layered_edge_threshold": int(args.auto_layered_edge_threshold),
        "auto_layered_min_success_rate": float(args.auto_layered_min_success_rate),
        "auto_layered_max_probe_sec": float(args.auto_layered_max_probe_sec),
        "strict_memory_updates": bool(args.strict_memory_updates),
        "disable_rl_experience": bool(args.disable_rl_experience),
        "rl_experience_path": os.path.abspath(args.rl_experience_path),
        "experience_prior_scale": float(args.experience_prior_scale),
        "gcn_cache_path": get_gcn_cache_path(resolved_benchmark_path, args.seed),
        "layout_memory_path": get_layout_memory_path(resolved_benchmark_path, args.seed),
        "crossings_after_gcn_barycenter_total": int(sum(crossings_per_layer.values())),
        "crossings_after_gcn_barycenter_per_layer": {
            str(layer_idx): int(count) for layer_idx, count in crossings_per_layer.items()
        },
        "warm_start": build_layout_snapshot(warm_start),
    }


def build_action_histogram(step_rows, top_k=16):
    action_counter = Counter(str(row["action_label"]) for row in step_rows)
    total_actions = sum(action_counter.values())
    histogram = []
    for action_label, count in action_counter.most_common(top_k):
        histogram.append(
            {
                "action_label": action_label,
                "count": int(count),
                "ratio": (float(count) / float(total_actions)) if total_actions else 0.0,
            }
        )
    return histogram


def build_training_summary(
    args,
    circuit,
    benchmark_path,
    resolved_benchmark_path,
    output_dir,
    device,
    env,
    warm_start,
    best_result,
    best_compact,
    global_best_cost,
    memory_only,
    parser_safe_generated,
    crossings_per_layer,
    episode_rows,
    step_rows,
    run_time_sec,
):
    rewards = np.asarray([float(row["episode_reward"]) for row in episode_rows], dtype=float)
    best_costs = np.asarray([float(row["best_cost"]) for row in episode_rows], dtype=float)
    best_areas = np.asarray([float(row["best_area"]) for row in episode_rows], dtype=float)
    invalid_rates = np.asarray([float(row["invalid_action_rate"]) for row in episode_rows], dtype=float)
    rollback_rates = np.asarray(
        [float(row.get("rollback_action_rate", 0.0)) for row in episode_rows],
        dtype=float,
    )
    total_invalid_actions = int(sum(int(row["invalid_action_count"]) for row in episode_rows))
    total_rollback_actions = int(sum(int(row.get("rollback_action_count", 0)) for row in episode_rows))
    total_accepted_actions = int(sum(int(row.get("accepted_action_count", row["steps"])) for row in episode_rows))
    total_elite_start_episodes = int(sum(int(row.get("elite_start", 0)) for row in episode_rows))
    total_steps = int(sum(int(row["steps"]) for row in episode_rows))
    reward_window = min(10, len(rewards)) if len(rewards) else 0
    reward_tail = rewards[-reward_window:] if reward_window else np.asarray([], dtype=float)
    cost_tail = best_costs[-reward_window:] if reward_window else np.asarray([], dtype=float)
    area_tail = best_areas[-reward_window:] if reward_window else np.asarray([], dtype=float)

    if len(best_costs) and args.best_selection_mode == "legal-area":
        final_area = float(best_compact["area"])
        final_width = int(best_compact["width"])
        final_height = int(best_compact["height"])
        best_episode = 0
        for row in episode_rows:
            if (
                abs(float(row["best_area"]) - final_area) <= 1e-6 and
                int(row["best_width"]) == final_width and
                int(row["best_height"]) == final_height
            ):
                best_episode = int(row["episode"])
                break
        if best_episode == 0:
            best_episode = int(np.argmin(best_areas) + 1)
    else:
        best_episode = int(np.argmin(best_costs) + 1) if len(best_costs) else 0
    episodes_since_last_best = int(len(best_costs) - best_episode) if best_episode else 0
    convergence_patience = max(5, args.episodes // 8)
    reward_last_std = float(np.std(reward_tail)) if reward_tail.size else 0.0
    reward_last_mean = float(np.mean(reward_tail)) if reward_tail.size else 0.0
    best_cost_slope_last = compute_series_slope(cost_tail)
    best_area_slope_last = compute_series_slope(area_tail)
    reward_slope_last = compute_series_slope(reward_tail)
    converged_by_patience = episodes_since_last_best >= convergence_patience
    reward_stable = reward_last_std <= max(0.05, 0.2 * max(1.0, abs(reward_last_mean)))
    converged_flag = bool(converged_by_patience and reward_stable and abs(best_cost_slope_last) <= 1e-6 + 0.01)

    warm_start_area = float(warm_start["area"])
    best_area = float(best_compact["area"])
    area_improvement_abs = warm_start_area - best_area
    area_improvement_ratio = (area_improvement_abs / warm_start_area) if warm_start_area > 0 else 0.0
    warm_start_cost = scalarize_layout_result(
        warm_start,
        aspect_ratio_limit=args.aspect_ratio_limit,
        aspect_ratio_weight=args.aspect_ratio_weight,
        max_span_weight=args.max_span_weight,
        area_reference=float(warm_start["area"]),
        area_regression_weight=args.area_regression_weight,
    )
    step_rewards = np.asarray([float(row["reward"]) for row in step_rows], dtype=float)
    reward_delta_clipped = np.asarray([float(row["reward_delta_clipped"]) for row in step_rows], dtype=float)
    reward_delta_raw = np.asarray([float(row["reward_delta_raw"]) for row in step_rows], dtype=float)
    policy_losses = np.asarray([float(row["policy_loss_mean"]) for row in episode_rows], dtype=float)
    value_losses = np.asarray([float(row["value_loss_mean"]) for row in episode_rows], dtype=float)
    entropies = np.asarray([float(row["entropy_mean"]) for row in episode_rows], dtype=float)
    approx_kls = np.asarray([float(row["approx_kl_mean"]) for row in episode_rows], dtype=float)
    clipfracs = np.asarray([float(row["clipfrac_mean"]) for row in episode_rows], dtype=float)
    eval_cache_hit_rates = np.asarray(
        [float(row["eval_cache_hit_rate_episode"]) for row in episode_rows],
        dtype=float,
    )
    action_histogram = build_action_histogram(step_rows)
    positive_reward_steps = int(sum(1 for row in step_rows if float(row["reward"]) > 0.0))
    improvement_bonus_steps = int(
        sum(1 for row in step_rows if float(row["reward_improvement_bonus"]) > 0.0)
    )
    total_reward_step_penalty = float(sum(float(row["reward_step_penalty"]) for row in step_rows))
    total_reward_invalid_penalty = float(sum(float(row["reward_invalid_penalty"]) for row in step_rows))
    total_reward_rollback_penalty = float(
        sum(float(row.get("reward_rollback_penalty", 0.0)) for row in step_rows)
    )
    total_reward_delta_clipped = float(sum(float(row["reward_delta_clipped"]) for row in step_rows))
    total_reward_area_delta_clipped = float(
        sum(float(row.get("reward_area_delta_clipped", 0.0)) for row in step_rows)
    )
    total_reward_improvement_bonus = float(
        sum(float(row["reward_improvement_bonus"]) for row in step_rows)
    )

    return {
        "benchmark": os.path.abspath(benchmark_path),
        "resolved_benchmark": os.path.abspath(resolved_benchmark_path),
        "output_dir": os.path.abspath(output_dir),
        "device": str(device),
        "seed": int(args.seed),
        "episodes": int(args.episodes),
        "steps_per_episode": int(args.steps_per_episode),
        "ppo_epochs": int(args.ppo_epochs),
        "minibatch_size": int(args.minibatch_size),
        "learning_rate": float(args.learning_rate),
        "gamma": float(args.gamma),
        "gae_lambda": float(args.gae_lambda),
        "clip_eps": float(args.clip_eps),
        "entropy_coef": float(args.entropy_coef),
        "value_coef": float(args.value_coef),
        "max_grad_norm": float(args.max_grad_norm),
        "hidden_dim": int(args.hidden_dim),
        "clock_domain_randomization": bool(args.clock_domain_randomization),
        "phase_cycle_range": list(
            resolve_int_range(args.phase_cycle_min, args.phase_cycle_max, args.phase_cycle, lower_bound=2)
        ),
        "padding_range": list(
            resolve_int_range(args.padding_min, args.padding_max, args.padding, lower_bound=0)
        ),
        "max_same_phase_range": list(
            resolve_int_range(
                args.max_same_phase_min,
                args.max_same_phase_max,
                args.max_same_phase,
                lower_bound=0,
            )
        ),
        "clock_random_seed": int(args.clock_random_seed) if args.clock_random_seed is not None else int(args.seed),
        "local_refine_rounds": int(args.local_refine_rounds),
        "local_lookahead_depth": int(args.local_lookahead_depth),
        "local_beam_width": int(args.local_beam_width),
        "local_branch_width": int(args.local_branch_width),
        "local_max_evaluations": int(args.local_max_evaluations),
        "post_primary_pack_rounds": int(args.post_primary_pack_rounds),
        "post_area_pack_rounds": int(args.post_area_pack_rounds),
        "post_pack_max_evaluations": int(args.post_pack_max_evaluations),
        "disable_gcn_cache": bool(args.disable_gcn_cache),
        "disable_layout_memory": bool(args.disable_layout_memory),
        "memory_only_inference": bool(args.memory_only_inference),
        "disable_step_log": bool(args.disable_step_log),
        "disable_training_plots": bool(args.disable_training_plots),
        "disable_secondary_squeeze": bool(args.disable_secondary_squeeze),
        "rollback_worse_actions": bool(args.rollback_worse_actions),
        "elite_start_probability": float(np.clip(args.elite_start_probability, 0.0, 1.0)),
        "aspect_ratio_limit": float(args.aspect_ratio_limit),
        "aspect_ratio_weight": float(args.aspect_ratio_weight),
        "max_span_weight": float(args.max_span_weight),
        "area_regression_weight": float(args.area_regression_weight),
        "best_selection_mode": str(args.best_selection_mode),
        "area_reward_weight": float(args.area_reward_weight),
        "train_eval_mode_requested": str(args.train_eval_mode),
        "train_eval_mode_resolved": str(getattr(env, "train_eval_mode", args.train_eval_mode)),
        "fast_eval_node_threshold": int(args.fast_eval_node_threshold),
        "final_exact_validation": bool(args.final_exact_validation),
        "exact_validation_interval": int(args.exact_validation_interval),
        "placement_candidate_pool_size": int(args.placement_candidate_pool_size),
        "final_exact_validation_candidates": int(args.final_exact_validation_candidates),
        "exact_eval_timeout_sec": int(args.exact_eval_timeout_sec),
        "require_legal_final": bool(args.require_legal_final),
        "legal_repair_candidates": int(args.legal_repair_candidates),
        "legal_repair_timeout_multiplier": float(args.legal_repair_timeout_multiplier),
        "legal_repair_max_padding": int(args.legal_repair_max_padding),
        "auto_layered_node_threshold": int(args.auto_layered_node_threshold),
        "auto_layered_edge_threshold": int(args.auto_layered_edge_threshold),
        "auto_layered_min_success_rate": float(args.auto_layered_min_success_rate),
        "auto_layered_max_probe_sec": float(args.auto_layered_max_probe_sec),
        "strict_memory_updates": bool(args.strict_memory_updates),
        "disable_rl_experience": bool(args.disable_rl_experience),
        "rl_experience_path": os.path.abspath(args.rl_experience_path),
        "experience_prior_scale": float(args.experience_prior_scale),
        "memory_only_warm_start": bool(memory_only),
        "parser_safe_generated": bool(parser_safe_generated),
        "gcn_cache_path": get_gcn_cache_path(resolved_benchmark_path, args.seed),
        "layout_memory_path": get_layout_memory_path(resolved_benchmark_path, args.seed),
        "module_name": circuit.moduleName,
        "parse_mode_requested": str(args.parse_mode),
        "parse_mode_resolved": str(getattr(circuit, "parse_mode_resolved", args.parse_mode)),
        "parse_cache_key": str(getattr(circuit, "parse_cache_key", "")),
        "original_node_count": int(circuit.originCircuitNodeNum),
        "original_edge_count": int(circuit.originCircuitEdgeNum),
        "effective_node_count": int(circuit.effective_nodes_num),
        "effective_edge_count": int(circuit.effective_edges_num),
        "differ_layer_route_pair_count": int(getattr(circuit, "differ_layer_route_pairs_num", 0)),
        "input_node_count": int(circuit.InputNodesNum),
        "output_node_count": int(circuit.OutputNodesNum),
        "total_layers": int(circuit.total_layers),
        "obs_dim": int(env.obs_dim),
        "action_dim": int(env.action_dim),
        "segment_action_count": int(
            sum(1 for action_def in env.action_defs if action_def[0] == "segment_shift")
        ),
        "layer_block_shift_action_count": int(
            sum(1 for action_def in env.action_defs if action_def[0] == "layer_block_shift")
        ),
        "eval_cache_entries": int(len(env.eval_cache)),
        "eval_calls_total": int(env.eval_calls_total),
        "eval_cache_hits_total": int(env.eval_cache_hits_total),
        "eval_cache_hit_rate": (
            float(env.eval_cache_hits_total) / float(env.eval_calls_total)
            if env.eval_calls_total else 0.0
        ),
        "crossings_after_gcn_barycenter_total": int(sum(crossings_per_layer.values())),
        "crossings_after_gcn_barycenter_per_layer": {
            str(layer_idx): int(count) for layer_idx, count in crossings_per_layer.items()
        },
        "training_runtime_sec": float(run_time_sec),
        "warm_start_strategy": warm_start["layout_strategy"],
        "warm_start_orientation": warm_start["layout_orientation"],
        "warm_start_width": int(warm_start["width"]),
        "warm_start_height": int(warm_start["height"]),
        "warm_start_area": warm_start_area,
        "warm_start_cost": float(warm_start_cost),
        "warm_start_failed_edges": int(len(warm_start["failed_edges"])),
        "best_strategy": best_result["layout_strategy"],
        "best_orientation": best_result["layout_orientation"],
        "best_width": int(best_compact["width"]),
        "best_height": int(best_compact["height"]),
        "best_area": best_area,
        "best_cost": float(global_best_cost),
        "best_failed_edges": int(len(best_compact["failed_edges"])),
        "best_direction_violation_count": int(best_compact["direction_violation_count"]),
        "best_route_overhang_penalty": int(best_compact["route_overhang_penalty"]),
        "best_io_exposure_penalty": int(best_compact["io_exposure_penalty"]),
        "area_improvement_abs": float(area_improvement_abs),
        "area_improvement_ratio": float(area_improvement_ratio),
        "total_steps": total_steps,
        "total_invalid_actions": total_invalid_actions,
        "invalid_action_rate_total": (float(total_invalid_actions) / float(total_steps) if total_steps else 0.0),
        "total_accepted_actions": total_accepted_actions,
        "accepted_action_rate_total": (
            float(total_accepted_actions) / float(total_steps) if total_steps else 0.0
        ),
        "total_rollback_actions": total_rollback_actions,
        "rollback_action_rate_total": (
            float(total_rollback_actions) / float(total_steps) if total_steps else 0.0
        ),
        "elite_start_episodes": total_elite_start_episodes,
        "elite_start_episode_rate": (
            float(total_elite_start_episodes) / float(len(episode_rows)) if episode_rows else 0.0
        ),
        "episode_reward_mean": float(np.mean(rewards)) if len(rewards) else 0.0,
        "episode_reward_std": float(np.std(rewards)) if len(rewards) else 0.0,
        "episode_reward_last_window_mean": reward_last_mean,
        "episode_reward_last_window_std": reward_last_std,
        "episode_reward_last_window_slope": reward_slope_last,
        "best_cost_last_window_slope": best_cost_slope_last,
        "best_area_last_window_slope": best_area_slope_last,
        "invalid_action_rate_mean": float(np.mean(invalid_rates)) if len(invalid_rates) else 0.0,
        "rollback_action_rate_mean": float(np.mean(rollback_rates)) if len(rollback_rates) else 0.0,
        "eval_cache_hit_rate_episode_mean": (
            float(np.mean(eval_cache_hit_rates)) if len(eval_cache_hit_rates) else 0.0
        ),
        "step_reward_mean": float(np.mean(step_rewards)) if len(step_rewards) else 0.0,
        "step_reward_std": float(np.std(step_rewards)) if len(step_rewards) else 0.0,
        "reward_delta_raw_mean": float(np.mean(reward_delta_raw)) if len(reward_delta_raw) else 0.0,
        "reward_delta_raw_std": float(np.std(reward_delta_raw)) if len(reward_delta_raw) else 0.0,
        "reward_delta_clipped_mean": (
            float(np.mean(reward_delta_clipped)) if len(reward_delta_clipped) else 0.0
        ),
        "reward_delta_clipped_std": (
            float(np.std(reward_delta_clipped)) if len(reward_delta_clipped) else 0.0
        ),
        "positive_reward_steps": positive_reward_steps,
        "positive_reward_step_rate": (
            float(positive_reward_steps) / float(total_steps) if total_steps else 0.0
        ),
        "improvement_bonus_steps": improvement_bonus_steps,
        "improvement_bonus_step_rate": (
            float(improvement_bonus_steps) / float(total_steps) if total_steps else 0.0
        ),
        "reward_step_penalty_total": total_reward_step_penalty,
        "reward_invalid_penalty_total": total_reward_invalid_penalty,
        "reward_rollback_penalty_total": total_reward_rollback_penalty,
        "reward_delta_clipped_total": total_reward_delta_clipped,
        "reward_area_delta_clipped_total": total_reward_area_delta_clipped,
        "reward_improvement_bonus_total": total_reward_improvement_bonus,
        "policy_loss_episode_mean": float(np.mean(policy_losses)) if len(policy_losses) else 0.0,
        "value_loss_episode_mean": float(np.mean(value_losses)) if len(value_losses) else 0.0,
        "entropy_episode_mean": float(np.mean(entropies)) if len(entropies) else 0.0,
        "approx_kl_episode_mean": float(np.mean(approx_kls)) if len(approx_kls) else 0.0,
        "clipfrac_episode_mean": float(np.mean(clipfracs)) if len(clipfracs) else 0.0,
        "best_episode": best_episode,
        "episodes_since_last_best": episodes_since_last_best,
        "convergence_patience": int(convergence_patience),
        "converged_by_patience": bool(converged_by_patience),
        "reward_stable_last_window": bool(reward_stable),
        "converged_flag": converged_flag,
        "top_actions": action_histogram,
    }


def plot_training_curves(output_dir, circuit_name, episode_rows):
    if not episode_rows:
        return None

    import matplotlib.pyplot as plt

    stem = os.path.splitext(circuit_name)[0]
    episodes = [int(row["episode"]) for row in episode_rows]
    rewards = [float(row["episode_reward"]) for row in episode_rows]
    best_costs = [float(row["best_cost"]) for row in episode_rows]
    best_areas = [float(row["best_area"]) for row in episode_rows]
    invalid_rates = [float(row["invalid_action_rate"]) for row in episode_rows]
    entropies = [float(row["entropy_mean"]) for row in episode_rows]
    approx_kls = [float(row["approx_kl_mean"]) for row in episode_rows]
    reward_array = np.asarray(rewards, dtype=float)
    window = min(10, len(reward_array))
    if window > 1:
        reward_mean = np.convolve(reward_array, np.ones(window) / window, mode="valid")
        reward_mean_episodes = episodes[window - 1 :]
    else:
        reward_mean = reward_array
        reward_mean_episodes = episodes
    best_rewards = np.maximum.accumulate(reward_array) if len(reward_array) else reward_array

    fig, axes = plt.subplots(6, 1, figsize=(9, 18), sharex=True)
    axes[0].plot(episodes, rewards, color="#93c5fd", linewidth=1.0, label="Episode reward")
    axes[0].plot(reward_mean_episodes, reward_mean, color="#2563eb", linewidth=1.8, label="Rolling mean")
    axes[0].plot(episodes, best_rewards, color="#16a34a", linewidth=1.3, label="Best reward so far")
    axes[0].set_ylabel("Episode reward")
    axes[0].legend(loc="best", fontsize=8)
    axes[0].grid(True, linestyle=":", alpha=0.5)

    axes[1].plot(episodes, best_costs, color="#dc2626", linewidth=1.4)
    axes[1].set_ylabel("Best cost")
    axes[1].grid(True, linestyle=":", alpha=0.5)

    axes[2].plot(episodes, best_areas, color="#059669", linewidth=1.4)
    axes[2].set_ylabel("Best area")
    axes[2].grid(True, linestyle=":", alpha=0.5)

    axes[3].plot(episodes, invalid_rates, color="#7c3aed", linewidth=1.4)
    axes[3].set_ylabel("Invalid rate")
    axes[3].grid(True, linestyle=":", alpha=0.5)

    axes[4].plot(episodes, entropies, color="#ea580c", linewidth=1.4)
    axes[4].set_ylabel("Entropy")
    axes[4].grid(True, linestyle=":", alpha=0.5)

    axes[5].plot(episodes, approx_kls, color="#0891b2", linewidth=1.4)
    axes[5].set_ylabel("Approx KL")
    axes[5].set_xlabel("Episode")
    axes[5].grid(True, linestyle=":", alpha=0.5)

    fig.suptitle(f"PPO Training Curves - {stem}")
    fig.tight_layout()
    output_path = os.path.join(output_dir, f"{stem}_rl_training_curves.svg")
    fig.savefig(output_path, bbox_inches="tight")
    plt.close(fig)
    return output_path


def main():
    args = parse_args()
    benchmark_path = os.path.abspath(args.benchmark)
    output_dir = os.path.abspath(args.output_dir)
    benchmark_label = os.path.basename(benchmark_path)
    output_stem = os.path.splitext(benchmark_label)[0]

    if not os.path.exists(benchmark_path):
        raise FileNotFoundError(f"Benchmark not found: {benchmark_path}")

    set_global_seed(args.seed)
    device = resolve_device(args.device)
    os.makedirs(output_dir, exist_ok=True)

    circuit, resolved_benchmark_path, parser_safe_generated = load_circuit_with_fallback(
        benchmark_path,
        output_dir,
        parse_mode=args.parse_mode,
    )
    memory_candidate = None
    if args.memory_only_inference and args.disable_layout_memory:
        raise RuntimeError("--memory-only-inference requires layout memory to be enabled.")
    if args.memory_only_inference:
        memory_candidate = load_layout_memory_candidate(circuit, resolved_benchmark_path, args.seed)
        if memory_candidate is None:
            raise RuntimeError(
                "No legal stored layout memory was found for this benchmark. "
                "Run normal GCN+RL once to populate memory first."
            )
        ordered_layers = normalize_layers(circuit.layer_nodes)
        embeddings = np.zeros((len(circuit.effective_nodes), 2), dtype=float)
        embedding_scores = {}
        crossings_per_layer = {
            int(layer_idx): 0 for layer_idx in range(max(0, len(ordered_layers) - 1))
        }
        print("[Memory] Memory-only inference enabled; skipping GCN training and PPO updates.")
    else:
        embeddings, barycenter_opt_layers, crossings_per_layer, _edges = load_or_generate_gcn_layout(
            circuit,
            resolved_benchmark_path,
            args.seed,
            use_cache=not args.disable_gcn_cache,
        )
        ordered_layers = normalize_layers(barycenter_opt_layers)
        embedding_scores = build_embedding_score_map(circuit, ordered_layers, embeddings)
        if not args.disable_layout_memory:
            memory_candidate = load_layout_memory_candidate(circuit, resolved_benchmark_path, args.seed)
    board_margin = args.board_margin if args.board_margin is not None else args.padding + 1
    experience_memory = {"version": 1, "actions": {}, "updates": []}
    experience_path = os.path.abspath(args.rl_experience_path)
    if not args.disable_rl_experience and not args.memory_only_inference:
        experience_memory = read_experience_memory(experience_path)
        print(
            "[RL-Memory] Loaded shared action experience: "
            f"path={experience_path}, "
            f"action_templates={len(experience_memory.get('actions', {}))}, "
            f"updates={len(experience_memory.get('updates', []))}"
        )
    memory_only = (
        memory_candidate is not None and
        (
            args.memory_only_inference or
            (
                args.start_layout_strategy == "auto" and
                args.start_layout_orientation == "auto"
            )
        )
    )
    effective_local_refine_rounds = 0 if memory_only else args.local_refine_rounds
    if memory_only:
        print("[Memory] Reusing stored layout as the only generated-candidate warm start.")
    allowed_strategies = (
        ()
        if memory_only else
        (None if args.start_layout_strategy == "auto" else (args.start_layout_strategy,))
    )
    allowed_orientations = (
        ()
        if memory_only else
        (None if args.start_layout_orientation == "auto" else (args.start_layout_orientation,))
    )
    resolved_train_eval_mode = args.train_eval_mode
    if resolved_train_eval_mode == "auto":
        resolved_train_eval_mode = (
            "placement"
            if int(circuit.effective_nodes_num) >= int(args.fast_eval_node_threshold)
            else "exact"
        )
    if args.memory_only_inference:
        resolved_train_eval_mode = "exact"
    if (
        bool(args.require_legal_final) and
        resolved_train_eval_mode == "placement" and
        not bool(args.final_exact_validation)
    ):
        print("[Strict] --require-legal-final forces final exact validation for placement training.")
        args.final_exact_validation = True
    warm_start_eval_stats = {}
    if memory_only:
        if resolved_train_eval_mode == "placement":
            print(
                "[Fast-Eval] Placement-only training evaluation enabled for stored memory layout: "
                f"nodes={circuit.effective_nodes_num}, threshold={args.fast_eval_node_threshold}. "
                "Final exact validation remains enabled unless disabled."
            )
            warm_start = evaluate_placement_only_candidate(memory_candidate, circuit)
        else:
            memory_candidates = [memory_candidate]
            if args.memory_only_inference:
                memory_candidates = load_layout_memory_candidates(circuit, resolved_benchmark_path, args.seed)
            warm_start = None
            best_partial_memory = None
            best_partial_key = None
            for candidate_idx, candidate in enumerate(memory_candidates, start=1):
                candidate_result = evaluate_layout_candidate_with_timeout(
                    candidate,
                    circuit,
                    args.phase_cycle,
                    args.padding,
                    args.max_same_phase,
                    embedding_scores=(
                        embedding_scores
                        if candidate.get("routing_embedding_guidance", False) else
                        None
                    ),
                    timeout_sec=args.exact_eval_timeout_sec,
                )
                candidate_cost = scalarize_layout_result(
                    candidate_result,
                    aspect_ratio_limit=args.aspect_ratio_limit,
                    aspect_ratio_weight=args.aspect_ratio_weight,
                    max_span_weight=args.max_span_weight,
                    area_reference=float(candidate_result["area"]),
                    area_regression_weight=args.area_regression_weight,
                )
                candidate_key = layout_selection_key(candidate_result, candidate_cost, args.best_selection_mode)
                if best_partial_key is None or candidate_key < best_partial_key:
                    best_partial_key = candidate_key
                    best_partial_memory = (candidate, candidate_result)
                if not args.memory_only_inference or int(len(candidate_result["failed_edges"])) == 0:
                    warm_start = candidate_result
                    memory_candidate = candidate
                    if args.memory_only_inference:
                        print(
                            "[Memory] Verified stored layout candidate under current phase settings: "
                            f"{candidate.get('memory_path', '')} "
                            f"area={candidate_result['area']:.1f} "
                            f"candidate={candidate_idx}/{len(memory_candidates)}"
                        )
                    break
                print(
                    "[Memory] Stored layout candidate is not legal under current phase settings; trying next: "
                    f"{candidate.get('memory_path', '')} failed_edges={len(candidate_result['failed_edges'])}"
                )
            if warm_start is None:
                if best_partial_memory is not None:
                    _candidate, partial_result = best_partial_memory
                    raise RuntimeError(
                        "Stored layout memory was found, but none routed legally under the current "
                        f"phase/padding settings. Best memory candidate had "
                        f"failed_edges={len(partial_result['failed_edges'])}, "
                        f"area={partial_result['area']}."
                    )
                raise RuntimeError("Stored layout memory was found, but no usable candidate could be evaluated.")
        warm_start["score"] = layout_selection_key(
            warm_start,
            scalarize_layout_result(
                warm_start,
                aspect_ratio_limit=args.aspect_ratio_limit,
                aspect_ratio_weight=args.aspect_ratio_weight,
                max_span_weight=args.max_span_weight,
                area_reference=float(warm_start["area"]),
                area_regression_weight=args.area_regression_weight,
            ),
            args.best_selection_mode,
        )
        print(
            "[Memory] Stored layout warm start selected: "
            f"strategy={warm_start['layout_strategy']} orientation={warm_start['layout_orientation']} "
            f"size={warm_start['width']}x{warm_start['height']} "
            f"area={warm_start['area']} failed_edges={len(warm_start['failed_edges'])}"
        )
    elif args.single_start_candidate:
        single_candidate = build_single_start_candidate(
            circuit=circuit,
            ordered_layers=ordered_layers,
            board_margin=board_margin,
            x_spacing=args.x_spacing,
            y_spacing=args.y_spacing,
            embeddings=embeddings,
            strategy=args.start_layout_strategy,
            orientation=args.start_layout_orientation,
        )
        single_eval_start = time.perf_counter()
        warm_start = (
            evaluate_placement_only_candidate(single_candidate, circuit)
            if resolved_train_eval_mode == "placement" else
            evaluate_layout_candidate_with_timeout(
                single_candidate,
                circuit,
                args.phase_cycle,
                args.padding,
                args.max_same_phase,
                embedding_scores=(
                    embedding_scores if single_candidate.get("routing_embedding_guidance", False) else None
                ),
                timeout_sec=args.exact_eval_timeout_sec,
            )
        )
        if resolved_train_eval_mode != "placement":
            warm_start["candidate_eval_sec"] = float(time.perf_counter() - single_eval_start)
        if resolved_train_eval_mode != "placement":
            warm_start_eval_stats = {
                "candidate_count": 1,
                "legal_count": 1 if int(len(warm_start.get("failed_edges", []))) == 0 else 0,
                "timeout_count": 1 if bool(warm_start.get("exact_evaluation_timeout", False)) else 0,
                "failed_edge_count_sum": int(len(warm_start.get("failed_edges", []))),
                "total_eval_sec": float(warm_start.get("candidate_eval_sec", 0.0)),
                "max_eval_sec": float(warm_start.get("candidate_eval_sec", 0.0)),
            }
        warm_start["score"] = layout_selection_key(
            warm_start,
            scalarize_layout_result(
                warm_start,
                aspect_ratio_limit=args.aspect_ratio_limit,
                aspect_ratio_weight=args.aspect_ratio_weight,
                max_span_weight=args.max_span_weight,
                area_reference=float(warm_start["area"]),
                area_regression_weight=args.area_regression_weight,
            ),
            args.best_selection_mode,
        )
        print(
            "[Start] Single start candidate selected: "
            f"strategy={warm_start['layout_strategy']} orientation={warm_start['layout_orientation']} "
            f"size={warm_start['width']}x{warm_start['height']} "
            f"area={warm_start['area']} failed_edges={len(warm_start['failed_edges'])}"
        )
    elif resolved_train_eval_mode == "placement":
        print(
            "[Fast-Eval] Placement-only training evaluation enabled: "
            f"nodes={circuit.effective_nodes_num}, threshold={args.fast_eval_node_threshold}. "
            "Final exact validation remains enabled unless disabled."
        )
        warm_start = select_fast_warm_start(
            circuit=circuit,
            ordered_layers=ordered_layers,
            board_margin=board_margin,
            base_x_spacing=args.x_spacing,
            base_y_spacing=args.y_spacing,
            embeddings=embeddings,
            embedding_scores=embedding_scores,
            allowed_strategies=allowed_strategies,
            allowed_orientations=allowed_orientations,
            best_selection_mode=args.best_selection_mode,
            aspect_ratio_limit=args.aspect_ratio_limit,
            aspect_ratio_weight=args.aspect_ratio_weight,
            max_span_weight=args.max_span_weight,
            area_regression_weight=args.area_regression_weight,
        )
    else:
        warm_start = select_best_layout(
            circuit,
            ordered_layers,
            board_margin,
            args.phase_cycle,
            args.padding,
            args.max_same_phase,
            args.x_spacing,
            args.y_spacing,
            embeddings=embeddings,
            embedding_scores=embedding_scores,
            allowed_strategies=allowed_strategies,
            allowed_orientations=allowed_orientations,
            local_refine_rounds=(
                0
                if args.parse_mode == "auto" and getattr(circuit, "parse_mode_resolved", "compact") == "compact"
                else effective_local_refine_rounds
            ),
            local_lookahead_depth=args.local_lookahead_depth,
            local_beam_width=args.local_beam_width,
            local_branch_width=args.local_branch_width,
            local_max_evaluations=args.local_max_evaluations,
            extra_candidates=([memory_candidate] if memory_candidate is not None else None),
            evaluation_fn=lambda candidate, circuit_obj, phase_cycle, padding, max_same_phase, embedding_scores=None: (
                evaluate_layout_candidate_with_timeout(
                    candidate,
                    circuit_obj,
                    phase_cycle,
                    padding,
                    max_same_phase,
                    embedding_scores=embedding_scores,
                    timeout_sec=args.exact_eval_timeout_sec,
                )
            ),
            evaluation_stats=warm_start_eval_stats,
        )
    auto_switch_reasons = auto_layered_switch_reasons(
        args,
        circuit,
        warm_start,
        warm_start_eval_stats,
        resolved_train_eval_mode,
    )
    if auto_switch_reasons:
        print("[Parser-Auto] Compact parsing looks poor for this run:")
        for reason in auto_switch_reasons:
            print(f"[Parser-Auto]   - {reason}")
        print("[Parser-Auto] Re-running the same job with layered parsing (addLayerRedundancyNode).")
        completed = subprocess.run(
            command_with_parse_mode("layered"),
            cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), "../../..")),
            text=True,
            check=False,
        )
        raise SystemExit(int(completed.returncode))
    if not args.disable_layout_memory and resolved_train_eval_mode != "placement":
        save_layout_memory(
            warm_start,
            resolved_benchmark_path,
            args.seed,
            allow_failed=not args.strict_memory_updates,
        )
    elif not args.disable_layout_memory:
        print("[Memory] Skip saving placement-only warm start before exact validation.")

    phase_cycle_range = resolve_int_range(
        args.phase_cycle_min,
        args.phase_cycle_max,
        args.phase_cycle,
        lower_bound=2,
    )
    padding_range = resolve_int_range(
        args.padding_min,
        args.padding_max,
        args.padding,
        lower_bound=0,
    )
    max_same_phase_range = resolve_int_range(
        args.max_same_phase_min,
        args.max_same_phase_max,
        args.max_same_phase,
        lower_bound=0,
    )

    env = LayoutCompactionEnv(
        circuit=circuit,
        ordered_layers=ordered_layers,
        embedding_scores=embedding_scores,
        start_result=warm_start,
        phase_cycle=args.phase_cycle,
        padding=args.padding,
        max_same_phase=args.max_same_phase,
        max_steps=args.steps_per_episode,
        enable_secondary_squeeze=not args.disable_secondary_squeeze,
        aspect_ratio_limit=args.aspect_ratio_limit,
        aspect_ratio_weight=args.aspect_ratio_weight,
        max_span_weight=args.max_span_weight,
        area_regression_weight=args.area_regression_weight,
        best_selection_mode=args.best_selection_mode,
        area_reward_weight=args.area_reward_weight,
        train_eval_mode=resolved_train_eval_mode,
        exact_eval_timeout_sec=args.exact_eval_timeout_sec,
        clock_domain_randomization=args.clock_domain_randomization,
        phase_cycle_range=phase_cycle_range,
        padding_range=padding_range,
        max_same_phase_range=max_same_phase_range,
        clock_random_seed=args.clock_random_seed if args.clock_random_seed is not None else args.seed,
        experience_memory=experience_memory,
        experience_prior_scale=args.experience_prior_scale,
        rollback_worse_actions=args.rollback_worse_actions,
    )
    if args.clock_domain_randomization:
        print(
            "[Clock-RL] Domain randomization enabled: "
            f"phase_cycle={phase_cycle_range}, "
            f"padding={padding_range}, "
            f"max_same_phase={max_same_phase_range}"
        )
    action_prior_logits = None if (args.disable_rl_experience or args.memory_only_inference) else env.get_action_prior_logits()
    if action_prior_logits is not None:
        nonzero_priors = int(np.count_nonzero(np.abs(action_prior_logits) > 1e-9))
        print(
            "[RL-Memory] Applying action prior logits: "
            f"nonzero={nonzero_priors}/{len(action_prior_logits)}, "
            f"min={float(np.min(action_prior_logits)):.3f}, "
            f"max={float(np.max(action_prior_logits)):.3f}"
        )
    model = PolicyValueNet(env.obs_dim, env.action_dim, args.hidden_dim).to(device)
    elite_start_probability = float(np.clip(args.elite_start_probability, 0.0, 1.0))
    optimizer = torch.optim.Adam(model.parameters(), lr=args.learning_rate)

    global_best_cost = env.initial_cost
    global_best_positions = clone_positions(env.initial_positions)
    global_best_result = warm_start
    global_best_compact = dict(env.initial_result)
    global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
    global_best_clock_context = (
        int(args.phase_cycle),
        int(args.padding),
        int(args.max_same_phase),
    )
    verified_best_result = None
    verified_best_compact = None
    verified_best_cost = None
    verified_best_positions = None
    verified_best_key = None
    strict_reference_result = warm_start
    strict_reference_compact = dict(env.initial_result)
    strict_reference_cost = float(env.initial_cost)
    strict_reference_positions = clone_positions(env.initial_positions)
    strict_reference_exact_payload = None
    strict_reference_exact_valid = False
    strict_reference_clock_context = (
        int(args.phase_cycle),
        int(args.padding),
        int(args.max_same_phase),
    )
    exact_validation_records = []
    exact_validation_signatures = set()
    placement_candidate_pool = []
    placement_candidate_signatures = set()
    history_rows = []
    step_rows = []
    run_config = build_run_config(
        args=args,
        circuit=circuit,
        benchmark_path=benchmark_path,
        resolved_benchmark_path=resolved_benchmark_path,
        output_dir=output_dir,
        device=device,
        memory_only=memory_only,
        parser_safe_generated=parser_safe_generated,
        warm_start=warm_start,
        crossings_per_layer=crossings_per_layer,
    )
    stem = output_stem
    config_path = write_json_file(os.path.join(output_dir, f"{stem}_rl_config.json"), run_config)
    if args.memory_only_inference:
        run_time_sec = 0.0
        export_layout_artifacts(
            circuit,
            ordered_layers,
            global_best_result,
            global_best_positions,
            output_dir,
            run_time_sec,
            artifact_stem=output_stem,
            benchmark_label=benchmark_label,
            phase_cycle=global_best_clock_context[0],
        )
        history_path = write_training_history(output_dir, benchmark_label, history_rows)
        summary = build_training_summary(
            args=args,
            circuit=circuit,
            benchmark_path=benchmark_path,
            resolved_benchmark_path=resolved_benchmark_path,
            output_dir=output_dir,
            device=device,
            env=env,
            warm_start=warm_start,
            best_result=global_best_result,
            best_compact=global_best_compact,
            global_best_cost=global_best_cost,
            memory_only=memory_only,
            parser_safe_generated=parser_safe_generated,
            crossings_per_layer=crossings_per_layer,
            episode_rows=history_rows,
            step_rows=step_rows,
            run_time_sec=run_time_sec,
        )
        summary["episodes_completed"] = 0
        summary["memory_only_inference"] = True
        summary["memory_candidate_path"] = str(memory_candidate.get("memory_path", ""))
        summary["train_eval_mode_resolved"] = "exact"
        summary["final_exact_validation"] = {
            "requested": False,
            "ran": False,
            "candidate_count": 1,
            "runtime_sec": 0.0,
            "failed_edges": int(len(global_best_compact["failed_edges"])),
            "area": float(global_best_compact["area"]),
        }
        summary["strict_success"] = bool(int(summary.get("best_failed_edges", 0)) == 0)
        summary["rl_experience_path"] = None
        summary["rl_experience_action_templates"] = 0
        summary["rl_experience_updates"] = 0
        summary["rl_experience_update_skipped_reason"] = "memory_only_inference"
        summary_path = write_json_file(os.path.join(output_dir, f"{stem}_rl_summary.json"), summary)
        warm_start_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_warm_start.json"),
            build_layout_snapshot(warm_start),
        )
        best_layout_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_best_layout.json"),
            build_layout_snapshot(global_best_result),
        )
        candidate_pool_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_candidate_pool.json"),
            [],
        )
        action_histogram_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_action_histogram.json"),
            [],
        )
        print("[Memory] Memory-only layout export completed.")
        print(f"[Memory] Candidate: {memory_candidate.get('memory_path', '')}")
        print(f"[RL] Benchmark: {benchmark_path}")
        if parser_safe_generated:
            print(f"[RL] Parser-safe benchmark used: {resolved_benchmark_path}")
        print(f"[RL] Device: {device}")
        print(
            "[RL] Best layout: "
            f"orientation={env.orientation}, "
            f"size={global_best_compact['width']}x{global_best_compact['height']}, "
            f"area={float(global_best_compact['area']):.1f}, "
            f"failed_edges={len(global_best_compact['failed_edges'])}, "
            f"cost={global_best_cost:.1f}"
        )
        print(f"[RL] Layout SVG written to: {os.path.join(output_dir, f'{stem}_rl_layout.svg')}")
        print(f"[RL] Layout TeX written to: {os.path.join(output_dir, f'{stem}_rl_layout.tex')}")
        print(f"[RL] Layout IFCN written to: {os.path.join(output_dir, f'{stem}_rl_layout.ifcn')}")
        print(f"[RL] Encoded layout IFCN written to: {os.path.join(output_dir, f'{stem}_rl_layout_encoded.ifcn')}")
        print(f"[RL] Training config written to: {config_path}")
        print(f"[RL] Training log written to: {history_path}")
        print(f"[RL] Summary written to: {summary_path}")
        print(f"[RL] Warm start snapshot written to: {warm_start_path}")
        print(f"[RL] Best layout snapshot written to: {best_layout_path}")
        print(f"[RL] Candidate pool written to: {candidate_pool_path}")
        print(f"[RL] Action histogram written to: {action_histogram_path}")
        return

    train_start = time.perf_counter()
    global_best_episode = 0
    stopped_early = False
    early_stop_reason = None

    def validate_exact_candidate(
        candidate_positions,
        clock_context,
        label,
        preferred_embedding_guidance=False,
        timeout_sec=None,
    ):
        validation_signature = (
            tuple(int(value) for value in clock_context),
            tuple(
                (int(node_id), int(coord[0]), int(coord[1]))
                for node_id, coord in sorted(candidate_positions.items())
            ),
        )
        if validation_signature in exact_validation_signatures:
            return None
        exact_validation_signatures.add(validation_signature)
        try_orders = [bool(preferred_embedding_guidance)]
        alternate_order = not bool(preferred_embedding_guidance)
        if alternate_order not in try_orders:
            try_orders.append(alternate_order)

        best_payload = None
        best_key = None
        effective_timeout_sec = (
            int(args.exact_eval_timeout_sec)
            if timeout_sec is None
            else int(timeout_sec)
        )
        for try_idx, use_embedding_guidance in enumerate(try_orders, start=1):
            validation_start = time.perf_counter()
            exact_result = evaluate_layout_candidate_with_timeout(
                {
                    "strategy": f"rl-exact-{label}",
                    "orientation": env.orientation,
                    "x_spacing": "n/a",
                    "y_spacing": "n/a",
                    "node_positions": clone_positions(candidate_positions),
                    "routing_embedding_guidance": bool(use_embedding_guidance),
                },
                circuit,
                int(clock_context[0]),
                int(clock_context[1]),
                int(clock_context[2]),
                embedding_scores=(embedding_scores if use_embedding_guidance else None),
                timeout_sec=effective_timeout_sec,
            )
            exact_compact = env._compact_result(exact_result)
            exact_cost = env._score_result(exact_compact)
            record = {
                "label": str(label),
                "try_index": int(try_idx),
                "routing_embedding_guidance": bool(use_embedding_guidance),
                "failed_edges": int(len(exact_compact["failed_edges"])),
                "area": float(exact_compact["area"]),
                "width": int(exact_compact["width"]),
                "height": int(exact_compact["height"]),
                "cost": float(exact_cost),
                "runtime_sec": float(time.perf_counter() - validation_start),
                "timeout_sec": int(effective_timeout_sec),
            }
            exact_validation_records.append(record)
            print(
                "[Strict] Exact validation "
                f"{label} try={try_idx} embedding={int(use_embedding_guidance)}: "
                f"failed_edges={record['failed_edges']} "
                f"area={record['area']:.1f} "
                f"size={record['width']}x{record['height']} "
                f"runtime={record['runtime_sec']:.2f}s"
            )
            exact_key = layout_selection_key(exact_compact, exact_cost, args.best_selection_mode)
            if best_key is None or exact_key < best_key:
                best_payload = (exact_result, exact_compact, exact_cost, record)
                best_key = exact_key
            if int(len(exact_compact["failed_edges"])) == 0:
                return exact_result, exact_compact, exact_cost, record
        return best_payload

    if env.train_eval_mode == "placement" and args.final_exact_validation:
        print("[Strict] Establishing exact-routed reference for placement-mode area comparison.")
        reference_payload = validate_exact_candidate(
            strict_reference_positions,
            strict_reference_clock_context,
            "strict_reference",
            preferred_embedding_guidance=bool(warm_start.get("routing_embedding_guidance", False)),
        )
        if reference_payload is not None:
            strict_reference_exact_payload = reference_payload
            reference_result, reference_compact, reference_cost, _record = reference_payload
            if int(len(reference_compact["failed_edges"])) == 0:
                strict_reference_exact_valid = True
                strict_reference_result = reference_result
                strict_reference_compact = reference_compact
                strict_reference_cost = float(reference_cost)
                strict_reference_positions = clone_positions(reference_result["node_positions"])
                verified_best_result = reference_result
                verified_best_compact = reference_compact
                verified_best_cost = float(reference_cost)
                verified_best_positions = clone_positions(reference_result["node_positions"])
                verified_best_key = layout_selection_key(
                    reference_compact,
                    reference_cost,
                    args.best_selection_mode,
                )
                print(
                    "[Strict] Exact routed reference ready: "
                    f"area={strict_reference_compact['area']:.1f} "
                    f"size={strict_reference_compact['width']}x{strict_reference_compact['height']}"
                )
            else:
                print(
                    "[Strict] Exact routed reference failed; strict success will require a valid "
                    "final exact candidate."
                )

    def record_placement_candidate(
        label,
        candidate_positions,
        candidate_compact,
        candidate_cost,
        clock_context,
        preferred_embedding_guidance=False,
    ):
        if env.train_eval_mode != "placement":
            return
        signature = (
            tuple(int(value) for value in clock_context),
            tuple(
                (int(node_id), int(coord[0]), int(coord[1]))
                for node_id, coord in sorted(candidate_positions.items())
            ),
        )
        if signature in placement_candidate_signatures:
            return
        placement_candidate_signatures.add(signature)
        key = layout_selection_key(candidate_compact, candidate_cost, args.best_selection_mode)
        placement_candidate_pool.append(
            {
                "label": str(label),
                "positions": clone_positions(candidate_positions),
                "clock_context": tuple(int(value) for value in clock_context),
                "compact": dict(candidate_compact),
                "cost": float(candidate_cost),
                "key": key,
                "preferred_embedding_guidance": bool(preferred_embedding_guidance),
            }
        )
        placement_candidate_pool.sort(
            key=lambda item: (
                int(len(item["compact"]["failed_edges"])),
                float(item["compact"]["area"]),
                int(max(item["compact"]["width"], item["compact"]["height"])),
                float(item["cost"]),
            )
        )
        max_pool_size = max(1, int(args.placement_candidate_pool_size))
        if len(placement_candidate_pool) > max_pool_size:
            del placement_candidate_pool[max_pool_size:]

    def run_legal_repair_search():
        max_repair_candidates = max(0, int(args.legal_repair_candidates))
        if max_repair_candidates <= 0:
            return None, {
                "ran": False,
                "candidate_count": 0,
                "timeout_sec": int(args.exact_eval_timeout_sec),
                "reason": "disabled",
            }

        max_padding = max(int(args.padding), int(args.legal_repair_max_padding))
        repair_timeout_sec = max(
            int(args.exact_eval_timeout_sec),
            int(round(float(args.exact_eval_timeout_sec) * max(1.0, float(args.legal_repair_timeout_multiplier)))),
        )
        repair_records = []
        repair_signatures = set()

        def add_repair_record(label, positions, clock_context, preferred_embedding_guidance=False):
            positions = clone_positions(positions)
            signature = (
                tuple(int(value) for value in clock_context),
                tuple(
                    (int(node_id), int(coord[0]), int(coord[1]))
                    for node_id, coord in sorted(positions.items())
                ),
            )
            if signature in repair_signatures or signature in exact_validation_signatures:
                return
            repair_signatures.add(signature)
            repair_records.append(
                {
                    "label": str(label),
                    "positions": positions,
                    "clock_context": tuple(int(value) for value in clock_context),
                    "preferred_embedding_guidance": bool(preferred_embedding_guidance),
                    "estimated_area": float(node_position_area(positions)),
                }
            )

        base_records = []
        base_records.append(
            {
                "label": "strict_reference_repair",
                "positions": clone_positions(strict_reference_positions),
                "clock_context": tuple(int(value) for value in strict_reference_clock_context),
                "preferred_embedding_guidance": bool(warm_start.get("routing_embedding_guidance", False)),
            }
        )
        base_records.extend(placement_candidate_pool[: max(1, min(len(placement_candidate_pool), max_repair_candidates))])
        expansion_schedule = (
            (1, 1, 0),
            (1, 2, 1),
            (2, 1, 1),
            (2, 2, 2),
            (1, 3, 2),
            (3, 1, 2),
            (2, 3, 3),
            (3, 2, 3),
            (3, 3, 4),
        )
        for base in base_records:
            base_clock = tuple(int(value) for value in base.get("clock_context", strict_reference_clock_context))
            for primary_scale, secondary_scale, padding_extra in expansion_schedule:
                repair_padding = min(max_padding, max(int(base_clock[1]), int(args.padding) + int(padding_extra)))
                positions = (
                    clone_positions(base["positions"])
                    if primary_scale == 1 and secondary_scale == 1 else
                    expand_node_positions(
                        base["positions"],
                        env.orientation,
                        primary_scale=primary_scale,
                        secondary_scale=secondary_scale,
                    )
                )
                add_repair_record(
                    f"{base['label']}_scale{primary_scale}x{secondary_scale}_pad{repair_padding}",
                    positions,
                    (int(base_clock[0]), int(repair_padding), int(base_clock[2])),
                    preferred_embedding_guidance=bool(base.get("preferred_embedding_guidance", False)),
                )

        orientations = [env.orientation]
        alternate_orientation = TOP_DOWN if env.orientation == LEFT_RIGHT else LEFT_RIGHT
        if alternate_orientation not in orientations:
            orientations.append(alternate_orientation)
        spacing_multipliers = (2, 3, 4, 5, 6)
        repair_strategies = ("gcn", "adaptive", "fixed")
        for multiplier in spacing_multipliers:
            repair_padding = min(max_padding, max(int(args.padding), int(args.padding) + max(0, multiplier - 2)))
            for orientation in orientations:
                for strategy in repair_strategies:
                    try:
                        candidate = build_single_start_candidate(
                            circuit=circuit,
                            ordered_layers=ordered_layers,
                            board_margin=board_margin,
                            x_spacing=max(1, int(args.x_spacing) * int(multiplier)),
                            y_spacing=max(1, int(args.y_spacing) * int(multiplier)),
                            embeddings=embeddings,
                            strategy=strategy,
                            orientation=orientation,
                        )
                    except Exception as ex:
                        print(f"[Strict] Skip repair start {strategy}/{orientation}/{multiplier}x: {ex}")
                        continue
                    add_repair_record(
                        f"fresh_{strategy}_{orientation}_spacing{multiplier}x_pad{repair_padding}",
                        candidate["node_positions"],
                        (int(args.phase_cycle), int(repair_padding), int(args.max_same_phase)),
                        preferred_embedding_guidance=bool(candidate.get("routing_embedding_guidance", False)),
                    )

        def repair_record_sort_key(item):
            return (
                float(item["estimated_area"]),
                int(max(item["clock_context"][1], 0)),
                item["label"],
            )

        def add_diverse_record(selected, selected_signatures, record):
            signature = (
                tuple(int(value) for value in record["clock_context"]),
                tuple(
                    (int(node_id), int(coord[0]), int(coord[1]))
                    for node_id, coord in sorted(record["positions"].items())
                ),
            )
            if signature in selected_signatures:
                return False
            selected.append(record)
            selected_signatures.add(signature)
            return True

        def diversify_repair_records(records, limit):
            if len(records) <= limit:
                return sorted(records, key=repair_record_sort_key)

            area_sorted = sorted(records, key=repair_record_sort_key)
            selected = []
            selected_signatures = set()

            # Keep a few compact candidates, but reserve most slots for wider
            # channels. Parity-like XOR trees often need the wider candidates
            # that pure area sorting would otherwise discard.
            compact_quota = max(1, min(len(area_sorted), limit // 3))
            for record in area_sorted[:compact_quota]:
                add_diverse_record(selected, selected_signatures, record)

            def spacing_multiplier(record):
                match = re.search(r"spacing(\d+)x", record["label"])
                return int(match.group(1)) if match else 0

            def scale_pressure(record):
                match = re.search(r"scale(\d+)x(\d+)", record["label"])
                if not match:
                    return 0
                return int(match.group(1)) * int(match.group(2))

            for multiplier in sorted({spacing_multiplier(record) for record in records}, reverse=True):
                if multiplier <= 0 or len(selected) >= limit:
                    continue
                family = [record for record in area_sorted if spacing_multiplier(record) == multiplier]
                for orientation in (env.orientation, TOP_DOWN if env.orientation == LEFT_RIGHT else LEFT_RIGHT):
                    if len(selected) >= limit:
                        break
                    oriented = [record for record in family if f"_{orientation}_" in record["label"]]
                    if oriented:
                        add_diverse_record(selected, selected_signatures, oriented[0])
                if len(selected) >= limit:
                    break

            for pressure in sorted({scale_pressure(record) for record in records}, reverse=True):
                if pressure <= 0 or len(selected) >= limit:
                    continue
                family = [record for record in area_sorted if scale_pressure(record) == pressure]
                if family:
                    add_diverse_record(selected, selected_signatures, family[0])

            for padding_value in sorted({int(record["clock_context"][1]) for record in records}, reverse=True):
                if len(selected) >= limit:
                    break
                family = [record for record in area_sorted if int(record["clock_context"][1]) == padding_value]
                if family:
                    add_diverse_record(selected, selected_signatures, family[0])

            for record in area_sorted:
                if len(selected) >= limit:
                    break
                add_diverse_record(selected, selected_signatures, record)

            return selected[:limit]

        repair_records = diversify_repair_records(repair_records, max_repair_candidates)
        if not repair_records:
            return None, {
                "ran": False,
                "candidate_count": 0,
                "timeout_sec": int(repair_timeout_sec),
                "reason": "no_candidates",
            }

        print(
            "[Strict] No legal exact final candidate yet; starting legality repair search: "
            f"candidates={len(repair_records)}, timeout={repair_timeout_sec}s"
        )
        best_payload = None
        best_key = None
        repair_start = time.perf_counter()
        for repair_idx, record in enumerate(repair_records, start=1):
            payload = validate_exact_candidate(
                record["positions"],
                record["clock_context"],
                f"legal_repair{repair_idx}_{record['label']}",
                preferred_embedding_guidance=bool(record.get("preferred_embedding_guidance", False)),
                timeout_sec=repair_timeout_sec,
            )
            if payload is None:
                continue
            exact_result, exact_compact, exact_cost, _validation_record = payload
            if int(len(exact_compact["failed_edges"])) != 0:
                continue
            exact_key = layout_selection_key(exact_compact, exact_cost, args.best_selection_mode)
            if best_key is None or exact_key < best_key:
                best_payload = payload
                best_key = exact_key
                print(
                    "[Strict] Legal repair candidate accepted: "
                    f"label={record['label']} "
                    f"area={exact_compact['area']:.1f} "
                    f"size={exact_compact['width']}x{exact_compact['height']}"
                )

        repair_summary = {
            "ran": True,
            "candidate_count": int(len(repair_records)),
            "timeout_sec": int(repair_timeout_sec),
            "runtime_sec": float(time.perf_counter() - repair_start),
            "legal_found": bool(best_payload is not None),
        }
        return best_payload, repair_summary

    record_placement_candidate(
        "warm_start",
        global_best_positions,
        global_best_compact,
        global_best_cost,
        global_best_clock_context,
        preferred_embedding_guidance=bool(warm_start.get("routing_embedding_guidance", False)),
    )

    for episode in range(1, args.episodes + 1):
        episode_start = time.perf_counter()
        elite_start = (
            episode > 1 and
            elite_start_probability > 0.0 and
            np.random.random() < elite_start_probability
        )
        if elite_start:
            obs = env.reset(
                start_positions=global_best_positions,
                start_compact=global_best_compact,
                start_cost=global_best_cost,
            )
        else:
            obs = env.reset()
        transitions = []
        episode_reward = 0.0
        episode_invalid_actions = 0
        episode_accepted_actions = 0
        episode_rollback_actions = 0
        positive_reward_steps = 0
        improvement_bonus_steps = 0
        reward_step_penalty_sum = 0.0
        reward_invalid_penalty_sum = 0.0
        reward_rollback_penalty_sum = 0.0
        reward_delta_raw_sum = 0.0
        reward_delta_clipped_sum = 0.0
        reward_improvement_bonus_sum = 0.0

        for step_idx in range(1, args.steps_per_episode + 1):
            action_mask = env.get_action_mask()
            action, logprob, value = select_action(
                model,
                obs,
                action_mask,
                device,
                action_prior_logits=action_prior_logits,
            )
            next_obs, reward, done, info = env.step(action)
            transitions.append(
                Transition(
                    obs=obs,
                    action=action,
                    logprob=logprob,
                    reward=reward,
                    value=value,
                    done=done,
                    action_mask=action_mask,
                )
            )
            obs = next_obs
            episode_reward += reward
            episode_invalid_actions += int(info["invalid_action"])
            episode_accepted_actions += int(info.get("accepted_action", True))
            episode_rollback_actions += int(info.get("rollback_action", False))
            positive_reward_steps += int(float(reward) > 0.0)
            improvement_bonus_steps += int(float(info["reward_improvement_bonus"]) > 0.0)
            reward_step_penalty_sum += float(info["reward_step_penalty"])
            reward_invalid_penalty_sum += float(info["reward_invalid_penalty"])
            reward_rollback_penalty_sum += float(info.get("reward_rollback_penalty", 0.0))
            reward_delta_raw_sum += float(info["reward_delta_raw"])
            reward_delta_clipped_sum += float(info["reward_delta_clipped"])
            reward_improvement_bonus_sum += float(info["reward_improvement_bonus"])

            step_rows.append(
                {
                    "episode": int(episode),
                    "step": int(step_idx),
                    "reward": float(reward),
                    "cumulative_reward": float(episode_reward),
                    "action_index": int(info["action_index"]),
                    "action_label": str(info["action_label"]),
                    "action_type": str(info["action_type"]),
                    "action_target": str(info["action_target"]),
                    "action_target_size": (
                        int(len(info["action_target"]))
                        if isinstance(info["action_target"], tuple)
                        else 1
                    ),
                    "action_delta": int(info["action_delta"]),
                    "phase_cycle": int(info.get("phase_cycle", env.phase_cycle)),
                    "padding": int(info.get("padding", env.padding)),
                    "max_same_phase": int(info.get("max_same_phase", env.max_same_phase)),
                    "invalid_action": int(info["invalid_action"]),
                    "accepted_action": int(info.get("accepted_action", True)),
                    "rollback_action": int(info.get("rollback_action", False)),
                    "reward_step_penalty": float(info["reward_step_penalty"]),
                    "reward_invalid_penalty": float(info["reward_invalid_penalty"]),
                    "reward_rollback_penalty": float(info.get("reward_rollback_penalty", 0.0)),
                    "reward_delta_raw": float(info["reward_delta_raw"]),
                    "reward_delta_clipped": float(info["reward_delta_clipped"]),
                    "reward_area_delta_clipped": float(info["reward_area_delta_clipped"]),
                    "reward_improvement_bonus": float(info["reward_improvement_bonus"]),
                    "previous_cost": float(info.get("previous_cost", env.current_cost)),
                    "current_cost": float(info["current_cost"]),
                    "best_cost": float(info["best_cost"]),
                    "candidate_cost": float(info.get("candidate_cost", info["current_cost"])),
                    "width": int(info.get("width", env.current_result["width"])),
                    "height": int(info.get("height", env.current_result["height"])),
                    "area": float(info.get("area", env.current_result["area"])),
                    "failed_edges": int(info.get("failed_edges", len(env.current_result["failed_edges"]))),
                    "candidate_width": int(info.get("candidate_width", info.get("width", env.current_result["width"]))),
                    "candidate_height": int(info.get("candidate_height", info.get("height", env.current_result["height"]))),
                    "candidate_area": float(info.get("candidate_area", info.get("area", env.current_result["area"]))),
                    "candidate_failed_edges": int(
                        info.get("candidate_failed_edges", info.get("failed_edges", len(env.current_result["failed_edges"])))
                    ),
                    "direction_violation_count": int(
                        info.get(
                            "direction_violation_count",
                            env.current_result["direction_violation_count"],
                        )
                    ),
                    "route_overhang_penalty": int(
                        info.get("route_overhang_penalty", env.current_result["route_overhang_penalty"])
                    ),
                    "io_exposure_penalty": int(
                        info.get("io_exposure_penalty", env.current_result["io_exposure_penalty"])
                    ),
                    "eval_cache_hit": int(info.get("eval_cache_hit", False)),
                    "eval_calls_episode": int(info.get("eval_calls_episode", env.eval_calls_episode)),
                    "eval_cache_hits_episode": int(
                        info.get("eval_cache_hits_episode", env.eval_cache_hits_episode)
                    ),
                    "eval_calls_total": int(info.get("eval_calls_total", env.eval_calls_total)),
                    "eval_cache_hits_total": int(
                        info.get("eval_cache_hits_total", env.eval_cache_hits_total)
                    ),
                }
            )
            if done:
                break

        ppo_metrics = ppo_update(
            model,
            optimizer,
            transitions,
            args,
            device,
            action_prior_logits=action_prior_logits,
        ) if transitions else {
            "policy_loss_mean": 0.0,
            "value_loss_mean": 0.0,
            "entropy_mean": 0.0,
            "approx_kl_mean": 0.0,
            "clipfrac_mean": 0.0,
            "update_steps": 0,
        }

        episode_best_compact = dict(env.best_result)
        episode_best_cost = float(env.best_cost)
        candidate_result, candidate_compact, candidate_cost, _cache_hit = env.evaluate_positions(
            env.best_positions
        )
        candidate_key = layout_selection_key(candidate_compact, candidate_cost, args.best_selection_mode)
        record_placement_candidate(
            f"episode{episode}_best",
            env.best_positions,
            candidate_compact,
            candidate_cost,
            (int(env.phase_cycle), int(env.padding), int(env.max_same_phase)),
        )
        if candidate_key < global_best_key:
            global_best_cost = candidate_cost
            global_best_positions = clone_positions(env.best_positions)
            global_best_result = candidate_result
            global_best_compact = candidate_compact
            global_best_key = candidate_key
            global_best_episode = int(episode)
            global_best_clock_context = (
                int(env.phase_cycle),
                int(env.padding),
                int(env.max_same_phase),
            )
            episode_best_compact = candidate_compact
            episode_best_cost = float(candidate_cost)

        if (
            env.train_eval_mode == "placement" and
            args.exact_validation_interval > 0 and
            episode % int(args.exact_validation_interval) == 0
        ):
            validation_payload = validate_exact_candidate(
                env.best_positions,
                (int(env.phase_cycle), int(env.padding), int(env.max_same_phase)),
                f"episode{episode}",
                preferred_embedding_guidance=False,
            )
            if validation_payload is not None:
                exact_result, exact_compact, exact_cost, _record = validation_payload
                exact_key = layout_selection_key(exact_compact, exact_cost, args.best_selection_mode)
                if (
                    int(len(exact_compact["failed_edges"])) == 0 and
                    (verified_best_key is None or exact_key < verified_best_key)
                ):
                    verified_best_result = exact_result
                    verified_best_compact = exact_compact
                    verified_best_cost = exact_cost
                    verified_best_positions = clone_positions(env.best_positions)
                verified_best_key = exact_key
                print(
                    "[Strict] New verified legal routed best: "
                    f"area={exact_compact['area']:.1f} "
                        f"size={exact_compact['width']}x{exact_compact['height']} "
                        f"cost={exact_cost:.1f}"
                    )

        episode_steps = len(transitions)
        episode_runtime_sec = time.perf_counter() - episode_start

        history_rows.append(
            {
                "episode": int(episode),
                "steps": int(episode_steps),
                "episode_reward": float(episode_reward),
                "episode_reward_mean_per_step": (
                    float(episode_reward) / float(episode_steps) if episode_steps else 0.0
                ),
                "episode_best_cost": float(episode_best_cost),
                "episode_best_area": float(episode_best_compact["area"]),
                "episode_best_width": int(episode_best_compact["width"]),
                "episode_best_height": int(episode_best_compact["height"]),
                "best_cost": float(global_best_cost),
                "best_area": float(global_best_compact["area"]),
                "best_width": int(global_best_compact["width"]),
                "best_height": int(global_best_compact["height"]),
                "failed_edges": int(len(global_best_compact["failed_edges"])),
                "phase_cycle": int(env.phase_cycle),
                "padding": int(env.padding),
                "max_same_phase": int(env.max_same_phase),
                "elite_start": int(elite_start),
                "end_cost": float(env.current_cost),
                "end_area": float(env.current_result["area"]),
                "invalid_action_count": int(episode_invalid_actions),
                "invalid_action_rate": (
                    float(episode_invalid_actions) / float(episode_steps) if episode_steps else 0.0
                ),
                "accepted_action_count": int(episode_accepted_actions),
                "accepted_action_rate": (
                    float(episode_accepted_actions) / float(episode_steps) if episode_steps else 0.0
                ),
                "rollback_action_count": int(episode_rollback_actions),
                "rollback_action_rate": (
                    float(episode_rollback_actions) / float(episode_steps) if episode_steps else 0.0
                ),
                "positive_reward_steps": int(positive_reward_steps),
                "positive_reward_rate": (
                    float(positive_reward_steps) / float(episode_steps) if episode_steps else 0.0
                ),
                "improvement_bonus_steps": int(improvement_bonus_steps),
                "reward_step_penalty_sum": float(reward_step_penalty_sum),
                "reward_invalid_penalty_sum": float(reward_invalid_penalty_sum),
                "reward_rollback_penalty_sum": float(reward_rollback_penalty_sum),
                "reward_delta_raw_sum": float(reward_delta_raw_sum),
                "reward_delta_clipped_sum": float(reward_delta_clipped_sum),
                "reward_improvement_bonus_sum": float(reward_improvement_bonus_sum),
                "eval_calls_episode": int(env.eval_calls_episode),
                "eval_cache_hits_episode": int(env.eval_cache_hits_episode),
                "eval_cache_hit_rate_episode": (
                    float(env.eval_cache_hits_episode) / float(env.eval_calls_episode)
                    if env.eval_calls_episode else 0.0
                ),
                "policy_loss_mean": float(ppo_metrics["policy_loss_mean"]),
                "value_loss_mean": float(ppo_metrics["value_loss_mean"]),
                "entropy_mean": float(ppo_metrics["entropy_mean"]),
                "approx_kl_mean": float(ppo_metrics["approx_kl_mean"]),
                "clipfrac_mean": float(ppo_metrics["clipfrac_mean"]),
                "update_steps": int(ppo_metrics["update_steps"]),
                "episode_runtime_sec": float(episode_runtime_sec),
            }
        )

        if episode == 1 or episode % args.log_interval == 0 or episode == args.episodes:
            print(
                "[RL] "
                f"episode={episode}/{args.episodes} "
                f"reward={episode_reward:.3f} "
                f"best_cost={global_best_cost:.1f} "
                f"best_area={global_best_compact['area']:.1f} "
                f"size={global_best_compact['width']}x{global_best_compact['height']} "
                f"failed_edges={len(global_best_compact['failed_edges'])} "
                f"invalid_rate={(episode_invalid_actions / max(1, episode_steps)):.3f} "
                f"rollback_rate={(episode_rollback_actions / max(1, episode_steps)):.3f} "
                f"entropy={ppo_metrics['entropy_mean']:.4f} "
                f"approx_kl={ppo_metrics['approx_kl_mean']:.5f}"
            )

        if (
            args.early_stop_patience > 0 and
            episode >= max(1, args.early_stop_min_episodes) and
            (episode - global_best_episode) >= args.early_stop_patience
        ):
            stopped_early = True
            early_stop_reason = (
                f"no_global_best_for_{int(args.early_stop_patience)}_episodes"
            )
            print(
                "[RL] Early stop triggered: "
                f"episode={episode} best_episode={global_best_episode} "
                f"reason={early_stop_reason}"
            )
            break

    run_time_sec = time.perf_counter() - train_start
    post_area_pack_evaluations = 0
    post_phase_strip_pack_evaluations = 0
    if args.post_primary_pack_rounds > 0:
        env.phase_cycle, env.padding, env.max_same_phase = global_best_clock_context
        packed_result, packed_compact, packed_cost, packed_positions = env.greedy_primary_pack(
            global_best_positions,
            global_best_compact,
            global_best_cost,
            max_rounds=args.post_primary_pack_rounds,
        )
        packed_key = layout_selection_key(packed_compact, packed_cost, args.best_selection_mode)
        if packed_result is not None and packed_key < global_best_key:
            print(
                "[RL] Post primary-pack improved layout: "
                f"cost {global_best_cost:.1f}->{packed_cost:.1f}, "
                f"area {global_best_compact['area']:.1f}->{packed_compact['area']:.1f}, "
                f"size {global_best_compact['width']}x{global_best_compact['height']}->"
                f"{packed_compact['width']}x{packed_compact['height']}"
            )
            global_best_result = packed_result
            global_best_compact = packed_compact
            global_best_cost = packed_cost
            global_best_positions = packed_positions
            global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
            record_placement_candidate(
                "post_primary_pack",
                global_best_positions,
                global_best_compact,
                global_best_cost,
                global_best_clock_context,
            )
    if args.post_area_pack_rounds > 0:
        env.phase_cycle, env.padding, env.max_same_phase = global_best_clock_context
        packed_result, packed_compact, packed_cost, packed_positions, post_area_pack_evaluations = env.greedy_area_pack(
            global_best_positions,
            global_best_compact,
            global_best_cost,
            max_rounds=args.post_area_pack_rounds,
            max_evaluations=args.post_pack_max_evaluations,
        )
        packed_key = layout_selection_key(packed_compact, packed_cost, args.best_selection_mode)
        if packed_result is not None and packed_key < global_best_key:
            print(
                "[RL] Post area-pack improved layout: "
                f"cost {global_best_cost:.1f}->{packed_cost:.1f}, "
                f"area {global_best_compact['area']:.1f}->{packed_compact['area']:.1f}, "
                f"size {global_best_compact['width']}x{global_best_compact['height']}->"
                f"{packed_compact['width']}x{packed_compact['height']}, "
                f"evaluations={post_area_pack_evaluations}"
            )
            global_best_result = packed_result
            global_best_compact = packed_compact
            global_best_cost = packed_cost
            global_best_positions = packed_positions
            global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
            record_placement_candidate(
                "post_area_pack",
                global_best_positions,
                global_best_compact,
                global_best_cost,
                global_best_clock_context,
            )
    if args.post_phase_strip_pack_rounds > 0:
        env.phase_cycle, env.padding, env.max_same_phase = global_best_clock_context
        (
            packed_result,
            packed_compact,
            packed_cost,
            packed_positions,
            post_phase_strip_pack_evaluations,
            phase_strip_pack_improved,
        ) = env.greedy_phase_strip_pack(
            global_best_positions,
            global_best_result,
            global_best_compact,
            global_best_cost,
            max_rounds=args.post_phase_strip_pack_rounds,
            max_evaluations=args.post_phase_strip_pack_max_evaluations,
        )
        if phase_strip_pack_improved:
            print(
                "[RL] Post phase-strip-pack improved layout: "
                f"cost {global_best_cost:.1f}->{packed_cost:.1f}, "
                f"area {global_best_compact['area']:.1f}->{packed_compact['area']:.1f}, "
                f"size {global_best_compact['width']}x{global_best_compact['height']}->"
                f"{packed_compact['width']}x{packed_compact['height']}, "
                f"evaluations={post_phase_strip_pack_evaluations}"
            )
            global_best_result = packed_result
            global_best_compact = packed_compact
            global_best_cost = packed_cost
            global_best_positions = packed_positions
            global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
            record_placement_candidate(
                "post_phase_strip_pack",
                global_best_positions,
                global_best_compact,
                global_best_cost,
                global_best_clock_context,
            )
    final_exact_validation = {
        "requested": bool(args.final_exact_validation and env.train_eval_mode == "placement"),
        "ran": False,
        "failed_edges": None,
        "area": None,
        "runtime_sec": 0.0,
        "candidate_count": 0,
    }
    legal_repair_summary = {
        "ran": False,
        "candidate_count": 0,
        "timeout_sec": int(args.exact_eval_timeout_sec),
        "legal_found": False,
    }
    if (
        env.train_eval_mode == "placement" and
        verified_best_result is not None and
        not args.final_exact_validation
    ):
        print("[Strict] Using verified exact-routed best for final export.")
        global_best_result = verified_best_result
        global_best_compact = verified_best_compact
        global_best_cost = float(verified_best_cost)
        global_best_positions = clone_positions(verified_best_positions)
        global_best_key = verified_best_key
    elif args.final_exact_validation and env.train_eval_mode == "placement":
        max_final_candidates = max(1, int(args.final_exact_validation_candidates))
        final_candidates = placement_candidate_pool[:max_final_candidates]
        strict_reference_fallback = {
            "label": "strict_reference_fallback",
            "positions": clone_positions(strict_reference_positions),
            "clock_context": tuple(int(value) for value in strict_reference_clock_context),
            "compact": dict(strict_reference_compact),
            "cost": float(strict_reference_cost),
            "key": layout_selection_key(strict_reference_compact, strict_reference_cost, args.best_selection_mode),
            "preferred_embedding_guidance": bool(warm_start.get("routing_embedding_guidance", False)),
        }
        if not any("strict_reference" in item["label"] for item in final_candidates):
            final_candidates.append(strict_reference_fallback)
        print(
            "[Strict] Running final exact routed validation for placement-trained candidates: "
            f"count={len(final_candidates)}"
        )
        best_exact_payload = strict_reference_exact_payload
        best_exact_key = None
        if best_exact_payload is not None:
            _ref_result, ref_compact, ref_cost, _ref_record = best_exact_payload
            best_exact_key = layout_selection_key(
                ref_compact,
                ref_cost,
                args.best_selection_mode,
            )
        final_validation_start = time.perf_counter()
        for candidate_idx, candidate_record in enumerate(final_candidates, start=1):
            validation_payload = validate_exact_candidate(
                candidate_record["positions"],
                candidate_record["clock_context"],
                f"final{candidate_idx}_{candidate_record['label']}",
                preferred_embedding_guidance=bool(candidate_record.get("preferred_embedding_guidance", False)),
            )
            if validation_payload is None:
                continue
            exact_result, exact_compact, exact_cost, record = validation_payload
            exact_key = layout_selection_key(exact_compact, exact_cost, args.best_selection_mode)
            if best_exact_key is None or exact_key < best_exact_key:
                best_exact_payload = validation_payload
                best_exact_key = exact_key
            if (
                int(len(exact_compact["failed_edges"])) == 0 and
                (verified_best_key is None or exact_key < verified_best_key)
            ):
                verified_best_result = exact_result
                verified_best_compact = exact_compact
                verified_best_cost = exact_cost
                verified_best_positions = clone_positions(candidate_record["positions"])
                verified_best_key = exact_key
                print(
                    "[Strict] Final validation found legal routed candidate: "
                    f"area={exact_compact['area']:.1f} "
                    f"size={exact_compact['width']}x{exact_compact['height']} "
                    f"candidate={candidate_record['label']}"
                )

        final_exact_validation = {
            "requested": True,
            "ran": True,
            "candidate_count": int(len(final_candidates)),
            "runtime_sec": float(time.perf_counter() - final_validation_start),
            "failed_edges": None,
            "area": None,
        }
        if verified_best_result is not None:
            global_best_result = verified_best_result
            global_best_compact = verified_best_compact
            global_best_cost = float(verified_best_cost)
            global_best_positions = clone_positions(verified_best_positions)
            global_best_key = verified_best_key
            final_exact_validation["failed_edges"] = int(len(global_best_compact["failed_edges"]))
            final_exact_validation["area"] = float(global_best_compact["area"])
        else:
            repair_payload, legal_repair_summary = run_legal_repair_search()
            if repair_payload is not None:
                exact_result, exact_compact, exact_cost, _record = repair_payload
                exact_key = layout_selection_key(exact_compact, exact_cost, args.best_selection_mode)
                verified_best_result = exact_result
                verified_best_compact = exact_compact
                verified_best_cost = exact_cost
                verified_best_positions = clone_positions(exact_result["node_positions"])
                verified_best_key = exact_key
                global_best_result = exact_result
                global_best_compact = exact_compact
                global_best_cost = exact_cost
                global_best_positions = clone_positions(exact_result["node_positions"])
                global_best_key = exact_key
                final_exact_validation["failed_edges"] = int(len(global_best_compact["failed_edges"]))
                final_exact_validation["area"] = float(global_best_compact["area"])
        if verified_best_result is None and best_exact_payload is not None and not bool(args.require_legal_final):
            exact_result, exact_compact, exact_cost, _record = best_exact_payload
            global_best_result = exact_result
            global_best_compact = exact_compact
            global_best_cost = exact_cost
            global_best_positions = clone_positions(exact_result["node_positions"])
            global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
            final_exact_validation["failed_edges"] = int(len(global_best_compact["failed_edges"]))
            final_exact_validation["area"] = float(global_best_compact["area"])
            if int(len(global_best_compact["failed_edges"])) > 0:
                print(
                    "[Strict] No fully routed final exact candidate found; exporting partial "
                    "result only because --no-require-legal-final was requested."
                )
        elif verified_best_result is None:
            print("[Strict] No fully routed final exact candidate found; no layout artifacts will be exported.")
    if (
        bool(args.require_legal_final) and
        int(len(global_best_compact["failed_edges"])) > 0 and
        not bool(legal_repair_summary.get("ran", False))
    ):
        repair_payload, legal_repair_summary = run_legal_repair_search()
        if repair_payload is not None:
            exact_result, exact_compact, exact_cost, _record = repair_payload
            global_best_result = exact_result
            global_best_compact = exact_compact
            global_best_cost = exact_cost
            global_best_positions = clone_positions(exact_result["node_positions"])
            global_best_key = layout_selection_key(global_best_compact, global_best_cost, args.best_selection_mode)
            verified_best_result = exact_result
            verified_best_compact = exact_compact
            verified_best_cost = exact_cost
            verified_best_positions = clone_positions(exact_result["node_positions"])
            verified_best_key = global_best_key

    if bool(args.require_legal_final) and int(len(global_best_compact["failed_edges"])) > 0:
        run_time_sec = time.perf_counter() - train_start
        history_path = write_training_history(output_dir, benchmark_label, history_rows)
        step_log_path = None
        if not args.disable_step_log:
            step_log_path = write_csv_rows(
                os.path.join(output_dir, f"{stem}_rl_steps.csv"),
                step_rows,
                fieldnames=list(step_rows[0].keys()) if step_rows else [],
            )
        summary = build_training_summary(
            args=args,
            circuit=circuit,
            benchmark_path=benchmark_path,
            resolved_benchmark_path=resolved_benchmark_path,
            output_dir=output_dir,
            device=device,
            env=env,
            warm_start=warm_start,
            best_result=global_best_result,
            best_compact=global_best_compact,
            global_best_cost=global_best_cost,
            memory_only=memory_only,
            parser_safe_generated=parser_safe_generated,
            crossings_per_layer=crossings_per_layer,
            episode_rows=history_rows,
            step_rows=step_rows,
            run_time_sec=run_time_sec,
        )
        summary["status"] = "failed"
        summary["export_skipped_reason"] = "no_legal_fully_routed_layout"
        summary["episodes_completed"] = int(len(history_rows))
        summary["stopped_early"] = bool(stopped_early)
        summary["early_stop_reason"] = early_stop_reason
        summary["post_area_pack_evaluations"] = int(post_area_pack_evaluations)
        summary["post_phase_strip_pack_evaluations"] = int(post_phase_strip_pack_evaluations)
        summary["train_eval_mode_resolved"] = str(env.train_eval_mode)
        summary["final_exact_validation"] = final_exact_validation
        summary["legal_repair"] = legal_repair_summary
        summary["exact_validation_records"] = exact_validation_records
        summary["verified_exact_best_found"] = False
        summary["strict_success"] = False
        summary["rl_experience_path"] = None if args.disable_rl_experience else experience_path
        summary["rl_experience_action_templates"] = 0
        summary["rl_experience_updates"] = 0
        summary["rl_experience_update_skipped_reason"] = "no_legal_fully_routed_layout"
        summary_path = write_json_file(os.path.join(output_dir, f"{stem}_rl_summary.json"), summary)
        warm_start_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_warm_start.json"),
            build_layout_snapshot(warm_start),
        )
        best_layout_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_best_layout.json"),
            build_layout_snapshot(global_best_result),
        )
        candidate_pool_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_candidate_pool.json"),
            build_candidate_pool_snapshot(placement_candidate_pool),
        )
        action_histogram_path = write_json_file(
            os.path.join(output_dir, f"{stem}_rl_action_histogram.json"),
            build_action_histogram(step_rows, top_k=32),
        )
        print(
            "[Strict] Legal final layout required, but no candidate routed all edges. "
            f"Best failed_edges={len(global_best_compact['failed_edges'])}. "
            "Layout .ifcn/.svg/.tex export skipped."
        )
        print(f"[RL] Training log written to: {history_path}")
        if step_log_path is not None:
            print(f"[RL] Step log written to: {step_log_path}")
        print(f"[RL] Summary written to: {summary_path}")
        print(f"[RL] Warm start snapshot written to: {warm_start_path}")
        print(f"[RL] Best failed snapshot written to: {best_layout_path}")
        print(f"[RL] Candidate pool written to: {candidate_pool_path}")
        print(f"[RL] Action histogram written to: {action_histogram_path}")
        raise SystemExit(2)
    if not args.disable_layout_memory:
        save_layout_memory(
            global_best_result,
            resolved_benchmark_path,
            args.seed,
            allow_failed=not args.strict_memory_updates,
        )
    export_layout_artifacts(
        circuit,
        ordered_layers,
        global_best_result,
        global_best_positions,
        output_dir,
        run_time_sec,
        artifact_stem=output_stem,
        benchmark_label=benchmark_label,
        phase_cycle=global_best_clock_context[0],
    )
    history_path = write_training_history(output_dir, benchmark_label, history_rows)
    step_log_path = None
    if not args.disable_step_log:
        step_log_path = write_csv_rows(
            os.path.join(output_dir, f"{stem}_rl_steps.csv"),
            step_rows,
            fieldnames=list(step_rows[0].keys()) if step_rows else [],
        )
    summary = build_training_summary(
        args=args,
        circuit=circuit,
        benchmark_path=benchmark_path,
        resolved_benchmark_path=resolved_benchmark_path,
        output_dir=output_dir,
        device=device,
        env=env,
        warm_start=warm_start,
        best_result=global_best_result,
        best_compact=global_best_compact,
        global_best_cost=global_best_cost,
        memory_only=memory_only,
        parser_safe_generated=parser_safe_generated,
        crossings_per_layer=crossings_per_layer,
        episode_rows=history_rows,
        step_rows=step_rows,
        run_time_sec=run_time_sec,
    )
    summary["episodes_completed"] = int(len(history_rows))
    summary["stopped_early"] = bool(stopped_early)
    summary["early_stop_reason"] = early_stop_reason
    summary["early_stop_patience"] = int(args.early_stop_patience)
    summary["early_stop_min_episodes"] = int(args.early_stop_min_episodes)
    summary["post_area_pack_evaluations"] = int(post_area_pack_evaluations)
    summary["post_phase_strip_pack_evaluations"] = int(post_phase_strip_pack_evaluations)
    summary["train_eval_mode_resolved"] = str(env.train_eval_mode)
    summary["final_exact_validation"] = final_exact_validation
    summary["legal_repair"] = legal_repair_summary
    summary["exact_validation_records"] = exact_validation_records
    summary["verified_exact_best_found"] = bool(verified_best_result is not None)
    summary["placement_candidate_pool_size"] = int(len(placement_candidate_pool))
    summary["placement_candidate_pool_preview"] = [
        {
            "label": item["label"],
            "area": float(item["compact"]["area"]),
            "width": int(item["compact"]["width"]),
            "height": int(item["compact"]["height"]),
            "cost": float(item["cost"]),
        }
        for item in placement_candidate_pool[: min(10, len(placement_candidate_pool))]
    ]
    summary["strict_reference"] = {
        "width": int(strict_reference_compact["width"]),
        "height": int(strict_reference_compact["height"]),
        "area": float(strict_reference_compact["area"]),
        "failed_edges": int(len(strict_reference_compact["failed_edges"])),
        "cost": float(strict_reference_cost),
    }
    summary["strict_success"] = bool(int(summary.get("best_failed_edges", 0)) == 0)
    summary["area_improved_vs_strict_reference"] = bool(
        summary["strict_success"] and
        float(summary.get("best_area", 0.0)) < float(strict_reference_compact["area"])
    )
    should_update_experience = (
        not args.disable_rl_experience and
        (not args.strict_memory_updates or bool(summary["strict_success"]))
    )
    if should_update_experience:
        experience_memory = update_experience_memory(
            experience_memory,
            step_rows,
            benchmark_label,
            summary,
        )
        written_experience_path = write_experience_memory(experience_path, experience_memory)
        summary["rl_experience_path"] = written_experience_path
        summary["rl_experience_action_templates"] = int(len(experience_memory.get("actions", {})))
        summary["rl_experience_updates"] = int(len(experience_memory.get("updates", [])))
    else:
        summary["rl_experience_path"] = None if args.disable_rl_experience else experience_path
        summary["rl_experience_action_templates"] = 0
        summary["rl_experience_updates"] = 0
        summary["rl_experience_update_skipped_reason"] = (
            "disabled"
            if args.disable_rl_experience else
            "strict_success_required"
        )
    summary_path = write_json_file(os.path.join(output_dir, f"{stem}_rl_summary.json"), summary)
    warm_start_path = write_json_file(
        os.path.join(output_dir, f"{stem}_rl_warm_start.json"),
        build_layout_snapshot(warm_start),
    )
    best_layout_path = write_json_file(
        os.path.join(output_dir, f"{stem}_rl_best_layout.json"),
        build_layout_snapshot(global_best_result),
    )
    candidate_pool_path = write_json_file(
        os.path.join(output_dir, f"{stem}_rl_candidate_pool.json"),
        build_candidate_pool_snapshot(placement_candidate_pool),
    )
    action_histogram_path = write_json_file(
        os.path.join(output_dir, f"{stem}_rl_action_histogram.json"),
        build_action_histogram(step_rows, top_k=32),
    )
    plot_path = None
    if not args.disable_training_plots:
        plot_path = plot_training_curves(output_dir, benchmark_label, history_rows)

    model_path = os.path.join(output_dir, f"{stem}_rl_policy.pt")
    torch.save(
        {
            "model_state_dict": model.state_dict(),
            "optimizer_state_dict": optimizer.state_dict(),
            "obs_dim": env.obs_dim,
            "action_dim": env.action_dim,
            "hidden_dim": args.hidden_dim,
            "orientation": env.orientation,
            "ordered_layers": ordered_layers,
            "benchmark": benchmark_path,
            "resolved_benchmark": resolved_benchmark_path,
            "parser_safe_generated": parser_safe_generated,
            "config": run_config,
            "summary": summary,
        },
        model_path,
    )

    warm_start_cost = scalarize_layout_result(
        warm_start,
        aspect_ratio_limit=args.aspect_ratio_limit,
        aspect_ratio_weight=args.aspect_ratio_weight,
        max_span_weight=args.max_span_weight,
        area_reference=float(warm_start["area"]),
        area_regression_weight=args.area_regression_weight,
    )
    warm_start_area = float(warm_start["area"])
    best_area = float(global_best_compact["area"])
    improvement_ratio = 0.0
    if warm_start_area > 0:
        improvement_ratio = (warm_start_area - best_area) / warm_start_area

    print("[RL] PPO layout refinement completed.")
    print(f"[RL] Benchmark: {benchmark_path}")
    if parser_safe_generated:
        print(f"[RL] Parser-safe benchmark used: {resolved_benchmark_path}")
    print(f"[RL] Device: {device}")
    print(
        "[RL] Warm start: "
        f"strategy={warm_start['layout_strategy']}, "
        f"orientation={warm_start['layout_orientation']}, "
        f"size={warm_start['width']}x{warm_start['height']}, "
        f"area={warm_start_area:.1f}, "
        f"failed_edges={len(warm_start['failed_edges'])}, "
        f"cost={warm_start_cost:.1f}"
    )
    print(
        "[RL] Best layout: "
        f"orientation={env.orientation}, "
        f"size={global_best_compact['width']}x{global_best_compact['height']}, "
        f"area={best_area:.1f}, "
        f"failed_edges={len(global_best_compact['failed_edges'])}, "
        f"cost={global_best_cost:.1f}"
    )
    print(f"[RL] Area improvement vs warm start: {improvement_ratio * 100:.2f}%")
    print(f"[RL] Crossings after GCN+barycenter ordering: {sum(crossings_per_layer.values())}")
    print(f"[RL] Training runtime: {run_time_sec:.4f} s")
    print(f"[RL] Layout SVG written to: {os.path.join(output_dir, f'{stem}_rl_layout.svg')}")
    print(f"[RL] Layout TeX written to: {os.path.join(output_dir, f'{stem}_rl_layout.tex')}")
    print(f"[RL] Layout IFCN written to: {os.path.join(output_dir, f'{stem}_rl_layout.ifcn')}")
    print(f"[RL] Encoded layout IFCN written to: {os.path.join(output_dir, f'{stem}_rl_layout_encoded.ifcn')}")
    print(f"[RL] Policy checkpoint written to: {model_path}")
    print(f"[RL] Training config written to: {config_path}")
    print(f"[RL] Training log written to: {history_path}")
    if step_log_path is not None:
        print(f"[RL] Step log written to: {step_log_path}")
    if plot_path is not None:
        print(f"[RL] Training curves written to: {plot_path}")
    print(f"[RL] Summary written to: {summary_path}")
    print(f"[RL] Warm start snapshot written to: {warm_start_path}")
    print(f"[RL] Best layout snapshot written to: {best_layout_path}")
    print(f"[RL] Candidate pool written to: {candidate_pool_path}")
    print(f"[RL] Action histogram written to: {action_histogram_path}")


if __name__ == "__main__":
    main()
