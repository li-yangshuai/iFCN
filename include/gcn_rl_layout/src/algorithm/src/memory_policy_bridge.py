"""Adapters between persistent layout retrieval and the recurrent graph policy."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from typing import Any, Mapping

import numpy as np
import torch

try:
    from .layout_retrieval_memory import (
        MEMORY_CONTEXT_DIM,
        LayoutRetrievalMemory,
        graph_descriptor_from_circuit,
        topology_hash_from_circuit,
    )
    from .universal_graph_policy import build_clock_features
except ImportError:
    from layout_retrieval_memory import (
        MEMORY_CONTEXT_DIM,
        LayoutRetrievalMemory,
        graph_descriptor_from_circuit,
        topology_hash_from_circuit,
    )
    from universal_graph_policy import build_clock_features


@dataclass(frozen=True)
class FrozenPolicyMemory:
    features: torch.Tensor
    node_hints: dict[int, tuple[float, float, float]]
    entry_ids: tuple[str, ...] = ()
    topology_hash: str = ""
    exact_count: int = 0
    best_similarity: float = 0.0

    @classmethod
    def empty(cls, topology_hash: str = "") -> "FrozenPolicyMemory":
        return cls(
            features=torch.zeros(MEMORY_CONTEXT_DIM, dtype=torch.float32),
            node_hints={},
            topology_hash=str(topology_hash),
        )


def retrieve_policy_memory(
    memory: LayoutRetrievalMemory | None,
    env: Any,
    field: Any,
    *,
    top_k: int = 4,
    exclude_exact_topology: bool = False,
) -> FrozenPolicyMemory:
    topology_hash = topology_hash_from_circuit(env.circuit)
    if memory is None or int(top_k) <= 0 or len(memory) == 0:
        return FrozenPolicyMemory.empty(topology_hash)
    graph_descriptor = graph_descriptor_from_circuit(env.circuit)
    clock_descriptor = build_clock_features(env, field).detach().cpu().numpy()
    excluded = {topology_hash} if exclude_exact_topology else set()
    context, results = memory.retrieve_context(
        graph_descriptor,
        clock_descriptor=clock_descriptor,
        topology_hash=topology_hash,
        top_k=int(top_k),
        prefer_exact=not exclude_exact_topology,
        exclude_hashes=excluded,
    )
    node_hints: dict[int, tuple[float, float, float]] = {}
    exact_results = [result for result in results if result.exact_topology]
    if exact_results:
        exemplar = exact_results[0]
        confidence = float(max(0.0, min(1.0, exemplar.combined_similarity)))
        node_id_set = {int(node_id) for node_id in env.node_ids}
        for raw_node_id, coordinate in exemplar.entry.normalized_placement.items():
            try:
                node_id = int(raw_node_id)
            except (TypeError, ValueError):
                continue
            if node_id not in node_id_set:
                continue
            node_hints[node_id] = (
                2.0 * float(coordinate[0]) - 1.0,
                2.0 * float(coordinate[1]) - 1.0,
                confidence,
            )
    return FrozenPolicyMemory(
        features=torch.as_tensor(context, dtype=torch.float32),
        node_hints=node_hints,
        entry_ids=tuple(result.entry.entry_id for result in results),
        topology_hash=topology_hash,
        exact_count=len(exact_results),
        best_similarity=max((result.combined_similarity for result in results), default=0.0),
    )


def _normalized_positions(positions: Mapping[int, tuple[int, int]]) -> dict[str, tuple[float, float]]:
    if not positions:
        return {}
    xs = [float(coord[0]) for coord in positions.values()]
    ys = [float(coord[1]) for coord in positions.values()]
    minimum_x, maximum_x = min(xs), max(xs)
    minimum_y, maximum_y = min(ys), max(ys)
    scale_x = max(1.0, maximum_x - minimum_x)
    scale_y = max(1.0, maximum_y - minimum_y)
    return {
        str(node_id): (
            (float(coord[0]) - minimum_x) / scale_x,
            (float(coord[1]) - minimum_y) / scale_y,
        )
        for node_id, coord in sorted(positions.items())
    }


def remember_exact_layout(
    memory: LayoutRetrievalMemory,
    env: Any,
    field: Any,
    positions: Mapping[int, tuple[int, int]],
    quality_metrics: Mapping[str, float],
    *,
    route_hints: Any = None,
    metadata: Mapping[str, Any] | None = None,
) -> str:
    topology_hash = topology_hash_from_circuit(env.circuit)
    graph_descriptor = graph_descriptor_from_circuit(env.circuit)
    clock_descriptor = build_clock_features(env, field).detach().cpu().numpy()
    identity = {
        "topology_hash": topology_hash,
        "clock": [float(value) for value in clock_descriptor],
        "positions": {
            str(node_id): [int(coord[0]), int(coord[1])]
            for node_id, coord in sorted(positions.items())
        },
    }
    entry_id = hashlib.sha256(
        json.dumps(identity, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    return memory.add_layout(
        topology_hash=topology_hash,
        graph_descriptor=graph_descriptor,
        clock_descriptor=clock_descriptor,
        normalized_placement=_normalized_positions(positions),
        route_hints=route_hints or {},
        quality_metrics={name: float(value) for name, value in quality_metrics.items()},
        entry_id=entry_id,
        metadata=dict(metadata or {}),
    )
