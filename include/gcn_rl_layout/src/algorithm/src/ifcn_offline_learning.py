"""Convert final IFCN layouts into dynamic-action offline learning samples."""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Mapping, Sequence

import numpy as np
import torch
import torch.nn.functional as F

try:
    from .ifcn_layout_dataset import IFCNLayout
    from .universal_graph_policy import (
        ACTION_FEATURE_DIM,
        CLOCK_FEATURE_DIM,
        EDGE_FEATURE_DIM,
        EPISODE_FEATURE_DIM,
        GATE_TYPE_NAMES,
        GraphPolicyInput,
        LayoutActionType,
        NODE_FEATURE_DIM,
        RETRIEVAL_FEATURE_DIM,
        ROUTE_AUX_FEATURE_DIM,
        UniversalGraphPolicy,
    )
except ImportError:  # Supports the project's top-level ``src`` import style.
    from ifcn_layout_dataset import IFCNLayout
    from universal_graph_policy import (
        ACTION_FEATURE_DIM,
        CLOCK_FEATURE_DIM,
        EDGE_FEATURE_DIM,
        EPISODE_FEATURE_DIM,
        GATE_TYPE_NAMES,
        GraphPolicyInput,
        LayoutActionType,
        NODE_FEATURE_DIM,
        RETRIEVAL_FEATURE_DIM,
        ROUTE_AUX_FEATURE_DIM,
        UniversalGraphPolicy,
    )


_DIRECTION_VECTOR = {
    "R": (1.0, 0.0),
    "L": (-1.0, 0.0),
    "D": (0.0, 1.0),
    "U": (0.0, -1.0),
}

_NODE_PHASE_CACHE: dict[str, dict[int, int]] = {}


@dataclass(frozen=True)
class _CanonicalTransform:
    swap_axes: bool
    flip_primary: bool

    def vector(self, x: float, y: float) -> tuple[float, float]:
        primary, secondary = (y, x) if self.swap_axes else (x, y)
        if self.flip_primary:
            primary = -primary
        return float(primary), float(secondary)


@dataclass(frozen=True)
class OfflineIFCNSample:
    policy_input: GraphPolicyInput
    quality_input: GraphPolicyInput
    target_positions: torch.Tensor
    target_route_features: torch.Tensor
    positive_action_mask: torch.Tensor
    quality_target: float
    topology_hash: str
    source_path: str

    def validate(self) -> None:
        self.policy_input.validate()
        self.quality_input.validate()
        node_count = int(self.policy_input.node_features.shape[0])
        edge_count = int(self.policy_input.edge_index.shape[1])
        action_count = int(self.policy_input.action_types.shape[0])
        if self.target_positions.shape != (node_count, 2):
            raise ValueError("target_positions shape does not match the graph")
        if self.target_route_features.shape != (edge_count, ROUTE_AUX_FEATURE_DIM):
            raise ValueError("target_route_features shape does not match the graph")
        if self.positive_action_mask.shape != (action_count,):
            raise ValueError("positive_action_mask shape does not match the actions")
        if not bool(torch.any(self.positive_action_mask)):
            raise ValueError("at least one offline teacher action must be positive")


def _gate_type_index(node_type: str) -> int | None:
    normalized = str(node_type).strip().lower()
    aliases = {
        "in": "input",
        "out": "output",
        "inv": "not",
        "majority": "maj",
        "buf": "redundancy",
    }
    normalized = aliases.get(normalized, normalized)
    for index, candidate in enumerate(GATE_TYPE_NAMES):
        if normalized == candidate or normalized.endswith(f".{candidate}"):
            return index
    return None


def _logic_layers(node_ids: Sequence[int], edges: Sequence[tuple[int, int]]) -> dict[int, int]:
    predecessors = {int(node_id): [] for node_id in node_ids}
    successors = {int(node_id): [] for node_id in node_ids}
    for source, target in edges:
        if source in successors and target in predecessors:
            successors[source].append(target)
            predecessors[target].append(source)
    indegree = {node_id: len(values) for node_id, values in predecessors.items()}
    queue = sorted(node_id for node_id, degree in indegree.items() if degree == 0)
    layers = {node_id: 0 for node_id in node_ids}
    visited = set()
    while queue:
        node_id = queue.pop(0)
        visited.add(node_id)
        for target in successors[node_id]:
            layers[target] = max(layers[target], layers[node_id] + 1)
            indegree[target] -= 1
            if indegree[target] == 0:
                queue.append(target)
                queue.sort()
    # Feedback/cyclic records are uncommon; retain a deterministic fallback.
    for node_id in sorted(set(node_ids) - visited):
        parent_layers = [layers[parent] for parent in predecessors[node_id] if parent in visited]
        layers[node_id] = max(parent_layers, default=-1) + 1
    return layers


def _canonical_target_positions(
    record: IFCNLayout,
    node_ids: Sequence[int],
    layers: Mapping[int, int],
) -> tuple[dict[int, tuple[float, float]], _CanonicalTransform]:
    raw = {
        int(node.node_id): (
            2.0 * float(node.normalized_position[0]) - 1.0,
            2.0 * float(node.normalized_position[1]) - 1.0,
        )
        for node in record.nodes
    }
    layer_values = np.asarray([float(layers[node_id]) for node_id in node_ids])
    xs = np.asarray([raw[node_id][0] for node_id in node_ids])
    ys = np.asarray([raw[node_id][1] for node_id in node_ids])

    def correlation(values):
        if len(values) <= 1 or float(np.std(values)) < 1e-9 or float(np.std(layer_values)) < 1e-9:
            return 0.0
        return float(np.corrcoef(layer_values, values)[0, 1])

    corr_x = correlation(xs)
    corr_y = correlation(ys)
    swap_axes = abs(corr_y) > abs(corr_x)
    primary_correlation = corr_y if swap_axes else corr_x
    transform = _CanonicalTransform(
        swap_axes=bool(swap_axes),
        flip_primary=bool(primary_correlation < 0.0),
    )
    result = {}
    for node_id in node_ids:
        x, y = raw[node_id]
        result[node_id] = transform.vector(x, y)
    return result, transform


def _phase_count(record: IFCNLayout) -> int:
    raw_value = record.header.get("phase count", "4")
    try:
        phase_count = int(str(raw_value).split()[0])
    except (TypeError, ValueError, IndexError):
        phase_count = 4
    return max(1, min(4, phase_count))


def _node_phases(record: IFCNLayout) -> dict[int, int]:
    cached = _NODE_PHASE_CACHE.get(record.layout_hash)
    if cached is not None:
        return cached
    phase_map = record.effective_phase_map()
    phase_count = _phase_count(record)
    phases = {
        int(node.node_id): int(phase_map[node.position])
        for node in record.nodes
        if node.position in phase_map
        and 0 <= int(phase_map[node.position]) < phase_count
    }
    _NODE_PHASE_CACHE[record.layout_hash] = phases
    return phases


def build_ifcn_clock_features(record: IFCNLayout) -> torch.Tensor:
    """Map legacy IFCN metadata onto the online 15-D clock schema.

    Historical IFCN files contain a phase projection but usually do not record
    the absolute stage field, causal axis, directions, or stochastic mode.  The
    known phase-count and canonical left-to-right orientation are populated;
    unknown ClockField descriptor components stay zero instead of being guessed.
    """

    normalized_phase_count = float(_phase_count(record)) / 4.0
    base = [normalized_phase_count, 0.0, 0.0, 1.0, 0.0]
    field = [normalized_phase_count] + [0.0] * 9
    features = torch.tensor(base + field, dtype=torch.float32)
    if features.shape != (CLOCK_FEATURE_DIM,):
        raise RuntimeError("IFCN clock feature schema drifted from the graph policy")
    return features


def _initial_positions(
    node_ids: Sequence[int],
    layers: Mapping[int, int],
) -> dict[int, tuple[float, float]]:
    grouped: dict[int, list[int]] = {}
    for node_id in node_ids:
        grouped.setdefault(int(layers[node_id]), []).append(int(node_id))
    max_layer = max(grouped, default=0)
    positions = {}
    for layer_idx, nodes in grouped.items():
        ordered = sorted(nodes)
        for rank, node_id in enumerate(ordered):
            primary = 2.0 * layer_idx / max(1, max_layer) - 1.0
            secondary = 2.0 * rank / max(1, len(ordered) - 1) - 1.0 if len(ordered) > 1 else 0.0
            positions[node_id] = (float(primary), float(secondary))
    return positions


def _perturb_positions(
    target: Mapping[int, tuple[float, float]],
    layers: Mapping[int, int],
    rng: np.random.Generator,
    scale: float,
) -> dict[int, tuple[float, float]]:
    if float(scale) <= 0.0:
        return dict(target)
    layer_primary_noise = {
        layer_idx: float(rng.normal(0.0, scale))
        for layer_idx in set(layers.values())
    }
    return {
        node_id: (
            float(target[node_id][0] + layer_primary_noise[layers[node_id]]),
            float(target[node_id][1] + rng.normal(0.0, scale)),
        )
        for node_id in target
    }


def _make_policy_input(
    record: IFCNLayout,
    node_ids: Sequence[int],
    edges: Sequence[tuple[int, int]],
    layers: Mapping[int, int],
    positions: Mapping[int, tuple[float, float]],
    actions: Sequence[tuple[LayoutActionType, tuple[int, ...], float]],
    retrieval_features: Sequence[float] | torch.Tensor | None,
) -> GraphPolicyInput:
    node_to_index = {node_id: index for index, node_id in enumerate(node_ids)}
    node_by_id = {int(node.node_id): node for node in record.nodes}
    fanins = {node_id: [] for node_id in node_ids}
    fanouts = {node_id: [] for node_id in node_ids}
    for source, target in edges:
        fanouts[source].append(target)
        fanins[target].append(source)
    grouped = {
        layer_idx: sorted(node_id for node_id in node_ids if layers[node_id] == layer_idx)
        for layer_idx in sorted(set(layers.values()))
    }
    rank_by_node = {
        node_id: rank
        for nodes in grouped.values()
        for rank, node_id in enumerate(nodes)
    }
    max_layer = max(layers.values(), default=0)
    node_phases = _node_phases(record)
    phase_count = _phase_count(record)
    node_rows = []
    for node_id in node_ids:
        type_one_hot = [0.0] * len(GATE_TYPE_NAMES)
        gate_index = _gate_type_index(node_by_id[node_id].node_type)
        if gate_index is not None:
            type_one_hot[gate_index] = 1.0
        layer_nodes = grouped[layers[node_id]]
        primary, secondary = positions[node_id]
        normalized_type = node_by_id[node_id].node_type.strip().lower()
        phase_one_hot = [0.0] * 4
        phase_assigned = float(node_id in node_phases)
        if node_id in node_phases:
            phase_one_hot[node_phases[node_id]] = 1.0
        node_rows.append(
            type_one_hot
            + [
                float(len(fanins[node_id])) / 4.0,
                float(len(fanouts[node_id])) / 4.0,
                float(layers[node_id]) / max(1.0, float(max_layer)),
                float(rank_by_node[node_id]) / max(1.0, float(len(layer_nodes) - 1)),
                float(primary),
                float(secondary),
                float(normalized_type in {"input", "in"}),
                float(normalized_type in {"output", "out"}),
                0.0,
            ]
            + phase_one_hot
            + [phase_assigned, 0.0]
            + [0.0, 0.0, 0.0]
        )
    if any(len(row) != NODE_FEATURE_DIM for row in node_rows):
        raise RuntimeError("offline node feature schema drifted from the graph policy")

    edge_rows = []
    max_layer_scale = max(1.0, float(max_layer))
    for source, target in edges:
        source_position = positions[source]
        target_position = positions[target]
        delta_primary = float(target_position[0] - source_position[0])
        delta_secondary = float(target_position[1] - source_position[1])
        distance = abs(delta_primary) + abs(delta_secondary)
        layer_delta = layers[target] - layers[source]
        clock_assigned = float(source in node_phases and target in node_phases)
        phase_delta = (
            (node_phases[target] - node_phases[source]) % phase_count
            if clock_assigned
            else 0
        )
        edge_rows.append(
            [
                float(layer_delta) / max_layer_scale,
                float(abs(layer_delta) > 1),
                delta_primary,
                delta_secondary,
                distance,
                float(phase_delta) / float(phase_count),
                0.0,
                0.0,
                0.0,
                clock_assigned,
            ]
        )
    edge_index = (
        torch.tensor(
            [(node_to_index[source], node_to_index[target]) for source, target in edges],
            dtype=torch.long,
        ).t().contiguous()
        if edges
        else torch.empty((2, 0), dtype=torch.long)
    )
    edge_features = (
        torch.tensor(edge_rows, dtype=torch.float32)
        if edge_rows
        else torch.empty((0, EDGE_FEATURE_DIM), dtype=torch.float32)
    )

    action_types = []
    action_rows = []
    memberships = []
    for action_index, (action_type, targets, delta) in enumerate(actions):
        target_layers = [layers[node_id] for node_id in targets]
        action_types.append(int(action_type))
        action_rows.append(
            [
                float(delta),
                float(len(targets)) / max(1.0, float(len(node_ids))),
                float(np.mean(target_layers)) / max_layer_scale if target_layers else 0.0,
                float(len(targets) > 1),
            ]
        )
        memberships.extend(
            (action_index, node_to_index[node_id])
            for node_id in sorted(set(targets))
        )
    clock_features = build_ifcn_clock_features(record)
    policy_input = GraphPolicyInput(
        node_features=torch.tensor(node_rows, dtype=torch.float32),
        edge_index=edge_index,
        edge_features=edge_features,
        clock_features=clock_features,
        action_types=torch.tensor(action_types, dtype=torch.long),
        action_features=torch.tensor(action_rows, dtype=torch.float32),
        action_target_index=(
            torch.tensor(memberships, dtype=torch.long).t().contiguous()
            if memberships
            else torch.empty((2, 0), dtype=torch.long)
        ),
        action_mask=torch.ones(len(actions), dtype=torch.bool),
        retrieval_features=(
            torch.as_tensor(retrieval_features, dtype=torch.float32)
            if retrieval_features is not None
            else torch.zeros(RETRIEVAL_FEATURE_DIM, dtype=torch.float32)
        ),
        episode_features=torch.zeros(EPISODE_FEATURE_DIM, dtype=torch.float32),
    )
    if policy_input.clock_features.shape != (CLOCK_FEATURE_DIM,):
        raise RuntimeError("offline clock feature schema drifted from the graph policy")
    if policy_input.action_features.shape[-1] != ACTION_FEATURE_DIM:
        raise RuntimeError("offline action feature schema drifted from the graph policy")
    policy_input.validate()
    return policy_input


def _action_candidates(
    node_ids: Sequence[int],
    layers: Mapping[int, int],
) -> list[tuple[LayoutActionType, tuple[int, ...], float]]:
    actions = [(LayoutActionType.NOOP, (), 0.0)]
    for node_id in node_ids:
        actions.extend(
            (
                (LayoutActionType.NODE_SHIFT, (int(node_id),), -1.0 / 6.0),
                (LayoutActionType.NODE_SHIFT, (int(node_id),), 1.0 / 6.0),
            )
        )
    grouped = {
        layer_idx: tuple(sorted(node_id for node_id in node_ids if layers[node_id] == layer_idx))
        for layer_idx in sorted(set(layers.values()))
    }
    for nodes in grouped.values():
        actions.extend(
            (
                (LayoutActionType.LAYER_PRIMARY_SHIFT, nodes, -1.0 / 6.0),
                (LayoutActionType.LAYER_PRIMARY_SHIFT, nodes, 1.0 / 6.0),
                (LayoutActionType.LAYER_SHIFT, nodes, -1.0 / 6.0),
                (LayoutActionType.LAYER_SHIFT, nodes, 1.0 / 6.0),
            )
        )
    return actions


def _position_loss(
    positions: Mapping[int, tuple[float, float]],
    target: Mapping[int, tuple[float, float]],
) -> float:
    return float(
        sum(
            abs(positions[node_id][0] - target[node_id][0])
            + abs(positions[node_id][1] - target[node_id][1])
            for node_id in target
        )
        / max(1, len(target))
    )


def _apply_offline_action(
    positions: Mapping[int, tuple[float, float]],
    action: tuple[LayoutActionType, tuple[int, ...], float],
) -> dict[int, tuple[float, float]]:
    action_type, targets, normalized_delta = action
    result = dict(positions)
    if action_type == LayoutActionType.NOOP:
        return result
    for node_id in targets:
        primary, secondary = result[node_id]
        if action_type == LayoutActionType.LAYER_PRIMARY_SHIFT:
            primary += float(normalized_delta)
        else:
            secondary += float(normalized_delta)
        result[node_id] = (primary, secondary)
    return result


def _route_targets(
    record: IFCNLayout,
    edges: Sequence[tuple[int, int]],
    transform: _CanonicalTransform,
) -> torch.Tensor:
    path_by_edge = {(int(path.source), int(path.target)): path for path in record.paths}
    if record.bounds is None:
        layout_scale = 1.0
    else:
        layout_scale = max(
            1.0,
            float(record.bounds[2] - record.bounds[0] + record.bounds[3] - record.bounds[1] + 2),
        )
    rows = []
    for edge in edges:
        path = path_by_edge[edge]
        raw_first = _DIRECTION_VECTOR.get(path.directions[0], (0.0, 0.0)) if path.directions else (0.0, 0.0)
        raw_last = _DIRECTION_VECTOR.get(path.directions[-1], (0.0, 0.0)) if path.directions else (0.0, 0.0)
        first_vector = transform.vector(*raw_first)
        last_vector = transform.vector(*raw_last)
        transformed_steps = [
            transform.vector(*_DIRECTION_VECTOR[direction])
            for direction in path.directions
            if direction in _DIRECTION_VECTOR
        ]
        primary_steps = sum(abs(vector[0]) > 0.5 for vector in transformed_steps)
        secondary_steps = sum(abs(vector[1]) > 0.5 for vector in transformed_steps)
        if path.points:
            xs = [point[0] for point in path.points]
            ys = [point[1] for point in path.points]
            bbox_area = (max(xs) - min(xs) + 1) * (max(ys) - min(ys) + 1)
        else:
            bbox_area = 0
        rows.append(
            [
                float(path.steps) / layout_scale,
                float(path.turns) / max(1.0, float(path.steps)),
                min(4.0, float(path.detour_ratio)) / 4.0,
                first_vector[0],
                first_vector[1],
                last_vector[0],
                last_vector[1],
                float(primary_steps) / max(1.0, float(path.steps)),
                float(secondary_steps) / max(1.0, float(path.steps)),
                float(bbox_area) / max(1.0, layout_scale * layout_scale),
            ]
        )
    return (
        torch.tensor(rows, dtype=torch.float32)
        if rows
        else torch.empty((0, ROUTE_AUX_FEATURE_DIM), dtype=torch.float32)
    )


def build_offline_ifcn_sample(
    record: IFCNLayout,
    *,
    best_area_for_topology: float | None = None,
    retrieval_features: Sequence[float] | torch.Tensor | None = None,
    seed: int = 0,
    perturb_scale: float = 0.30,
    teacher_tolerance: float = 1e-6,
) -> OfflineIFCNSample:
    if not record.quality.valid_for_training:
        raise ValueError(f"IFCN record is not valid for training: {record.quality.errors}")
    node_ids = sorted(int(node.node_id) for node in record.nodes)
    edges = [(int(path.source), int(path.target)) for path in record.paths]
    layers = _logic_layers(node_ids, edges)
    target, canonical_transform = _canonical_target_positions(record, node_ids, layers)
    rng = np.random.default_rng(int(seed))
    current = _perturb_positions(target, layers, rng, perturb_scale)
    actions = _action_candidates(node_ids, layers)
    current_loss = _position_loss(current, target)
    candidate_losses = np.asarray(
        [
            _position_loss(_apply_offline_action(current, action), target)
            for action in actions
        ],
        dtype=np.float64,
    )
    minimum_loss = float(candidate_losses.min())
    if minimum_loss >= current_loss - float(teacher_tolerance):
        positive_mask = np.zeros(len(actions), dtype=np.bool_)
        positive_mask[0] = True
    else:
        positive_mask = candidate_losses <= minimum_loss + float(teacher_tolerance)
        positive_mask[0] = False

    policy_input = _make_policy_input(
        record,
        node_ids,
        edges,
        layers,
        current,
        actions,
        retrieval_features,
    )
    quality_input = _make_policy_input(
        record,
        node_ids,
        edges,
        layers,
        target,
        [(LayoutActionType.NOOP, (), 0.0)],
        retrieval_features,
    )
    raw_best_area = (
        float(best_area_for_topology)
        if best_area_for_topology is not None
        else math.nan
    )
    best_area_is_valid = math.isfinite(raw_best_area) and raw_best_area > 0.0
    raw_area = (
        float(record.quality.area)
        if record.quality.area is not None
        else math.nan
    )
    area = (
        raw_area
        if math.isfinite(raw_area) and raw_area > 0.0
        else (raw_best_area if best_area_is_valid else 1.0)
    )
    best_area = raw_best_area if best_area_is_valid else area
    sample = OfflineIFCNSample(
        policy_input=policy_input,
        quality_input=quality_input,
        target_positions=torch.tensor(
            [target[node_id] for node_id in node_ids],
            dtype=torch.float32,
        ),
        target_route_features=_route_targets(record, edges, canonical_transform),
        positive_action_mask=torch.tensor(positive_mask, dtype=torch.bool),
        quality_target=float(math.log(max(1e-6, area / best_area))),
        topology_hash=record.topology_hash,
        source_path=record.source_path,
    )
    sample.validate()
    return sample


def listwise_imitation_loss(
    logits: torch.Tensor,
    positive_action_mask: torch.Tensor,
) -> torch.Tensor:
    if logits.ndim != 1 or positive_action_mask.shape != logits.shape:
        raise ValueError("listwise imitation tensors must share one action dimension")
    if not bool(torch.any(positive_action_mask)):
        raise ValueError("listwise imitation requires at least one positive action")
    log_probabilities = F.log_softmax(logits, dim=0)
    return -torch.logsumexp(log_probabilities[positive_action_mask], dim=0)


def offline_pretraining_loss(
    model: UniversalGraphPolicy,
    sample: OfflineIFCNSample,
    device: torch.device | str,
    *,
    imitation_weight: float = 1.0,
    placement_weight: float = 2.0,
    route_weight: float = 1.0,
    quality_weight: float = 0.25,
) -> tuple[torch.Tensor, dict[str, float]]:
    sample.validate()
    policy_input = sample.policy_input.to(device)
    quality_input = sample.quality_input.to(device)
    logits, _value = model(policy_input)
    predictions = model.auxiliary_predictions(policy_input)
    quality_predictions = model.auxiliary_predictions(quality_input)
    target_positions = sample.target_positions.to(device)
    target_routes = sample.target_route_features.to(device)
    positive_mask = sample.positive_action_mask.to(device)
    imitation_loss = listwise_imitation_loss(logits, positive_mask)
    placement_loss = F.smooth_l1_loss(predictions["placement"], target_positions)
    route_loss = (
        F.smooth_l1_loss(predictions["route"], target_routes)
        if target_routes.numel()
        else logits.sum() * 0.0
    )
    quality_target = torch.tensor(
        float(sample.quality_target),
        dtype=quality_predictions["quality"].dtype,
        device=device,
    )
    quality_loss = F.smooth_l1_loss(quality_predictions["quality"], quality_target)
    total = (
        float(imitation_weight) * imitation_loss
        + float(placement_weight) * placement_loss
        + float(route_weight) * route_loss
        + float(quality_weight) * quality_loss
    )
    with torch.no_grad():
        selected_action = int(torch.argmax(logits).item())
        metrics = {
            "loss": float(total.item()),
            "imitation_loss": float(imitation_loss.item()),
            "placement_loss": float(placement_loss.item()),
            "route_loss": float(route_loss.item()),
            "quality_loss": float(quality_loss.item()),
            "teacher_action_accuracy": float(bool(positive_mask[selected_action])),
            "placement_mae": float(
                torch.mean(torch.abs(predictions["placement"] - target_positions)).item()
            ),
            "route_mae": float(
                torch.mean(torch.abs(predictions["route"] - target_routes)).item()
                if target_routes.numel()
                else 0.0
            ),
        }
    return total, metrics
