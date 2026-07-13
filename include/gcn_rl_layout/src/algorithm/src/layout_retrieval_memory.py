"""Topology-aware long-term memory for reusable IFCN layouts.

The memory deliberately has no FAISS dependency.  The current IFCN corpus is
small enough for exact NumPy cosine search, while keeping the storage format
portable and deterministic.  A future ANN index can be placed behind the same
retrieval API without changing policy code.

Typical policy integration::

    graph_descriptor = graph_descriptor_from_circuit(env.circuit)
    context, memories = memory.retrieve_context(
        graph_descriptor,
        clock_descriptor=clock_features,
        topology_hash=topology_hash_from_circuit(env.circuit),
        top_k=4,
    )
    context_tensor = torch.as_tensor(context, device=device)

``context`` always has :data:`MEMORY_CONTEXT_DIM` elements by default.  It is
an aggregate, not a concatenation of examples, so model parameter shapes do
not depend on the number or size of retrieved layouts.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile
import threading
from typing import Any, Iterable, Mapping, Sequence

import numpy as np
import torch


MEMORY_SCHEMA_VERSION = 1
GRAPH_DESCRIPTOR_DIM = 32
DEFAULT_CLOCK_DESCRIPTOR_DIM = 15
MEMORY_CONTEXT_DIM = 32

_GATE_TYPES = (
    "input",
    "output",
    "maj",
    "and",
    "or",
    "not",
    "redundancy",
    "fanout",
)

# Direction is +1 when a larger metric is better and -1 when smaller is
# better.  Unknown numeric metrics are retained in storage but do not silently
# influence ranking.
DEFAULT_QUALITY_DIRECTIONS = {
    "quality_score": 1,
    "legal": 1,
    "is_legal": 1,
    "success": 1,
    "success_rate": 1,
    "routed_fraction": 1,
    "area": -1,
    "wirelength": -1,
    "wire_length": -1,
    "route_length": -1,
    "crossing": -1,
    "crossings": -1,
    "runtime": -1,
    "runtime_sec": -1,
    "failed_edges": -1,
    "direction_violations": -1,
    "clock_violations": -1,
    "phase_conflicts": -1,
    "congestion": -1,
    "critical_path": -1,
}

_LEGALITY_METRICS = {"legal", "is_legal", "success"}
_VIOLATION_METRICS = {
    "failed_edges",
    "direction_violations",
    "clock_violations",
    "phase_conflicts",
}


def _as_vector(
    value: Sequence[float] | np.ndarray | torch.Tensor,
    *,
    name: str,
    expected_dim: int | None = None,
) -> np.ndarray:
    if isinstance(value, torch.Tensor):
        value = value.detach().cpu().numpy()
    vector = np.asarray(value, dtype=np.float64).reshape(-1)
    if expected_dim is not None and vector.size != int(expected_dim):
        raise ValueError(f"{name} must have {expected_dim} elements, got {vector.size}")
    if vector.size == 0:
        raise ValueError(f"{name} must not be empty")
    if not np.isfinite(vector).all():
        raise ValueError(f"{name} must contain only finite values")
    return vector


def _json_safe(value: Any, *, path: str = "value") -> Any:
    """Convert common NumPy/Torch values and reject non-JSON-safe payloads."""

    if isinstance(value, torch.Tensor):
        value = value.detach().cpu().tolist()
    elif isinstance(value, np.ndarray):
        value = value.tolist()
    elif isinstance(value, np.generic):
        value = value.item()

    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, float):
        if not math.isfinite(value):
            raise ValueError(f"{path} contains a non-finite float")
        return float(value)
    if isinstance(value, Mapping):
        return {
            str(key): _json_safe(item, path=f"{path}.{key}")
            for key, item in sorted(value.items(), key=lambda pair: str(pair[0]))
        }
    if isinstance(value, (list, tuple)):
        return [_json_safe(item, path=f"{path}[{index}]") for index, item in enumerate(value)]
    raise TypeError(f"{path} contains unsupported type {type(value).__name__}")


def _canonical_json(value: Any) -> str:
    return json.dumps(_json_safe(value), sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _stable_digest(value: Any) -> str:
    """Match ``ifcn_layout_dataset._stable_digest`` byte for byte."""

    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _cosine(left: np.ndarray, right: np.ndarray) -> float:
    denominator = float(np.linalg.norm(left) * np.linalg.norm(right))
    if denominator <= 1e-12:
        return 0.0
    return float(np.clip(np.dot(left, right) / denominator, -1.0, 1.0))


def _record_value(record: Any, names: Sequence[str], default: Any = None) -> Any:
    for name in names:
        if isinstance(record, Mapping) and name in record:
            return record[name]
        if hasattr(record, name):
            return getattr(record, name)
    return default


def _normalise_edges(edges: Any) -> list[tuple[Any, Any]]:
    if edges is None:
        return []
    if isinstance(edges, torch.Tensor):
        edges = edges.detach().cpu().numpy()
    array = np.asarray(edges, dtype=object)
    if array.ndim == 2 and array.shape[0] == 2 and array.shape[1] != 2:
        array = array.T
    if array.size == 0:
        return []
    if array.ndim != 2 or array.shape[1] != 2:
        raise ValueError("edges must be an [E, 2] or [2, E] collection")
    return [(row[0].item() if hasattr(row[0], "item") else row[0],
             row[1].item() if hasattr(row[1], "item") else row[1]) for row in array]


def _extract_graph(record: Any) -> tuple[list[Any], list[tuple[Any, Any]], dict[Any, str], dict[Any, int]]:
    """Extract a minimal directed graph from a CircuitParser or plain record."""

    raw_nodes = _record_value(record, ("node_ids", "effective_nodes", "nodes"), None)
    raw_edges = _record_value(record, ("effective_edges", "edges", "edge_index"), [])
    edges = _normalise_edges(raw_edges)

    node_types: dict[Any, str] = {}
    node_layers: dict[Any, int] = {}
    if isinstance(raw_nodes, Mapping):
        nodes = list(raw_nodes.keys())
        for node_id, payload in raw_nodes.items():
            if isinstance(payload, Mapping):
                node_types[node_id] = str(payload.get("type", payload.get("node_type", "unknown"))).lower()
                if "layer" in payload:
                    node_layers[node_id] = int(payload["layer"])
    elif raw_nodes is None:
        nodes = []
    else:
        nodes = []
        for index, payload in enumerate(list(raw_nodes)):
            if isinstance(payload, Mapping):
                node_id = payload.get("id", payload.get("node_id", index))
                node_types[node_id] = str(payload.get("type", payload.get("node_type", "unknown"))).lower()
                if "layer" in payload:
                    node_layers[node_id] = int(payload["layer"])
                nodes.append(node_id)
            elif hasattr(payload, "node_id"):
                node_id = getattr(payload, "node_id")
                nodes.append(node_id)
                node_types[node_id] = str(
                    getattr(payload, "node_type", getattr(payload, "type", "unknown"))
                ).lower()
                if hasattr(payload, "layer"):
                    node_layers[node_id] = int(getattr(payload, "layer"))
            else:
                nodes.append(payload.item() if hasattr(payload, "item") else payload)

    # Parsed IFCN layouts expose routed ``paths`` instead of a separate edge
    # list.  Only their endpoints define circuit topology; waypoints do not.
    if not edges:
        raw_paths = _record_value(record, ("paths",), ())
        for path in raw_paths:
            if isinstance(path, Mapping):
                source, target = path.get("source"), path.get("target")
            else:
                source, target = getattr(path, "source", None), getattr(path, "target", None)
            if source is not None and target is not None:
                edges.append((source, target))

    if not nodes:
        nodes = sorted({node for edge in edges for node in edge}, key=str)
    else:
        # Match IFCN dataset integrity semantics: paths with an endpoint not in
        # the declared node table are invalid samples, not implicit new gates.
        known = set(nodes)
        edges = [edge for edge in edges if edge[0] in known and edge[1] in known]

    explicit_types = _record_value(record, ("node_types", "types"), None)
    if isinstance(explicit_types, Mapping):
        node_types.update({node_id: str(value).lower() for node_id, value in explicit_types.items()})
    type_getter = getattr(record, "getNodeTypeString", None)
    if callable(type_getter):
        for node_id in nodes:
            try:
                node_types[node_id] = str(type_getter(node_id)).lower()
            except Exception:
                node_types.setdefault(node_id, "unknown")

    raw_layers = _record_value(record, ("ordered_layers", "layer_nodes", "layers"), None)
    if isinstance(raw_layers, Mapping):
        for node_id, layer in raw_layers.items():
            node_layers[node_id] = int(layer)
    elif raw_layers is not None:
        for layer_index, layer_nodes in enumerate(raw_layers):
            for node_id in layer_nodes:
                node_layers[node_id] = int(layer_index)

    # Fall back to a Kahn-style longest-path depth.  Cyclic/unresolved nodes get
    # depth zero and are reflected by the backward-edge descriptor feature.
    if len(node_layers) < len(nodes):
        incoming: dict[Any, list[Any]] = {node: [] for node in nodes}
        outgoing: dict[Any, list[Any]] = {node: [] for node in nodes}
        for source, target in edges:
            outgoing.setdefault(source, []).append(target)
            incoming.setdefault(target, []).append(source)
        indegree = {node: len(incoming.get(node, ())) for node in nodes}
        queue = sorted((node for node in nodes if indegree[node] == 0), key=str)
        inferred = {node: 0 for node in nodes}
        cursor = 0
        while cursor < len(queue):
            source = queue[cursor]
            cursor += 1
            for target in outgoing.get(source, ()):
                inferred[target] = max(inferred.get(target, 0), inferred[source] + 1)
                indegree[target] -= 1
                if indegree[target] == 0:
                    queue.append(target)
        for node_id in nodes:
            node_layers.setdefault(node_id, inferred.get(node_id, 0))

    return nodes, edges, node_types, node_layers


def graph_descriptor_from_circuit(
    circuit_or_record: Any,
    descriptor_dim: int = GRAPH_DESCRIPTOR_DIM,
) -> np.ndarray:
    """Build a node-ID-invariant structural summary for retrieval.

    A mapping containing an explicit ``graph_descriptor`` is accepted as an
    escape hatch for a learned GNN embedding.  Otherwise the descriptor uses
    graph size, directed degree statistics, gate proportions and layer/span
    statistics.  It is cheap enough to compute for every query.
    """

    descriptor_dim = int(descriptor_dim)
    if descriptor_dim <= 0:
        raise ValueError("descriptor_dim must be positive")
    explicit = _record_value(circuit_or_record, ("graph_descriptor",), None)
    if explicit is not None:
        return _as_vector(explicit, name="graph_descriptor", expected_dim=descriptor_dim).astype(np.float32)

    nodes, edges, node_types, layers = _extract_graph(circuit_or_record)
    if not nodes:
        raise ValueError("cannot describe an empty circuit")
    node_count = len(nodes)
    edge_count = len(edges)
    indegree = {node: 0 for node in nodes}
    outdegree = {node: 0 for node in nodes}
    for source, target in edges:
        outdegree[source] = outdegree.get(source, 0) + 1
        indegree[target] = indegree.get(target, 0) + 1
    in_values = np.asarray([indegree[node] for node in nodes], dtype=np.float64)
    out_values = np.asarray([outdegree[node] for node in nodes], dtype=np.float64)

    layer_values = np.asarray([layers.get(node, 0) for node in nodes], dtype=np.float64)
    layer_count = int(layer_values.max()) + 1 if layer_values.size else 1
    layer_widths = np.bincount(layer_values.astype(np.int64), minlength=max(1, layer_count)).astype(np.float64)
    edge_spans = np.asarray(
        [layers.get(target, 0) - layers.get(source, 0) for source, target in edges],
        dtype=np.float64,
    )
    positive_scale = max(1.0, float(layer_count - 1))

    type_fractions = []
    canonical_types = {node: str(node_types.get(node, "unknown")).lower() for node in nodes}
    for gate_type in _GATE_TYPES:
        type_fractions.append(
            sum(gate_type in canonical_types[node] for node in nodes) / float(node_count)
        )

    base = np.asarray(
        [
            math.log1p(node_count) / 8.0,
            math.log1p(edge_count) / 8.0,
            edge_count / float(max(1, node_count * max(1, node_count - 1))),
            float(np.mean(in_values == 0)),
            float(np.mean(out_values == 0)),
            float(np.mean((in_values + out_values) == 0)),
            float(in_values.mean()) / 4.0,
            float(in_values.std()) / 4.0,
            float(in_values.max(initial=0.0)) / 8.0,
            float(out_values.mean()) / 4.0,
            float(out_values.std()) / 4.0,
            float(out_values.max(initial=0.0)) / 8.0,
            float(np.mean(out_values > 1)),
            float(np.mean(in_values > 1)),
            *type_fractions,
            math.log1p(layer_count) / 6.0,
            float(layer_widths.max(initial=0.0)) / float(node_count),
            float(layer_widths.mean()) / float(node_count),
            float(layer_widths.std() / max(1e-8, layer_widths.mean())),
            float(np.mean(edge_spans > 0)) if edge_spans.size else 0.0,
            float(np.mean(edge_spans == 0)) if edge_spans.size else 0.0,
            float(np.mean(edge_spans > 1)) if edge_spans.size else 0.0,
            float(np.mean(np.abs(edge_spans))) / positive_scale if edge_spans.size else 0.0,
            float(np.std(edge_spans)) / positive_scale if edge_spans.size else 0.0,
            float(np.mean(edge_spans < 0)) if edge_spans.size else 0.0,
        ],
        dtype=np.float64,
    )
    # The documented schema is 32-D.  Supporting another fixed output size is
    # useful for ablations and still deterministic: truncate or zero-pad.
    output = np.zeros(descriptor_dim, dtype=np.float32)
    output[: min(descriptor_dim, base.size)] = base[:descriptor_dim].astype(np.float32)
    return output


def topology_hash_from_circuit(circuit_or_record: Any) -> str:
    """Return the exact hash used by ``ifcn_layout_dataset.topology_fingerprint``.

    Keeping this implementation byte-compatible is important: offline IFCN
    entries and online ``CircuitParser`` queries must meet in the exact-match
    index rather than accidentally falling back to approximate retrieval.
    """

    explicit = _record_value(circuit_or_record, ("topology_hash",), None)
    if explicit is not None:
        value = str(explicit).strip()
        if not value:
            raise ValueError("topology_hash must not be empty")
        return value

    nodes, edges, node_types, _ = _extract_graph(circuit_or_record)
    if not nodes:
        raise ValueError("cannot hash an empty circuit")
    normalised_types = {
        node: " ".join(str(node_types.get(node, "unknown")).strip().lower().split()) or "unknown"
        for node in nodes
    }
    incoming: dict[Any, list[Any]] = {node: [] for node in nodes}
    outgoing: dict[Any, list[Any]] = {node: [] for node in nodes}
    valid_edges: list[tuple[Any, Any]] = []
    for source, target in edges:
        if source not in normalised_types or target not in normalised_types:
            continue
        outgoing[source].append(target)
        incoming[target].append(source)
        valid_edges.append((source, target))
    colours = {
        node: _stable_digest(
            [normalised_types[node], len(incoming[node]), len(outgoing[node])]
        )
        for node in nodes
    }
    round_histograms: list[list[tuple[str, int]]] = [
        sorted(Counter(colours.values()).items())
    ]
    rounds = min(max(len(nodes), 1), 16)
    for _ in range(rounds):
        colours = {
            node: _stable_digest(
                [
                    normalised_types[node],
                    colours[node],
                    sorted(colours[parent] for parent in incoming[node]),
                    sorted(colours[child] for child in outgoing[node]),
                ]
            )
            for node in nodes
        }
        round_histograms.append(sorted(Counter(colours.values()).items()))
    signature = {
        "node_count": len(nodes),
        "edge_count": len(valid_edges),
        "round_histograms": round_histograms,
        "final_edges": sorted((colours[source], colours[target]) for source, target in valid_edges),
    }
    return _stable_digest(signature)


@dataclass(frozen=True)
class LayoutMemoryEntry:
    """One reusable layout example in normalized, circuit-size-free form."""

    topology_hash: str
    graph_descriptor: tuple[float, ...]
    clock_descriptor: tuple[float, ...]
    normalized_placement: dict[str, tuple[float, float]]
    route_hints: Any
    quality_metrics: dict[str, float]
    entry_id: str = ""
    metadata: dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        topology_hash = str(self.topology_hash).strip()
        if not topology_hash:
            raise ValueError("topology_hash must not be empty")
        graph = tuple(float(value) for value in _as_vector(self.graph_descriptor, name="graph_descriptor"))
        clock = tuple(float(value) for value in _as_vector(self.clock_descriptor, name="clock_descriptor"))

        if not isinstance(self.normalized_placement, Mapping):
            raise TypeError("normalized_placement must map node identifiers to (x, y)")
        placement: dict[str, tuple[float, float]] = {}
        for node_id, coordinate in sorted(self.normalized_placement.items(), key=lambda item: str(item[0])):
            vector = _as_vector(coordinate, name=f"placement[{node_id}]", expected_dim=2)
            placement[str(node_id)] = (float(vector[0]), float(vector[1]))

        if not isinstance(self.quality_metrics, Mapping):
            raise TypeError("quality_metrics must be a numeric mapping")
        quality: dict[str, float] = {}
        for name, value in sorted(self.quality_metrics.items(), key=lambda item: str(item[0])):
            numeric = float(value)
            if not math.isfinite(numeric):
                raise ValueError(f"quality metric {name!r} is not finite")
            quality[str(name)] = numeric

        route_hints = _json_safe(self.route_hints, path="route_hints")
        metadata = _json_safe(self.metadata, path="metadata")
        if not isinstance(metadata, dict):
            raise TypeError("metadata must be a mapping")
        identifier = str(self.entry_id).strip()
        if not identifier:
            identity_payload = {
                "topology_hash": topology_hash,
                "graph_descriptor": graph,
                "clock_descriptor": clock,
                "normalized_placement": placement,
                "route_hints": route_hints,
                "quality_metrics": quality,
            }
            identifier = hashlib.sha256(_canonical_json(identity_payload).encode("utf-8")).hexdigest()

        object.__setattr__(self, "topology_hash", topology_hash)
        object.__setattr__(self, "graph_descriptor", graph)
        object.__setattr__(self, "clock_descriptor", clock)
        object.__setattr__(self, "normalized_placement", placement)
        object.__setattr__(self, "route_hints", route_hints)
        object.__setattr__(self, "quality_metrics", quality)
        object.__setattr__(self, "entry_id", identifier)
        object.__setattr__(self, "metadata", metadata)

    @classmethod
    def create(
        cls,
        *,
        topology_hash: str,
        graph_descriptor: Sequence[float] | np.ndarray | torch.Tensor,
        clock_descriptor: Sequence[float] | np.ndarray | torch.Tensor,
        normalized_placement: Mapping[Any, Sequence[float]],
        route_hints: Any,
        quality_metrics: Mapping[str, float],
        entry_id: str = "",
        metadata: Mapping[str, Any] | None = None,
    ) -> "LayoutMemoryEntry":
        return cls(
            topology_hash=topology_hash,
            graph_descriptor=tuple(_as_vector(graph_descriptor, name="graph_descriptor")),
            clock_descriptor=tuple(_as_vector(clock_descriptor, name="clock_descriptor")),
            normalized_placement={str(key): tuple(value) for key, value in normalized_placement.items()},
            route_hints=route_hints,
            quality_metrics=dict(quality_metrics),
            entry_id=entry_id,
            metadata=dict(metadata or {}),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "entry_id": self.entry_id,
            "topology_hash": self.topology_hash,
            "graph_descriptor": list(self.graph_descriptor),
            "clock_descriptor": list(self.clock_descriptor),
            "normalized_placement": {
                node_id: [coordinate[0], coordinate[1]]
                for node_id, coordinate in self.normalized_placement.items()
            },
            "route_hints": _json_safe(self.route_hints),
            "quality_metrics": dict(self.quality_metrics),
            "metadata": _json_safe(self.metadata),
        }

    @classmethod
    def from_dict(cls, payload: Mapping[str, Any]) -> "LayoutMemoryEntry":
        return cls.create(
            topology_hash=str(payload["topology_hash"]),
            graph_descriptor=payload["graph_descriptor"],
            clock_descriptor=payload["clock_descriptor"],
            normalized_placement=payload.get("normalized_placement", {}),
            route_hints=payload.get("route_hints", {}),
            quality_metrics=payload.get("quality_metrics", {}),
            entry_id=str(payload.get("entry_id", "")),
            metadata=payload.get("metadata", {}),
        )


@dataclass(frozen=True)
class RetrievedLayout:
    entry: LayoutMemoryEntry
    graph_similarity: float
    clock_similarity: float
    combined_similarity: float
    quality_score: float
    selection_score: float
    exact_topology: bool


class LayoutRetrievalMemory:
    """Incremental topology-aware layout store with exact cosine retrieval."""

    def __init__(
        self,
        graph_descriptor_dim: int = GRAPH_DESCRIPTOR_DIM,
        clock_descriptor_dim: int = DEFAULT_CLOCK_DESCRIPTOR_DIM,
        context_dim: int = MEMORY_CONTEXT_DIM,
        *,
        quality_directions: Mapping[str, int] | None = None,
        quality_weights: Mapping[str, float] | None = None,
    ) -> None:
        self.graph_descriptor_dim = int(graph_descriptor_dim)
        self.clock_descriptor_dim = int(clock_descriptor_dim)
        self.context_dim = int(context_dim)
        if self.graph_descriptor_dim <= 0 or self.clock_descriptor_dim <= 0:
            raise ValueError("descriptor dimensions must be positive")
        if self.context_dim < 16:
            raise ValueError("context_dim must be at least 16")
        directions = dict(DEFAULT_QUALITY_DIRECTIONS)
        if quality_directions:
            for name, direction in quality_directions.items():
                if int(direction) not in (-1, 1):
                    raise ValueError("quality directions must be -1 or +1")
                directions[str(name)] = int(direction)
        self.quality_directions = directions
        self.quality_weights = {
            str(name): float(weight) for name, weight in (quality_weights or {}).items()
        }
        if any(not math.isfinite(weight) or weight < 0 for weight in self.quality_weights.values()):
            raise ValueError("quality weights must be finite and non-negative")
        self._entries: dict[str, LayoutMemoryEntry] = {}
        self._by_topology: dict[str, set[str]] = {}
        # Layout payload hashing is the expensive part of MMR selection.  A
        # memory entry is immutable from the index's point of view, so cache
        # its diversity embedding until that entry ID is explicitly replaced.
        # Keeping the entry alongside the vector also prevents a concurrent
        # replacement from publishing a stale in-flight computation.
        self._diversity_embedding_cache: dict[
            str, tuple[LayoutMemoryEntry, np.ndarray]
        ] = {}
        self._lock = threading.RLock()

    def __len__(self) -> int:
        with self._lock:
            return len(self._entries)

    @property
    def topology_count(self) -> int:
        with self._lock:
            return len(self._by_topology)

    @property
    def entries(self) -> tuple[LayoutMemoryEntry, ...]:
        with self._lock:
            return tuple(self._entries[key] for key in sorted(self._entries))

    def _validate_entry(self, entry: LayoutMemoryEntry) -> None:
        if len(entry.graph_descriptor) != self.graph_descriptor_dim:
            raise ValueError(
                f"entry graph descriptor has {len(entry.graph_descriptor)} elements; "
                f"memory expects {self.graph_descriptor_dim}"
            )
        if len(entry.clock_descriptor) != self.clock_descriptor_dim:
            raise ValueError(
                f"entry clock descriptor has {len(entry.clock_descriptor)} elements; "
                f"memory expects {self.clock_descriptor_dim}"
            )

    def add(self, entry: LayoutMemoryEntry, *, replace_existing: bool = False) -> str:
        """Add one entry; identical deterministic IDs make ingestion idempotent."""

        if not isinstance(entry, LayoutMemoryEntry):
            raise TypeError("entry must be a LayoutMemoryEntry")
        self._validate_entry(entry)
        with self._lock:
            previous = self._entries.get(entry.entry_id)
            if previous is not None:
                if previous == entry:
                    return entry.entry_id
                if not replace_existing:
                    raise ValueError(f"entry_id {entry.entry_id!r} already exists")
                self._by_topology[previous.topology_hash].discard(previous.entry_id)
                if not self._by_topology[previous.topology_hash]:
                    del self._by_topology[previous.topology_hash]
            self._diversity_embedding_cache.pop(entry.entry_id, None)
            self._entries[entry.entry_id] = entry
            self._by_topology.setdefault(entry.topology_hash, set()).add(entry.entry_id)
        return entry.entry_id

    def add_layout(self, **kwargs: Any) -> str:
        """Construct and incrementally add :class:`LayoutMemoryEntry`."""

        replace_existing = bool(kwargs.pop("replace_existing", False))
        return self.add(LayoutMemoryEntry.create(**kwargs), replace_existing=replace_existing)

    def extend(self, entries: Iterable[LayoutMemoryEntry], *, replace_existing: bool = False) -> list[str]:
        return [self.add(entry, replace_existing=replace_existing) for entry in entries]

    @staticmethod
    def _excluded(
        entry: LayoutMemoryEntry,
        exclude_hashes: set[str],
        exclude_entry_ids: set[str],
    ) -> bool:
        # ``exclude_hashes`` intentionally covers both hash namespaces.  This
        # makes a held-out split safe even if a caller passes entry hashes and
        # topology hashes in a single manifest.
        return (
            entry.topology_hash in exclude_hashes
            or entry.entry_id in exclude_hashes
            or entry.entry_id in exclude_entry_ids
        )

    def _quality_scores(self, entries: Sequence[LayoutMemoryEntry]) -> dict[str, float]:
        if not entries:
            return {}
        metric_names = sorted(
            {
                name
                for entry in entries
                for name in entry.quality_metrics
                if name in self.quality_directions
            }
        )
        accumulated = {entry.entry_id: 0.0 for entry in entries}
        total_weights = {entry.entry_id: 0.0 for entry in entries}
        for name in metric_names:
            present = [(entry, entry.quality_metrics[name]) for entry in entries if name in entry.quality_metrics]
            if not present:
                continue
            values = np.asarray([value for _, value in present], dtype=np.float64)
            direction = self.quality_directions[name]
            span = float(values.max() - values.min())
            if span <= 1e-12:
                if name in _LEGALITY_METRICS:
                    utilities = np.clip(values, 0.0, 1.0)
                elif name in _VIOLATION_METRICS:
                    utilities = 1.0 / (1.0 + np.maximum(0.0, values))
                else:
                    utilities = np.full(values.shape, 0.5)
            elif direction > 0:
                utilities = (values - values.min()) / span
            else:
                utilities = (values.max() - values) / span
            weight = self.quality_weights.get(name, 4.0 if name in (_LEGALITY_METRICS | _VIOLATION_METRICS) else 1.0)
            for (entry, _), utility in zip(present, utilities):
                accumulated[entry.entry_id] += weight * float(utility)
                total_weights[entry.entry_id] += weight

        output: dict[str, float] = {}
        for entry in entries:
            base = (
                accumulated[entry.entry_id] / total_weights[entry.entry_id]
                if total_weights[entry.entry_id] > 0
                else 0.5
            )
            legality_known = False
            legal = True
            for name in _LEGALITY_METRICS:
                if name in entry.quality_metrics:
                    legality_known = True
                    legal = legal and entry.quality_metrics[name] > 0.5
            for name in _VIOLATION_METRICS:
                if name in entry.quality_metrics:
                    legality_known = True
                    legal = legal and entry.quality_metrics[name] <= 0.0
            # Legal layouts always outrank illegal ones at equal relevance;
            # within each group, area/runtime/etc. still determine order.
            output[entry.entry_id] = (0.65 * float(legal) + 0.35 * base) if legality_known else base
        return output

    @staticmethod
    def _hash_features(payload: Any, dimension: int) -> np.ndarray:
        features = np.zeros(int(dimension), dtype=np.float64)
        if dimension <= 0:
            return features

        def add(path: str, value: float = 1.0) -> None:
            digest = hashlib.blake2b(path.encode("utf-8"), digest_size=16).digest()
            index = int.from_bytes(digest[:8], "little") % dimension
            sign = 1.0 if (digest[8] & 1) == 0 else -1.0
            features[index] += sign * value

        def visit(value: Any, path: str) -> None:
            if isinstance(value, Mapping):
                for key, child in sorted(value.items(), key=lambda pair: str(pair[0])):
                    visit(child, f"{path}.{key}")
            elif isinstance(value, (list, tuple)):
                for index, child in enumerate(value):
                    visit(child, f"{path}[{index}]")
            elif isinstance(value, bool):
                add(path, float(value))
            elif isinstance(value, (int, float)):
                numeric = float(value)
                add(path, math.copysign(math.log1p(abs(numeric)), numeric) if numeric else 0.0)
            elif value is not None:
                add(f"{path}={value}")

        visit(payload, "memory")
        return features

    def _diversity_embedding(self, entry: LayoutMemoryEntry) -> np.ndarray:
        with self._lock:
            cached = self._diversity_embedding_cache.get(entry.entry_id)
            if cached is not None and cached[0] == entry:
                return cached[1]

        graph = np.asarray(entry.graph_descriptor, dtype=np.float64)
        graph /= max(1e-12, float(np.linalg.norm(graph)))
        layout = self._hash_features(
            {
                "placement": entry.normalized_placement,
                "routes": entry.route_hints,
            },
            64,
        )
        layout /= max(1e-12, float(np.linalg.norm(layout)))
        embedding = np.concatenate((0.35 * graph, layout))
        embedding /= max(1e-12, float(np.linalg.norm(embedding)))
        embedding.setflags(write=False)

        with self._lock:
            # The candidate snapshot may race with replace_existing.  Only
            # publish the computed vector when the indexed entry is still the
            # same value; returning it remains correct for the old snapshot.
            if self._entries.get(entry.entry_id) == entry:
                existing = self._diversity_embedding_cache.get(entry.entry_id)
                if existing is None or existing[0] != entry:
                    self._diversity_embedding_cache[entry.entry_id] = (entry, embedding)
                else:
                    embedding = existing[1]
        return embedding

    def _select(
        self,
        candidates: Sequence[RetrievedLayout],
        *,
        top_k: int,
        quality_weight: float,
        diversity_weight: float,
    ) -> list[RetrievedLayout]:
        if top_k <= 0 or not candidates:
            return []
        if not 0.0 <= quality_weight <= 1.0:
            raise ValueError("quality_weight must be in [0, 1]")
        if not 0.0 <= diversity_weight <= 1.0:
            raise ValueError("diversity_weight must be in [0, 1]")
        needs_diversity = diversity_weight > 0.0 and int(top_k) > 1
        embeddings = (
            {
                candidate.entry.entry_id: self._diversity_embedding(candidate.entry)
                for candidate in candidates
            }
            if needs_diversity
            else {}
        )
        remaining = {candidate.entry.entry_id: candidate for candidate in candidates}
        selected: list[RetrievedLayout] = []
        while remaining and len(selected) < int(top_k):
            best: RetrievedLayout | None = None
            best_score = -math.inf
            for entry_id in sorted(remaining):
                candidate = remaining[entry_id]
                similarity_01 = 0.5 * (candidate.combined_similarity + 1.0)
                relevance = (
                    (1.0 - quality_weight) * similarity_01
                    + quality_weight * candidate.quality_score
                )
                redundancy = 0.0
                if selected and needs_diversity:
                    redundancy = max(
                        max(0.0, _cosine(embeddings[entry_id], embeddings[item.entry.entry_id]))
                        for item in selected
                    )
                score = relevance - diversity_weight * redundancy
                if score > best_score + 1e-12:
                    best_score = score
                    best = candidate
            assert best is not None
            selected.append(replace(best, selection_score=float(best_score)))
            del remaining[best.entry.entry_id]
        return selected

    def retrieve_exact(
        self,
        topology_hash: str,
        *,
        clock_descriptor: Sequence[float] | np.ndarray | torch.Tensor | None = None,
        top_k: int = 4,
        exclude_hashes: Iterable[str] = (),
        exclude_entry_ids: Iterable[str] = (),
        quality_weight: float = 0.45,
        diversity_weight: float = 0.15,
        clock_weight: float = 0.2,
    ) -> list[RetrievedLayout]:
        """Retrieve layouts with exactly the same canonical topology hash."""

        topology_hash = str(topology_hash)
        if not 0.0 <= clock_weight <= 1.0:
            raise ValueError("clock_weight must be in [0, 1]")
        query_clock = (
            _as_vector(clock_descriptor, name="clock_descriptor", expected_dim=self.clock_descriptor_dim)
            if clock_descriptor is not None
            else None
        )
        excluded_hashes = {str(value) for value in exclude_hashes}
        excluded_ids = {str(value) for value in exclude_entry_ids}
        with self._lock:
            entries = [
                self._entries[entry_id]
                for entry_id in sorted(self._by_topology.get(topology_hash, ()))
                if not self._excluded(self._entries[entry_id], excluded_hashes, excluded_ids)
            ]
        quality = self._quality_scores(entries)
        candidates = []
        for entry in entries:
            clock_similarity = (
                _cosine(query_clock, np.asarray(entry.clock_descriptor, dtype=np.float64))
                if query_clock is not None
                else 0.0
            )
            combined = (1.0 - clock_weight) + clock_weight * clock_similarity if query_clock is not None else 1.0
            candidates.append(
                RetrievedLayout(
                    entry=entry,
                    graph_similarity=1.0,
                    clock_similarity=clock_similarity,
                    combined_similarity=combined,
                    quality_score=quality[entry.entry_id],
                    selection_score=0.0,
                    exact_topology=True,
                )
            )
        return self._select(
            candidates,
            top_k=int(top_k),
            quality_weight=float(quality_weight),
            diversity_weight=float(diversity_weight),
        )

    def retrieve_similar(
        self,
        graph_descriptor: Sequence[float] | np.ndarray | torch.Tensor,
        *,
        clock_descriptor: Sequence[float] | np.ndarray | torch.Tensor | None = None,
        topology_hash: str | None = None,
        top_k: int = 4,
        exclude_hashes: Iterable[str] = (),
        exclude_entry_ids: Iterable[str] = (),
        include_exact: bool = True,
        min_similarity: float = -1.0,
        quality_weight: float = 0.3,
        diversity_weight: float = 0.15,
        clock_weight: float = 0.2,
    ) -> list[RetrievedLayout]:
        """Cosine Top-K retrieval followed by quality/diversity-aware MMR."""

        query_graph = _as_vector(
            graph_descriptor,
            name="graph_descriptor",
            expected_dim=self.graph_descriptor_dim,
        )
        query_clock = (
            _as_vector(clock_descriptor, name="clock_descriptor", expected_dim=self.clock_descriptor_dim)
            if clock_descriptor is not None
            else None
        )
        if not -1.0 <= float(min_similarity) <= 1.0:
            raise ValueError("min_similarity must be in [-1, 1]")
        if not 0.0 <= clock_weight <= 1.0:
            raise ValueError("clock_weight must be in [0, 1]")
        excluded_hashes = {str(value) for value in exclude_hashes}
        excluded_ids = {str(value) for value in exclude_entry_ids}
        with self._lock:
            entries = [
                entry
                for entry in self._entries.values()
                if not self._excluded(entry, excluded_hashes, excluded_ids)
                and (include_exact or topology_hash is None or entry.topology_hash != topology_hash)
            ]
        quality = self._quality_scores(entries)
        candidates: list[RetrievedLayout] = []
        for entry in entries:
            graph_similarity = _cosine(
                query_graph,
                np.asarray(entry.graph_descriptor, dtype=np.float64),
            )
            if graph_similarity < float(min_similarity):
                continue
            clock_similarity = (
                _cosine(query_clock, np.asarray(entry.clock_descriptor, dtype=np.float64))
                if query_clock is not None
                else 0.0
            )
            combined = (
                (1.0 - clock_weight) * graph_similarity + clock_weight * clock_similarity
                if query_clock is not None
                else graph_similarity
            )
            candidates.append(
                RetrievedLayout(
                    entry=entry,
                    graph_similarity=graph_similarity,
                    clock_similarity=clock_similarity,
                    combined_similarity=combined,
                    quality_score=quality[entry.entry_id],
                    selection_score=0.0,
                    exact_topology=topology_hash is not None and entry.topology_hash == topology_hash,
                )
            )
        return self._select(
            candidates,
            top_k=int(top_k),
            quality_weight=float(quality_weight),
            diversity_weight=float(diversity_weight),
        )

    def retrieve(
        self,
        graph_descriptor: Sequence[float] | np.ndarray | torch.Tensor,
        *,
        clock_descriptor: Sequence[float] | np.ndarray | torch.Tensor | None = None,
        topology_hash: str | None = None,
        top_k: int = 4,
        prefer_exact: bool = True,
        exclude_hashes: Iterable[str] = (),
        exclude_entry_ids: Iterable[str] = (),
        quality_weight: float = 0.35,
        diversity_weight: float = 0.15,
        clock_weight: float = 0.2,
        min_similarity: float = -1.0,
    ) -> list[RetrievedLayout]:
        """Retrieve exact-topology memories first, then fill from neighbours."""

        top_k = int(top_k)
        if top_k <= 0:
            return []
        excluded_hashes = {str(value) for value in exclude_hashes}
        excluded_ids = {str(value) for value in exclude_entry_ids}
        selected: list[RetrievedLayout] = []
        if topology_hash is not None and prefer_exact:
            selected = self.retrieve_exact(
                topology_hash,
                clock_descriptor=clock_descriptor,
                top_k=top_k,
                exclude_hashes=excluded_hashes,
                exclude_entry_ids=excluded_ids,
                quality_weight=quality_weight,
                diversity_weight=diversity_weight,
                clock_weight=clock_weight,
            )
        if len(selected) >= top_k:
            return selected
        selected_ids = excluded_ids | {result.entry.entry_id for result in selected}
        # If all exact entries have already been considered, exclude that
        # topology while filling to prevent it from bypassing exact selection.
        fill_excluded_hashes = set(excluded_hashes)
        if topology_hash is not None and prefer_exact:
            fill_excluded_hashes.add(str(topology_hash))
        selected.extend(
            self.retrieve_similar(
                graph_descriptor,
                clock_descriptor=clock_descriptor,
                topology_hash=topology_hash,
                top_k=top_k - len(selected),
                exclude_hashes=fill_excluded_hashes,
                exclude_entry_ids=selected_ids,
                include_exact=not prefer_exact,
                min_similarity=min_similarity,
                quality_weight=quality_weight,
                diversity_weight=diversity_weight,
                clock_weight=clock_weight,
            )
        )
        return selected

    @staticmethod
    def _leaf_count(payload: Any) -> int:
        if isinstance(payload, Mapping):
            return sum(LayoutRetrievalMemory._leaf_count(value) for value in payload.values())
        if isinstance(payload, (list, tuple)):
            return sum(LayoutRetrievalMemory._leaf_count(value) for value in payload)
        return 1

    def aggregate_context(self, results: Sequence[RetrievedLayout]) -> np.ndarray:
        """Aggregate a ragged result set into a deterministic fixed-size vector."""

        context = np.zeros(self.context_dim, dtype=np.float32)
        if not results:
            return context
        scores = np.asarray([result.selection_score for result in results], dtype=np.float64)
        scores -= scores.max(initial=0.0)
        weights = np.exp(np.clip(scores, -40.0, 0.0))
        weights /= max(1e-12, float(weights.sum()))
        similarities = np.asarray([result.combined_similarity for result in results], dtype=np.float64)
        clock_similarities = np.asarray([result.clock_similarity for result in results], dtype=np.float64)
        qualities = np.asarray([result.quality_score for result in results], dtype=np.float64)
        route_counts = np.asarray([self._leaf_count(result.entry.route_hints) for result in results], dtype=np.float64)
        exact = np.asarray([result.exact_topology for result in results], dtype=np.float64)

        widths = []
        heights = []
        densities = []
        legalities = []
        for result in results:
            coordinates = np.asarray(list(result.entry.normalized_placement.values()), dtype=np.float64)
            if coordinates.size:
                width = float(np.ptp(coordinates[:, 0]))
                height = float(np.ptp(coordinates[:, 1]))
                area = max(1e-6, width * height)
                density = len(coordinates) / (len(coordinates) + area)
            else:
                width = height = density = 0.0
            widths.append(width)
            heights.append(height)
            densities.append(density)
            metrics = result.entry.quality_metrics
            known = False
            legal = True
            for name in _LEGALITY_METRICS:
                if name in metrics:
                    known = True
                    legal = legal and metrics[name] > 0.5
            for name in _VIOLATION_METRICS:
                if name in metrics:
                    known = True
                    legal = legal and metrics[name] <= 0.0
            legalities.append(float(legal) if known else 0.5)

        sim01 = np.clip(0.5 * (similarities + 1.0), 0.0, 1.0)
        clock01 = np.clip(0.5 * (clock_similarities + 1.0), 0.0, 1.0)
        effective_count = 1.0 / max(1e-12, float(np.square(weights).sum()))
        header = np.asarray(
            [
                1.0,
                1.0 - math.exp(-len(results) / 4.0),
                float(np.dot(weights, exact)),
                float(sim01.max()),
                float(np.dot(weights, sim01)),
                float(np.sqrt(np.dot(weights, np.square(sim01 - np.dot(weights, sim01))))),
                float(np.dot(weights, clock01)),
                float(qualities.max()),
                float(np.dot(weights, qualities)),
                float(1.0 - math.exp(-effective_count / 4.0)),
                float(np.dot(weights, np.tanh(np.asarray(widths)))),
                float(np.dot(weights, np.tanh(np.asarray(heights)))),
                float(np.dot(weights, np.asarray(densities))),
                float(np.dot(weights, np.tanh(np.log1p(route_counts) / 4.0))),
                float(np.dot(weights, np.asarray(legalities))),
                float(np.clip(np.dot(weights, [result.selection_score for result in results]), -1.0, 1.0)),
            ],
            dtype=np.float64,
        )
        context[:16] = header.astype(np.float32)
        tail_dim = self.context_dim - 16
        if tail_dim:
            tail = np.zeros(tail_dim, dtype=np.float64)
            for weight, result in zip(weights, results):
                tail += float(weight) * self._hash_features(
                    {
                        "graph": result.entry.graph_descriptor,
                        "clock": result.entry.clock_descriptor,
                        "placement": result.entry.normalized_placement,
                        "routes": result.entry.route_hints,
                        "quality": result.entry.quality_metrics,
                    },
                    tail_dim,
                )
            norm = float(np.linalg.norm(tail))
            if norm > 1e-12:
                tail /= norm
            context[16:] = tail.astype(np.float32)
        return context

    def context_tensor(
        self,
        results: Sequence[RetrievedLayout],
        *,
        device: torch.device | str | None = None,
        dtype: torch.dtype = torch.float32,
    ) -> torch.Tensor:
        """Torch adapter for direct use by a policy memory encoder."""

        return torch.as_tensor(self.aggregate_context(results), device=device, dtype=dtype)

    def retrieve_context(self, *args: Any, **kwargs: Any) -> tuple[np.ndarray, list[RetrievedLayout]]:
        """Run :meth:`retrieve` and return ``(fixed_context, memories)``."""

        results = self.retrieve(*args, **kwargs)
        return self.aggregate_context(results), results

    def to_dict(self) -> dict[str, Any]:
        with self._lock:
            entries = [self._entries[key].to_dict() for key in sorted(self._entries)]
        return {
            "schema_version": MEMORY_SCHEMA_VERSION,
            "graph_descriptor_dim": self.graph_descriptor_dim,
            "clock_descriptor_dim": self.clock_descriptor_dim,
            "context_dim": self.context_dim,
            "quality_directions": dict(sorted(self.quality_directions.items())),
            "quality_weights": dict(sorted(self.quality_weights.items())),
            "entries": entries,
        }

    @classmethod
    def from_dict(cls, payload: Mapping[str, Any]) -> "LayoutRetrievalMemory":
        version = int(payload.get("schema_version", 0))
        if version != MEMORY_SCHEMA_VERSION:
            raise ValueError(
                f"unsupported layout-memory schema {version}; expected {MEMORY_SCHEMA_VERSION}"
            )
        memory = cls(
            graph_descriptor_dim=int(payload["graph_descriptor_dim"]),
            clock_descriptor_dim=int(payload["clock_descriptor_dim"]),
            context_dim=int(payload.get("context_dim", MEMORY_CONTEXT_DIM)),
            quality_directions=payload.get("quality_directions", {}),
            quality_weights=payload.get("quality_weights", {}),
        )
        memory.extend(LayoutMemoryEntry.from_dict(item) for item in payload.get("entries", ()))
        return memory

    def save(self, path: str | os.PathLike[str]) -> Path:
        """Atomically persist the portable JSON index."""

        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        payload = self.to_dict()
        temporary_name = ""
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                dir=destination.parent,
                prefix=f".{destination.name}.",
                suffix=".tmp",
                delete=False,
            ) as handle:
                temporary_name = handle.name
                json.dump(payload, handle, ensure_ascii=False, sort_keys=True, indent=2)
                handle.write("\n")
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temporary_name, destination)
        finally:
            if temporary_name and os.path.exists(temporary_name):
                os.unlink(temporary_name)
        return destination

    @classmethod
    def load(cls, path: str | os.PathLike[str]) -> "LayoutRetrievalMemory":
        with Path(path).open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        if not isinstance(payload, Mapping):
            raise ValueError("layout-memory JSON root must be an object")
        return cls.from_dict(payload)


__all__ = [
    "DEFAULT_CLOCK_DESCRIPTOR_DIM",
    "GRAPH_DESCRIPTOR_DIM",
    "LayoutMemoryEntry",
    "LayoutRetrievalMemory",
    "MEMORY_CONTEXT_DIM",
    "MEMORY_SCHEMA_VERSION",
    "RetrievedLayout",
    "graph_descriptor_from_circuit",
    "topology_hash_from_circuit",
]
