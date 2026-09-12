"""Dataset utilities for IFCN placement-and-routing files.

The project contains several generations of ``.ifcn`` files.  This module is
deliberately dependency-free and accepts both of the phase representations in
the repository:

* a raw ``(x,y):phase`` map;
* packed square blocks written either as ``tile(x,y):0x...`` or as
  ``(block_x,block_y):0x...`` inside an ``#encoded phase map`` section.

The central :class:`IFCNLayout` object contains training-ready relative
coordinates, path direction/waypoint features, a node-id-independent directed
Weisfeiler--Lehman topology hash, and an explicit integrity report.  Dataset
splits are made by topology hash, so repeated layouts of the same circuit can
never leak across train/validation/test partitions.
"""

from __future__ import annotations

import hashlib
import json
import math
import re
from collections import Counter, defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Mapping, Optional, Sequence, Tuple, Union


SCHEMA_VERSION = 1
MAPPING_MODE_COMBINATIONAL = "combinational"
MAPPING_MODE_SEQUENTIAL = "sequential"
Coord = Tuple[int, int]
FloatCoord = Tuple[float, float]
PathLike = Union[str, Path]


_NODE_RE = re.compile(
    r"^\s*([+-]?\d+)\s*,\s*(.*?)\s*,\s*([^,]+?)\s*,\s*"
    r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*;\s*$"
)
_PATH_RE = re.compile(
    r"^\s*\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*:\s*(.*?)\s*;\s*$"
)
_COORD_RE = re.compile(r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)")
_RAW_PHASE_RE = re.compile(
    r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*:\s*([+-]?\d+)\s*;"
)
_PACKED_TILE_RE = re.compile(
    r"tile\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*:\s*(0x[0-9a-fA-F]+)\s*;",
    re.IGNORECASE,
)
_PACKED_BLOCK_RE = re.compile(
    r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*:\s*(0x[0-9a-fA-F]+)\s*;",
    re.IGNORECASE,
)
_ITERATION_DISTANCE_RE = re.compile(
    r"^\s*#\s*iteration(?:_|\s+)distance\s*(?:=|:)\s*([+-]?\d+)\s*$",
    re.IGNORECASE,
)
_ITERATION_DISTANCE_PREFIX_RE = re.compile(
    r"^\s*#\s*iteration(?:_|\s+)distance\b",
    re.IGNORECASE,
)
_MAPPING_MODE_RE = re.compile(
    r"^\s*#\s*mapping\s+mode\s*:\s*(.*?)\s*$",
    re.IGNORECASE,
)
_MAPPING_MODE_KEY_RE = re.compile(
    r"^\s*#\s*mapping\s+mode\b.*$",
    re.IGNORECASE,
)
_FLOW_RE = re.compile(r"^\s*#\s*flow\s*:\s*(.*?)\s*$", re.IGNORECASE)
_LEGACY_SEQUENTIAL_FLOWS = {
    "sequential register-cut p&r v0",
    "sampled-state physical-feedback p&r experiment v0",
}
_MAX_IFCN_ITERATION_DISTANCE = (1 << 32) - 1


def _normalise_key(value: str) -> str:
    return " ".join(value.strip().lower().split())


def normalize_mapping_mode(value: object = None) -> str:
    """Normalize a valid explicit mode, defaulting only missing values.

    Unknown explicit values are metadata errors, not aliases for the legacy
    combinational mode.  IFCN parsing records such errors in its quality report.
    """

    if value is None:
        return MAPPING_MODE_COMBINATIONAL
    normalized = str(value).strip().lower()
    if normalized in {MAPPING_MODE_COMBINATIONAL, MAPPING_MODE_SEQUENTIAL}:
        return normalized
    raise ValueError(f"unsupported IFCN mapping mode: {normalized}")


def _maybe_int(value: Optional[str]) -> Optional[int]:
    if value is None:
        return None
    match = re.search(r"[+-]?\d+", value)
    return int(match.group(0)) if match else None


def _maybe_float(value: Optional[str]) -> Optional[float]:
    if value is None:
        return None
    match = re.search(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", value)
    return float(match.group(0)) if match else None


def _pair_from_text(value: Optional[str], label_a: str, label_b: str) -> Tuple[Optional[int], Optional[int]]:
    if value is None:
        return None, None
    first = re.search(rf"\b{re.escape(label_a)}\s*:\s*([+-]?\d+)", value, re.IGNORECASE)
    second = re.search(rf"\b{re.escape(label_b)}\s*:\s*([+-]?\d+)", value, re.IGNORECASE)
    return (
        int(first.group(1)) if first else None,
        int(second.group(1)) if second else None,
    )


def _stable_digest(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _normalise_node_type(node_type: str) -> str:
    return " ".join(node_type.strip().lower().split()) or "unknown"


def _direction(first: Coord, second: Coord) -> str:
    dx = second[0] - first[0]
    dy = second[1] - first[1]
    if (dx, dy) == (1, 0):
        return "R"
    if (dx, dy) == (-1, 0):
        return "L"
    if (dx, dy) == (0, 1):
        return "D"
    if (dx, dy) == (0, -1):
        return "U"
    if (dx, dy) == (0, 0):
        return "STAY"
    horizontal = "R" if dx > 0 else "L"
    vertical = "D" if dy > 0 else "U"
    return f"{vertical}{horizontal}"


def _waypoints(points: Sequence[Coord], directions: Sequence[str]) -> Tuple[Coord, ...]:
    if not points:
        return ()
    if len(points) == 1:
        return (points[0],)
    result: List[Coord] = [points[0]]
    for index in range(1, len(directions)):
        if directions[index] != directions[index - 1]:
            result.append(points[index])
    if result[-1] != points[-1]:
        result.append(points[-1])
    return tuple(result)


def _normalise_coord(coord: Coord, origin: Coord, span: Coord) -> FloatCoord:
    return (
        (coord[0] - origin[0]) / max(span[0], 1),
        (coord[1] - origin[1]) / max(span[1], 1),
    )


@dataclass
class IFCNNode:
    node_id: int
    name: str
    node_type: str
    position: Coord
    relative_position: Coord = (0, 0)
    normalized_position: FloatCoord = (0.0, 0.0)

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "IFCNNode":
        return cls(
            node_id=int(value["node_id"]),
            name=str(value["name"]),
            node_type=str(value["node_type"]),
            position=tuple(int(item) for item in value["position"]),  # type: ignore[arg-type]
            relative_position=tuple(int(item) for item in value["relative_position"]),  # type: ignore[arg-type]
            normalized_position=tuple(float(item) for item in value["normalized_position"]),  # type: ignore[arg-type]
        )


@dataclass
class IFCNPath:
    source: int
    target: int
    points: Tuple[Coord, ...]
    directions: Tuple[str, ...] = ()
    direction_counts: Dict[str, int] = field(default_factory=dict)
    waypoints: Tuple[Coord, ...] = ()
    relative_waypoints: Tuple[Coord, ...] = ()
    normalized_waypoints: Tuple[FloatCoord, ...] = ()
    steps: int = 0
    manhattan_length: int = 0
    turns: int = 0
    detour_ratio: float = 1.0
    endpoint_match: bool = False
    iteration_distance: int = 0

    def __post_init__(self) -> None:
        self.iteration_distance = int(self.iteration_distance)
        if self.iteration_distance < 0:
            raise ValueError("IFCN path iteration_distance must be non-negative")

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "IFCNPath":
        def coords(key: str) -> Tuple[Coord, ...]:
            return tuple(tuple(int(item) for item in coord) for coord in value.get(key, []))  # type: ignore[arg-type]

        iteration_distance = int(value.get("iteration_distance", 0))
        if iteration_distance < 0:
            raise ValueError("IFCN path iteration_distance must be non-negative")

        return cls(
            source=int(value["source"]),
            target=int(value["target"]),
            points=coords("points"),
            iteration_distance=iteration_distance,
            directions=tuple(str(item) for item in value.get("directions", [])),  # type: ignore[arg-type]
            direction_counts={
                str(key): int(item)
                for key, item in value.get("direction_counts", {}).items()  # type: ignore[union-attr]
            },
            waypoints=coords("waypoints"),
            relative_waypoints=coords("relative_waypoints"),
            normalized_waypoints=tuple(
                tuple(float(item) for item in coord)
                for coord in value.get("normalized_waypoints", [])  # type: ignore[arg-type]
            ),
            steps=int(value.get("steps", 0)),
            manhattan_length=int(value.get("manhattan_length", 0)),
            turns=int(value.get("turns", 0)),
            detour_ratio=float(value.get("detour_ratio", 1.0)),
            endpoint_match=bool(value.get("endpoint_match", False)),
        )


@dataclass
class PackedPhaseTile:
    block_x: int
    block_y: int
    hex_value: str
    notation: str = "block"

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "PackedPhaseTile":
        return cls(
            block_x=int(value["block_x"]),
            block_y=int(value["block_y"]),
            hex_value=str(value["hex_value"]),
            notation=str(value.get("notation", "block")),
        )


@dataclass
class PackedPhaseMetadata:
    block_size: int = 4
    phase_count: int = 4
    codec: str = "packed_hex_2bit_row_major"
    raw_size: Tuple[Optional[int], Optional[int]] = (None, None)
    padded_size: Tuple[Optional[int], Optional[int]] = (None, None)
    block_grid: Tuple[Optional[int], Optional[int]] = (None, None)
    absolute_bbox: Optional[Tuple[int, int, int, int]] = None
    tiles: Tuple[PackedPhaseTile, ...] = ()
    disabled: bool = False

    @property
    def tile_count(self) -> int:
        return len(self.tiles)

    def decode(self) -> Dict[Coord, int]:
        """Decode packed blocks to normalized raw coordinates.

        Every row occupies one byte and every cell two bits, matching both
        packed encodings currently emitted by the project.
        """

        if self.disabled:
            return {}
        result: Dict[Coord, int] = {}
        raw_width, raw_height = self.raw_size
        digits_per_tile = 2 * self.block_size
        for tile in self.tiles:
            digits = tile.hex_value.lower().removeprefix("0x").zfill(digits_per_tile)
            # Be permissive with legacy values that contain leading padding.
            digits = digits[-digits_per_tile:]
            for local_y in range(self.block_size):
                row_start = local_y * 2
                row_byte = int(digits[row_start : row_start + 2], 16)
                for local_x in range(self.block_size):
                    x = tile.block_x * self.block_size + local_x
                    y = tile.block_y * self.block_size + local_y
                    if raw_width is not None and x >= raw_width:
                        continue
                    if raw_height is not None and y >= raw_height:
                        continue
                    result[(x, y)] = (row_byte >> (2 * local_x)) & 0x3
        return result

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "PackedPhaseMetadata":
        bbox = value.get("absolute_bbox")
        return cls(
            block_size=int(value.get("block_size", 4)),
            phase_count=int(value.get("phase_count", 4)),
            codec=str(value.get("codec", "packed_hex_2bit_row_major")),
            raw_size=tuple(value.get("raw_size", (None, None))),  # type: ignore[arg-type]
            padded_size=tuple(value.get("padded_size", (None, None))),  # type: ignore[arg-type]
            block_grid=tuple(value.get("block_grid", (None, None))),  # type: ignore[arg-type]
            absolute_bbox=tuple(int(item) for item in bbox) if bbox is not None else None,  # type: ignore[arg-type]
            tiles=tuple(PackedPhaseTile.from_dict(item) for item in value.get("tiles", [])),  # type: ignore[arg-type]
            disabled=bool(value.get("disabled", False)),
        )


@dataclass
class IFCNQualityReport:
    complete: bool
    valid_for_training: bool
    errors: Tuple[str, ...] = ()
    warnings: Tuple[str, ...] = ()
    declared_nodes: Optional[int] = None
    parsed_nodes: int = 0
    declared_edges: Optional[int] = None
    parsed_paths: int = 0
    unknown_path_endpoints: int = 0
    endpoint_mismatches: int = 0
    non_cardinal_steps: int = 0
    invalid_phase_values: int = 0
    phase_coverage: float = 0.0
    area: Optional[float] = None
    wirelength: float = 0.0
    crossings: Optional[float] = None
    cell_count: Optional[float] = None
    runtime: Optional[float] = None
    critical_path: Optional[float] = None
    phase_conflicts: int = 0

    def objective(self, name: str) -> float:
        aliases = {"cross_count": "crossings", "run_time": "runtime"}
        name = aliases.get(name, name)
        value = getattr(self, name, None)
        if value is None:
            return math.inf
        numeric = float(value)
        return numeric if math.isfinite(numeric) else math.inf

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "IFCNQualityReport":
        return cls(
            complete=bool(value["complete"]),
            valid_for_training=bool(value["valid_for_training"]),
            errors=tuple(str(item) for item in value.get("errors", [])),  # type: ignore[arg-type]
            warnings=tuple(str(item) for item in value.get("warnings", [])),  # type: ignore[arg-type]
            declared_nodes=_optional_int_value(value.get("declared_nodes")),
            parsed_nodes=int(value.get("parsed_nodes", 0)),
            declared_edges=_optional_int_value(value.get("declared_edges")),
            parsed_paths=int(value.get("parsed_paths", 0)),
            unknown_path_endpoints=int(value.get("unknown_path_endpoints", 0)),
            endpoint_mismatches=int(value.get("endpoint_mismatches", 0)),
            non_cardinal_steps=int(value.get("non_cardinal_steps", 0)),
            invalid_phase_values=int(value.get("invalid_phase_values", 0)),
            phase_coverage=float(value.get("phase_coverage", 0.0)),
            area=_optional_float_value(value.get("area")),
            wirelength=float(value.get("wirelength", 0.0)),
            crossings=_optional_float_value(value.get("crossings")),
            cell_count=_optional_float_value(value.get("cell_count")),
            runtime=_optional_float_value(value.get("runtime")),
            critical_path=_optional_float_value(value.get("critical_path")),
            phase_conflicts=int(value.get("phase_conflicts", 0)),
        )


def _optional_int_value(value: object) -> Optional[int]:
    return None if value is None else int(value)


def _optional_float_value(value: object) -> Optional[float]:
    return None if value is None else float(value)


@dataclass
class IFCNLayout:
    source_path: str
    header: Dict[str, str]
    nodes: Tuple[IFCNNode, ...]
    paths: Tuple[IFCNPath, ...]
    raw_phase_map: Dict[Coord, int]
    packed_phase: Optional[PackedPhaseMetadata]
    topology_hash: str
    layout_hash: str
    quality: IFCNQualityReport
    bounds: Optional[Tuple[int, int, int, int]] = None
    schema_version: int = SCHEMA_VERSION
    mapping_mode: str = ""

    def __post_init__(self) -> None:
        raw_mode = self.mapping_mode or self.header.get("mapping mode")
        if raw_mode is not None:
            self.mapping_mode = normalize_mapping_mode(raw_mode)
            return
        legacy_flow = str(self.header.get("flow", "")).strip().lower()
        self.mapping_mode = (
            MAPPING_MODE_SEQUENTIAL
            if any(path.iteration_distance > 0 for path in self.paths)
            or legacy_flow in _LEGACY_SEQUENTIAL_FLOWS
            else MAPPING_MODE_COMBINATIONAL
        )

    @property
    def circuit_name(self) -> str:
        return self.header.get("circuit name", Path(self.source_path).stem)

    @property
    def phase_encoding(self) -> str:
        if self.raw_phase_map:
            return "raw"
        if self.packed_phase and self.packed_phase.tiles:
            return "packed"
        if self.packed_phase and self.packed_phase.disabled:
            return "disabled"
        return "none"

    def effective_phase_map(self) -> Dict[Coord, int]:
        if self.raw_phase_map:
            return dict(self.raw_phase_map)
        return self.packed_phase.decode() if self.packed_phase is not None else {}

    def to_dict(self) -> Dict[str, object]:
        return {
            "schema_version": self.schema_version,
            "source_path": self.source_path,
            "header": dict(self.header),
            "mapping_mode": self.mapping_mode,
            "nodes": [asdict(node) for node in self.nodes],
            "paths": [asdict(path) for path in self.paths],
            "raw_phase_map": [
                {"x": x, "y": y, "phase": phase}
                for (x, y), phase in sorted(self.raw_phase_map.items())
            ],
            "packed_phase": asdict(self.packed_phase) if self.packed_phase is not None else None,
            "topology_hash": self.topology_hash,
            "layout_hash": self.layout_hash,
            "quality": asdict(self.quality),
            "bounds": list(self.bounds) if self.bounds is not None else None,
        }

    @classmethod
    def from_dict(cls, value: Mapping[str, object]) -> "IFCNLayout":
        schema_version = int(value.get("schema_version", 0))
        if schema_version != SCHEMA_VERSION:
            raise ValueError(
                f"unsupported IFCN dataset schema {schema_version}; expected {SCHEMA_VERSION}"
            )
        phase_entries = value.get("raw_phase_map", [])
        raw_phase_map = {
            (int(item["x"]), int(item["y"])): int(item["phase"])
            for item in phase_entries  # type: ignore[union-attr]
        }
        packed = value.get("packed_phase")
        bounds = value.get("bounds")
        header = {
            str(key): str(item)
            for key, item in value.get("header", {}).items()  # type: ignore[union-attr]
        }
        return cls(
            source_path=str(value["source_path"]),
            header=header,
            nodes=tuple(IFCNNode.from_dict(item) for item in value.get("nodes", [])),  # type: ignore[arg-type]
            paths=tuple(IFCNPath.from_dict(item) for item in value.get("paths", [])),  # type: ignore[arg-type]
            raw_phase_map=raw_phase_map,
            packed_phase=PackedPhaseMetadata.from_dict(packed) if packed is not None else None,  # type: ignore[arg-type]
            topology_hash=str(value["topology_hash"]),
            layout_hash=str(value["layout_hash"]),
            quality=IFCNQualityReport.from_dict(value["quality"]),  # type: ignore[arg-type]
            bounds=tuple(int(item) for item in bounds) if bounds is not None else None,  # type: ignore[arg-type]
            mapping_mode=str(value.get("mapping_mode") or ""),
            schema_version=schema_version,
        )


def topology_fingerprint(
    nodes: Sequence[IFCNNode],
    paths: Sequence[IFCNPath],
    mapping_mode: object = MAPPING_MODE_COMBINATIONAL,
) -> str:
    """Return a directed WL topology hash independent of node IDs and order.

    Node types, edge direction and edge multiplicity participate in the hash.
    Sequential mode and non-zero iteration distances additionally make this an
    edge-labelled topology hash.  The legacy all-zero combinational code path
    is kept byte-for-byte compatible with existing online topology hashes.
    Node names, coordinates, paths and clock values intentionally do not.  WL
    is not a full graph-isomorphism solver, but the retained round histograms
    and final colour-edge multiset make accidental collisions unlikely for the
    sparse directed logic graphs used here.
    """

    normalized_mode = normalize_mapping_mode(mapping_mode)
    semantic_edges = (
        normalized_mode == MAPPING_MODE_SEQUENTIAL
        or any(path.iteration_distance != 0 for path in paths)
    )
    node_types = {node.node_id: _normalise_node_type(node.node_type) for node in nodes}

    if semantic_edges:
        labelled_predecessors: Dict[int, List[Tuple[int, int]]] = {
            node_id: [] for node_id in node_types
        }
        labelled_successors: Dict[int, List[Tuple[int, int]]] = {
            node_id: [] for node_id in node_types
        }
        labelled_edges: List[Tuple[int, int, int]] = []
        for path in paths:
            if path.source not in node_types or path.target not in node_types:
                continue
            distance = int(path.iteration_distance)
            labelled_successors[path.source].append((path.target, distance))
            labelled_predecessors[path.target].append((path.source, distance))
            labelled_edges.append((path.source, path.target, distance))

        labels = {
            node_id: _stable_digest(
                [
                    node_types[node_id],
                    sorted(distance for _, distance in labelled_predecessors[node_id]),
                    sorted(distance for _, distance in labelled_successors[node_id]),
                ]
            )
            for node_id in node_types
        }
        round_histograms: List[List[Tuple[str, int]]] = [
            sorted(Counter(labels.values()).items())
        ]
        rounds = min(max(len(nodes), 1), 16)
        for _ in range(rounds):
            labels = {
                node_id: _stable_digest(
                    [
                        node_types[node_id],
                        labels[node_id],
                        sorted(
                            (labels[parent], distance)
                            for parent, distance in labelled_predecessors[node_id]
                        ),
                        sorted(
                            (labels[child], distance)
                            for child, distance in labelled_successors[node_id]
                        ),
                    ]
                )
                for node_id in node_types
            }
            round_histograms.append(sorted(Counter(labels.values()).items()))

        signature = {
            "mapping_mode": normalized_mode,
            "node_count": len(nodes),
            "edge_count": len(labelled_edges),
            "round_histograms": round_histograms,
            "final_edges": sorted(
                (labels[source], labels[target], distance)
                for source, target, distance in labelled_edges
            ),
        }
        return _stable_digest(signature)

    predecessors: Dict[int, List[int]] = {node_id: [] for node_id in node_types}
    successors: Dict[int, List[int]] = {node_id: [] for node_id in node_types}
    valid_edges: List[Tuple[int, int]] = []
    for path in paths:
        if path.source not in node_types or path.target not in node_types:
            continue
        successors[path.source].append(path.target)
        predecessors[path.target].append(path.source)
        valid_edges.append((path.source, path.target))

    labels = {
        node_id: _stable_digest(
            [node_types[node_id], len(predecessors[node_id]), len(successors[node_id])]
        )
        for node_id in node_types
    }
    round_histograms: List[List[Tuple[str, int]]] = [sorted(Counter(labels.values()).items())]
    rounds = min(max(len(nodes), 1), 16)
    for _ in range(rounds):
        labels = {
            node_id: _stable_digest(
                [
                    node_types[node_id],
                    labels[node_id],
                    sorted(labels[parent] for parent in predecessors[node_id]),
                    sorted(labels[child] for child in successors[node_id]),
                ]
            )
            for node_id in node_types
        }
        round_histograms.append(sorted(Counter(labels.values()).items()))

    signature = {
        "node_count": len(nodes),
        "edge_count": len(valid_edges),
        "round_histograms": round_histograms,
        "final_edges": sorted((labels[source], labels[target]) for source, target in valid_edges),
    }
    return _stable_digest(signature)


def _layout_fingerprint(
    nodes: Sequence[IFCNNode],
    paths: Sequence[IFCNPath],
    phase_map: Mapping[Coord, int],
    topology_hash: str,
    mapping_mode: object = MAPPING_MODE_COMBINATIONAL,
) -> str:
    normalized_mode = normalize_mapping_mode(mapping_mode)
    semantic_edges = (
        normalized_mode == MAPPING_MODE_SEQUENTIAL
        or any(path.iteration_distance != 0 for path in paths)
    )
    if nodes or paths:
        all_coords = [node.position for node in nodes]
        all_coords.extend(point for path in paths for point in path.points)
        min_x = min(coord[0] for coord in all_coords)
        min_y = min(coord[1] for coord in all_coords)
    else:
        min_x = min_y = 0
    node_descriptor = {
        node.node_id: (
            _normalise_node_type(node.node_type),
            node.name.strip(),
            node.position[0] - min_x,
            node.position[1] - min_y,
        )
        for node in nodes
    }
    geometry = {
        "topology": topology_hash,
        "nodes": sorted(node_descriptor.values()),
        "paths": sorted(
            (
                node_descriptor.get(path.source, ("?", str(path.source), 0, 0)),
                node_descriptor.get(path.target, ("?", str(path.target), 0, 0)),
                tuple((x - min_x, y - min_y) for x, y in path.points),
                *(
                    (int(path.iteration_distance),)
                    if semantic_edges
                    else ()
                ),
            )
            for path in paths
        ),
        "phase": sorted(
            (x - min_x, y - min_y, int(phase)) for (x, y), phase in phase_map.items()
        ),
    }
    if semantic_edges:
        geometry["mapping_mode"] = normalized_mode
    return _stable_digest(geometry)


def _parse_header(lines: Sequence[str]) -> Tuple[Dict[str, str], str]:
    header: Dict[str, str] = {}
    algorithm = ""
    for line in lines:
        stripped = line.strip()
        lower = stripped.lower()
        if lower.startswith("#designed by "):
            algorithm = stripped[len("#designed by ") :].rstrip(".")
        elif lower.startswith("#generated by "):
            algorithm = stripped[len("#generated by ") :].rstrip(".")
        if not stripped.startswith("#") or stripped.startswith("###") or ":" not in stripped:
            continue
        key, value = stripped[1:].split(":", 1)
        key = _normalise_key(key)
        if key in {"nodes info", "paths info", "phase map", "encoded phase map"}:
            continue
        header[key] = value.strip()
    if algorithm:
        header.setdefault("algorithm", algorithm)
    return header, algorithm


def _parse_mapping_metadata(
    lines: Sequence[str],
) -> Tuple[bool, Optional[str], bool, List[str]]:
    """Collect mapping declarations without losing duplicates or malformed keys."""

    explicit_declared = False
    explicit_modes: List[str] = []
    legacy_sequential_flow = False
    errors: List[str] = []
    for line_number, line in enumerate(lines, start=1):
        if _MAPPING_MODE_KEY_RE.match(line):
            explicit_declared = True
            match = _MAPPING_MODE_RE.match(line)
            if match is None:
                errors.append(
                    f"line {line_number}: malformed IFCN mapping mode declaration"
                )
                continue
            try:
                explicit_modes.append(normalize_mapping_mode(match.group(1)))
            except ValueError as error:
                errors.append(f"line {line_number}: {error}")
            continue

        flow_match = _FLOW_RE.match(line)
        if flow_match is not None:
            normalized_flow = flow_match.group(1).strip().lower()
            legacy_sequential_flow = (
                legacy_sequential_flow
                or normalized_flow in _LEGACY_SEQUENTIAL_FLOWS
            )

    distinct_modes = set(explicit_modes)
    if len(distinct_modes) > 1:
        errors.append("conflicting IFCN mapping mode declarations")
    explicit_mode = explicit_modes[0] if explicit_modes else None
    return explicit_declared, explicit_mode, legacy_sequential_flow, errors


def _packed_metadata(
    header: Mapping[str, str], tiles: Sequence[PackedPhaseTile], disabled: bool
) -> Optional[PackedPhaseMetadata]:
    if not tiles and not disabled and "phase encoding" not in header and "phase codec" not in header:
        return None
    phase_count = _maybe_int(header.get("phase count")) or 4
    block_size = _maybe_int(header.get("block size"))
    if block_size is None:
        codec_size = re.search(r"block_size\s*=\s*(\d+)", header.get("phase codec", ""))
        block_size = int(codec_size.group(1)) if codec_size else phase_count
    raw_size = _pair_from_text(header.get("raw size"), "width", "height")
    if raw_size == (None, None):
        # Legacy ``tile(x,y)`` files predate the explicit raw-size header; the
        # declared layout rectangle is the unpadded phase extent in that form.
        raw_size = _pair_from_text(header.get("layout area"), "width", "height")
    padded_size = _pair_from_text(header.get("padded size"), "width", "height")
    block_grid = _pair_from_text(header.get("blocks"), "columns", "rows")
    bbox_match = re.search(
        r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)\s*->\s*"
        r"\(\s*([+-]?\d+)\s*,\s*([+-]?\d+)\s*\)",
        header.get("absolute bbox", ""),
    )
    bbox = tuple(int(item) for item in bbox_match.groups()) if bbox_match else None
    codec = header.get("phase codec", header.get("encoding", "packed_hex_2bit_row_major"))
    return PackedPhaseMetadata(
        block_size=max(int(block_size), 1),
        phase_count=max(int(phase_count), 1),
        codec=codec,
        raw_size=raw_size,
        padded_size=padded_size,
        block_grid=block_grid,
        absolute_bbox=bbox,  # type: ignore[arg-type]
        tiles=tuple(sorted(tiles, key=lambda tile: (tile.block_y, tile.block_x))),
        disabled=disabled,
    )


def _layout_area(header: Mapping[str, str]) -> Tuple[Optional[int], Optional[int], Optional[float]]:
    text = header.get("layout area", "")
    width, height = _pair_from_text(text, "width", "height")
    area_match = re.search(r"\barea\s*:\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))", text, re.IGNORECASE)
    area = float(area_match.group(1)) if area_match else None
    if area is None and width is not None and height is not None:
        area = float(width * height)
    return width, height, area


def _phase_conflict_count(header: Mapping[str, str]) -> int:
    candidates = [
        _maybe_int(header.get("clock scheme conflicts")),
        _maybe_int(header.get("random phase scheme conflicts")),
    ]
    return max((value for value in candidates if value is not None), default=0)


def _enrich_geometry(
    nodes: Sequence[IFCNNode], paths: Sequence[IFCNPath]
) -> Tuple[Tuple[IFCNNode, ...], Tuple[IFCNPath, ...], Optional[Tuple[int, int, int, int]]]:
    all_coords = [node.position for node in nodes]
    all_coords.extend(point for path in paths for point in path.points)
    if not all_coords:
        return tuple(nodes), tuple(paths), None
    min_x = min(coord[0] for coord in all_coords)
    min_y = min(coord[1] for coord in all_coords)
    max_x = max(coord[0] for coord in all_coords)
    max_y = max(coord[1] for coord in all_coords)
    origin = (min_x, min_y)
    span = (max_x - min_x, max_y - min_y)
    positions = {node.node_id: node.position for node in nodes}

    enriched_nodes = tuple(
        IFCNNode(
            node_id=node.node_id,
            name=node.name,
            node_type=node.node_type,
            position=node.position,
            relative_position=(node.position[0] - min_x, node.position[1] - min_y),
            normalized_position=_normalise_coord(node.position, origin, span),
        )
        for node in nodes
    )
    enriched_paths: List[IFCNPath] = []
    for path in paths:
        directions = tuple(
            _direction(first, second) for first, second in zip(path.points, path.points[1:])
        )
        waypoints = _waypoints(path.points, directions)
        relative_waypoints = tuple((x - min_x, y - min_y) for x, y in waypoints)
        normalized_waypoints = tuple(_normalise_coord(point, origin, span) for point in waypoints)
        manhattan_length = sum(
            abs(second[0] - first[0]) + abs(second[1] - first[1])
            for first, second in zip(path.points, path.points[1:])
        )
        if path.points:
            direct = abs(path.points[-1][0] - path.points[0][0]) + abs(
                path.points[-1][1] - path.points[0][1]
            )
            detour_ratio = manhattan_length / max(direct, 1)
        else:
            detour_ratio = math.inf
        endpoint_match = bool(
            path.points
            and positions.get(path.source) == path.points[0]
            and positions.get(path.target) == path.points[-1]
        )
        enriched_paths.append(
            IFCNPath(
                source=path.source,
                target=path.target,
                points=path.points,
                iteration_distance=path.iteration_distance,
                directions=directions,
                direction_counts=dict(sorted(Counter(directions).items())),
                waypoints=waypoints,
                relative_waypoints=relative_waypoints,
                normalized_waypoints=normalized_waypoints,
                steps=max(len(path.points) - 1, 0),
                manhattan_length=manhattan_length,
                turns=sum(
                    directions[index] != directions[index - 1]
                    for index in range(1, len(directions))
                ),
                detour_ratio=detour_ratio,
                endpoint_match=endpoint_match,
            )
        )
    return enriched_nodes, tuple(enriched_paths), (min_x, min_y, max_x, max_y)


def _build_quality_report(
    header: Mapping[str, str],
    mapping_mode: str,
    nodes: Sequence[IFCNNode],
    paths: Sequence[IFCNPath],
    raw_phase_map: Mapping[Coord, int],
    packed_phase: Optional[PackedPhaseMetadata],
    bounds: Optional[Tuple[int, int, int, int]],
    parse_errors: Sequence[str],
) -> IFCNQualityReport:
    errors = list(parse_errors)
    warnings: List[str] = []
    sequential_training_unsupported = mapping_mode == MAPPING_MODE_SEQUENTIAL
    if sequential_training_unsupported:
        warnings.append(
            "sequential mapping recurrence is unsupported by offline DAG training"
        )
    declared_nodes = _maybe_int(header.get("gates number"))
    declared_edges = _maybe_int(header.get("edges number"))
    node_ids = [node.node_id for node in nodes]
    duplicate_ids = len(node_ids) - len(set(node_ids))
    if not nodes:
        errors.append("no nodes parsed")
    if duplicate_ids:
        errors.append(f"duplicate node ids: {duplicate_ids}")
    if declared_nodes is not None and declared_nodes != len(nodes):
        errors.append(f"declared nodes {declared_nodes} != parsed nodes {len(nodes)}")
    if declared_edges is not None and declared_edges != len(paths):
        errors.append(f"declared edges {declared_edges} != parsed paths {len(paths)}")

    known_nodes = set(node_ids)
    unknown_endpoints = sum(
        path.source not in known_nodes or path.target not in known_nodes for path in paths
    )
    endpoint_mismatches = sum(not path.endpoint_match for path in paths)
    non_cardinal_steps = sum(
        abs(second[0] - first[0]) + abs(second[1] - first[1]) != 1
        for path in paths
        for first, second in zip(path.points, path.points[1:])
    )
    empty_paths = sum(not path.points for path in paths)
    if unknown_endpoints:
        errors.append(f"paths with unknown endpoints: {unknown_endpoints}")
    if endpoint_mismatches:
        errors.append(f"path endpoint mismatches: {endpoint_mismatches}")
    if non_cardinal_steps:
        errors.append(f"non-cardinal/non-unit path steps: {non_cardinal_steps}")
    if empty_paths:
        errors.append(f"empty paths: {empty_paths}")

    phase_map = dict(raw_phase_map)
    if not phase_map and packed_phase is not None:
        phase_map = packed_phase.decode()
    used_coords = {node.position for node in nodes}
    used_coords.update(point for path in paths for point in path.points)
    phase_count = _maybe_int(header.get("phase count")) or 4
    invalid_phase_values = sum(
        coord in phase_map and not (0 <= int(phase_map[coord]) < phase_count)
        for coord in used_coords
    )
    covered = sum(
        coord in phase_map and 0 <= int(phase_map[coord]) < phase_count
        for coord in used_coords
    )
    phase_coverage = covered / len(used_coords) if used_coords else 0.0
    phase_disabled = "disabled" in header.get("clock scheme", "").lower() or bool(
        packed_phase and packed_phase.disabled
    )
    if not phase_map and not phase_disabled:
        warnings.append("no usable phase map")
    elif phase_map and phase_coverage < 1.0:
        errors.append(f"phase map covers only {phase_coverage:.3f} of used coordinates")
    if invalid_phase_values:
        errors.append(f"invalid phase values on used coordinates: {invalid_phase_values}")

    width, height, area = _layout_area(header)
    if area is None or area <= 0:
        warnings.append("missing or non-positive layout area")
    if bounds is not None and width is not None and height is not None:
        used_width = bounds[2] - bounds[0] + 1
        used_height = bounds[3] - bounds[1] + 1
        if used_width > width or used_height > height:
            errors.append(
                f"used bounds {used_width}x{used_height} exceed declared layout {width}x{height}"
            )
        elif used_width != width or used_height != height:
            warnings.append(
                f"used bounds {used_width}x{used_height} differ from declared layout {width}x{height}"
            )

    phase_conflicts = _phase_conflict_count(header)
    consistency_values = (
        header.get("clock scheme consistency", ""),
        header.get("random phase scheme consistency", ""),
    )
    failed_consistency = any(value.strip().lower() == "failed" for value in consistency_values)
    if phase_conflicts:
        errors.append(f"declared clock conflicts: {phase_conflicts}")
    if failed_consistency:
        errors.append("declared clock consistency failed")

    complete = not errors
    phase_available = bool(phase_map) and phase_coverage >= 1.0
    return IFCNQualityReport(
        complete=complete,
        valid_for_training=(
            complete
            and phase_available
            and not phase_disabled
            and not sequential_training_unsupported
        ),
        errors=tuple(dict.fromkeys(errors)),
        warnings=tuple(dict.fromkeys(warnings)),
        declared_nodes=declared_nodes,
        parsed_nodes=len(nodes),
        declared_edges=declared_edges,
        parsed_paths=len(paths),
        unknown_path_endpoints=unknown_endpoints,
        endpoint_mismatches=endpoint_mismatches,
        non_cardinal_steps=non_cardinal_steps,
        invalid_phase_values=invalid_phase_values,
        phase_coverage=phase_coverage,
        area=area,
        wirelength=float(sum(path.manhattan_length for path in paths)),
        crossings=_maybe_float(header.get("cross count")),
        cell_count=_maybe_float(header.get("cell count")),
        runtime=_maybe_float(header.get("run time", header.get("runtime"))),
        critical_path=_maybe_float(header.get("critical path")),
        phase_conflicts=phase_conflicts,
    )


def parse_ifcn(path: PathLike) -> IFCNLayout:
    """Parse one raw or encoded IFCN layout into a training-ready record."""

    source = Path(path)
    lines = source.read_text(encoding="utf-8", errors="replace").splitlines()
    header, _ = _parse_header(lines)
    (
        explicit_mode_declared,
        explicit_mapping_mode,
        legacy_sequential_flow,
        mapping_metadata_errors,
    ) = _parse_mapping_metadata(lines)
    nodes: List[IFCNNode] = []
    paths: List[IFCNPath] = []
    raw_phase_map: Dict[Coord, int] = {}
    tiles: List[PackedPhaseTile] = []
    errors: List[str] = list(mapping_metadata_errors)
    section: Optional[str] = None
    packed_disabled = False
    mapping_mode = MAPPING_MODE_COMBINATIONAL
    pending_iteration_distance: Optional[int] = None
    pending_iteration_distance_line: Optional[int] = None
    paths_with_explicit_iteration_distance = 0

    def switch_section(next_section: str, line_number: int) -> None:
        nonlocal section, pending_iteration_distance, pending_iteration_distance_line
        if section == "paths" and pending_iteration_distance is not None:
            errors.append(
                f"line {pending_iteration_distance_line}: dangling iteration_distance "
                f"before paths section ended at line {line_number}"
            )
        pending_iteration_distance = None
        pending_iteration_distance_line = None
        section = None if section == next_section else next_section

    for line_number, line in enumerate(lines, start=1):
        stripped = line.strip()
        marker = stripped.lower()
        if marker == "#nodes info":
            switch_section("nodes", line_number)
            continue
        if marker == "#paths info":
            switch_section("paths", line_number)
            continue
        if marker == "#phase map":
            switch_section("raw_phase", line_number)
            continue
        if marker == "#encoded phase map":
            switch_section("packed_phase", line_number)
            continue
        if stripped.startswith("###"):
            if "disabled" in marker and section in {"raw_phase", "packed_phase"}:
                packed_disabled = True
            continue
        if _ITERATION_DISTANCE_PREFIX_RE.match(stripped):
            if section != "paths":
                errors.append(
                    f"line {line_number}: iteration_distance is only valid inside "
                    "the paths section"
                )
                continue
            distance_match = _ITERATION_DISTANCE_RE.match(stripped)
            if pending_iteration_distance is not None:
                errors.append(
                    f"line {pending_iteration_distance_line}: iteration_distance was not "
                    f"consumed before another directive at line {line_number}"
                )
            pending_iteration_distance = None
            pending_iteration_distance_line = None
            if distance_match is None:
                errors.append(f"line {line_number}: malformed iteration_distance")
                continue
            distance = int(distance_match.group(1))
            if distance < 0:
                errors.append(
                    f"line {line_number}: iteration_distance must be non-negative"
                )
                continue
            if distance > _MAX_IFCN_ITERATION_DISTANCE:
                errors.append(
                    f"line {line_number}: IFCN iteration_distance is out of range"
                )
                continue
            pending_iteration_distance = distance
            pending_iteration_distance_line = line_number
            continue
        if not stripped or stripped.startswith("#"):
            continue

        if section == "nodes":
            match = _NODE_RE.match(line)
            if not match:
                errors.append(f"line {line_number}: malformed node")
                continue
            node_id, name, node_type, x, y = match.groups()
            nodes.append(
                IFCNNode(
                    node_id=int(node_id),
                    name=name.strip(),
                    node_type=node_type.strip(),
                    position=(int(x), int(y)),
                )
            )
            continue

        if section == "paths":
            match = _PATH_RE.match(line)
            if not match:
                errors.append(f"line {line_number}: malformed path")
                continue
            source_id, target_id, point_text = match.groups()
            points = tuple((int(x), int(y)) for x, y in _COORD_RE.findall(point_text))
            if pending_iteration_distance is not None:
                paths_with_explicit_iteration_distance += 1
            paths.append(
                IFCNPath(
                    source=int(source_id),
                    target=int(target_id),
                    points=points,
                    iteration_distance=(
                        pending_iteration_distance
                        if pending_iteration_distance is not None
                        else 0
                    ),
                )
            )
            pending_iteration_distance = None
            pending_iteration_distance_line = None
            continue

        if section == "raw_phase":
            tile_matches = list(_PACKED_TILE_RE.finditer(line))
            if tile_matches:
                tiles.extend(
                    PackedPhaseTile(
                        block_x=int(match.group(1)),
                        block_y=int(match.group(2)),
                        hex_value=match.group(3).lower(),
                        notation="tile",
                    )
                    for match in tile_matches
                )
                continue
            matches = list(_RAW_PHASE_RE.finditer(line))
            if matches:
                for match in matches:
                    raw_phase_map[(int(match.group(1)), int(match.group(2)))] = int(match.group(3))
            else:
                errors.append(f"line {line_number}: malformed phase entry")
            continue

        if section == "packed_phase":
            matches = list(_PACKED_BLOCK_RE.finditer(line))
            if matches:
                tiles.extend(
                    PackedPhaseTile(
                        block_x=int(match.group(1)),
                        block_y=int(match.group(2)),
                        hex_value=match.group(3).lower(),
                        notation="block",
                    )
                    for match in matches
                )
            else:
                errors.append(f"line {line_number}: malformed packed phase entry")

    if section == "paths" and pending_iteration_distance is not None:
        errors.append(
            f"line {pending_iteration_distance_line}: dangling iteration_distance at end of file"
        )

    positive_iteration_distance = any(
        path.iteration_distance > 0 for path in paths
    )
    if explicit_mode_declared:
        if explicit_mapping_mode is not None:
            mapping_mode = explicit_mapping_mode
            if (
                mapping_mode == MAPPING_MODE_COMBINATIONAL
                and positive_iteration_distance
            ):
                errors.append(
                    "combinational IFCN declares a positive iteration distance"
                )
            if (
                mapping_mode == MAPPING_MODE_SEQUENTIAL
                and paths_with_explicit_iteration_distance != len(paths)
            ):
                errors.append(
                    "sequential IFCN requires iteration_distance before every route"
                )
    elif positive_iteration_distance or legacy_sequential_flow:
        mapping_mode = MAPPING_MODE_SEQUENTIAL

    packed_phase = _packed_metadata(header, tiles, packed_disabled)
    enriched_nodes, enriched_paths, bounds = _enrich_geometry(nodes, paths)
    topology_hash = topology_fingerprint(
        enriched_nodes, enriched_paths, mapping_mode=mapping_mode
    )
    effective_phase = raw_phase_map or (packed_phase.decode() if packed_phase is not None else {})
    layout_hash = _layout_fingerprint(
        enriched_nodes,
        enriched_paths,
        effective_phase,
        topology_hash,
        mapping_mode=mapping_mode,
    )
    quality = _build_quality_report(
        header,
        mapping_mode,
        enriched_nodes,
        enriched_paths,
        raw_phase_map,
        packed_phase,
        bounds,
        errors,
    )
    return IFCNLayout(
        source_path=str(source.resolve()),
        header=header,
        nodes=enriched_nodes,
        paths=enriched_paths,
        raw_phase_map=raw_phase_map,
        packed_phase=packed_phase,
        topology_hash=topology_hash,
        layout_hash=layout_hash,
        quality=quality,
        bounds=bounds,
        mapping_mode=mapping_mode,
    )


def _iter_ifcn_files(roots: Union[PathLike, Sequence[PathLike]], recursive: bool) -> Iterator[Path]:
    root_values: Sequence[PathLike]
    if isinstance(roots, (str, Path)):
        root_values = [roots]
    else:
        root_values = roots
    seen: set[str] = set()
    for root_value in root_values:
        root = Path(root_value)
        candidates = [root] if root.is_file() else (
            root.rglob("*.ifcn") if recursive else root.glob("*.ifcn")
        )
        for candidate in candidates:
            if not candidate.is_file() or candidate.suffix.lower() != ".ifcn":
                continue
            resolved = str(candidate.resolve())
            if resolved not in seen:
                seen.add(resolved)
                yield candidate


def iter_ifcn(
    roots: Union[PathLike, Sequence[PathLike]],
    *,
    recursive: bool = True,
    valid_only: bool = False,
    strict: bool = False,
) -> Iterator[IFCNLayout]:
    """Yield parsed files from a deterministic recursive IFCN scan.

    Unreadable files are skipped unless ``strict`` is true.  Parseable but
    incomplete layouts are yielded by default so callers can audit data loss;
    pass ``valid_only=True`` for records suitable for model training.
    """

    for path in sorted(_iter_ifcn_files(roots, recursive), key=lambda item: str(item.resolve())):
        try:
            record = parse_ifcn(path)
        except (OSError, UnicodeError, ValueError):
            if strict:
                raise
            continue
        if not valid_only or record.quality.valid_for_training:
            yield record


def scan_ifcn(
    roots: Union[PathLike, Sequence[PathLike]],
    *,
    recursive: bool = True,
    valid_only: bool = False,
    strict: bool = False,
) -> List[IFCNLayout]:
    """Return all records from :func:`iter_ifcn` as a list."""

    return list(
        iter_ifcn(
            roots,
            recursive=recursive,
            valid_only=valid_only,
            strict=strict,
        )
    )


def _representation_rank(record: IFCNLayout) -> Tuple[int, int, int, int]:
    return (
        int(record.quality.valid_for_training),
        int(record.phase_encoding == "raw"),
        -len(record.quality.errors),
        -len(record.quality.warnings),
    )


def deduplicate_layouts(records: Iterable[IFCNLayout]) -> List[IFCNLayout]:
    """Remove semantic layout duplicates while retaining the best representation."""

    chosen: Dict[str, IFCNLayout] = {}

    for record in sorted(records, key=lambda item: item.source_path):
        previous = chosen.get(record.layout_hash)
        if previous is None or _representation_rank(record) > _representation_rank(previous):
            chosen[record.layout_hash] = record
    return sorted(chosen.values(), key=lambda item: (item.topology_hash, item.source_path))


DEFAULT_PARETO_OBJECTIVES = ("area", "wirelength", "crossings", "cell_count", "runtime")


def _dominates(
    first: IFCNLayout, second: IFCNLayout, objectives: Sequence[str]
) -> bool:
    first_values = [first.quality.objective(objective) for objective in objectives]
    second_values = [second.quality.objective(objective) for objective in objectives]
    return all(left <= right for left, right in zip(first_values, second_values)) and any(
        left < right for left, right in zip(first_values, second_values)
    )


def pareto_frontier(
    records: Iterable[IFCNLayout],
    *,
    objectives: Sequence[str] = DEFAULT_PARETO_OBJECTIVES,
) -> List[IFCNLayout]:
    """Return non-dominated layouts for a single topology (all minimised)."""

    candidates = list(records)
    if not objectives:
        raise ValueError("at least one Pareto objective is required")
    topology_hashes = {record.topology_hash for record in candidates}
    if len(topology_hashes) > 1:
        raise ValueError("pareto_frontier expects records from one topology")
    frontier = [
        candidate
        for index, candidate in enumerate(candidates)
        if not any(
            other_index != index and _dominates(other, candidate, objectives)
            for other_index, other in enumerate(candidates)
        )
    ]
    return sorted(frontier, key=lambda item: tuple(item.quality.objective(name) for name in objectives))


def pareto_by_topology(
    records: Iterable[IFCNLayout],
    *,
    objectives: Sequence[str] = DEFAULT_PARETO_OBJECTIVES,
) -> List[IFCNLayout]:
    groups: Dict[str, List[IFCNLayout]] = defaultdict(list)
    for record in records:
        groups[record.topology_hash].append(record)
    result: List[IFCNLayout] = []
    for topology_hash in sorted(groups):
        result.extend(pareto_frontier(groups[topology_hash], objectives=objectives))
    return result


def build_ifcn_dataset(
    roots: Union[PathLike, Sequence[PathLike]],
    *,
    recursive: bool = True,
    valid_only: bool = True,
    deduplicate: bool = True,
    pareto_only: bool = True,
    objectives: Sequence[str] = DEFAULT_PARETO_OBJECTIVES,
) -> List[IFCNLayout]:
    """Scan, quality-filter, de-duplicate, and optionally Pareto-filter IFCN data."""

    if deduplicate:
        # De-duplicate while scanning so repeated raw maps and long paths do not
        # all coexist in memory.  This matters for the repository's many copied
        # experiment outputs.
        chosen: Dict[str, IFCNLayout] = {}
        for record in iter_ifcn(roots, recursive=recursive, valid_only=valid_only):
            previous = chosen.get(record.layout_hash)
            if previous is None or _representation_rank(record) > _representation_rank(previous):
                chosen[record.layout_hash] = record
        records = sorted(
            chosen.values(), key=lambda item: (item.topology_hash, item.source_path)
        )
    else:
        records = scan_ifcn(roots, recursive=recursive, valid_only=valid_only)
    if pareto_only:
        records = pareto_by_topology(records, objectives=objectives)
    return records


def _partition_counts(total: int, ratios: Sequence[float]) -> List[int]:
    exact = [total * ratio / sum(ratios) for ratio in ratios]
    counts = [math.floor(value) for value in exact]
    remainder = total - sum(counts)
    order = sorted(range(len(ratios)), key=lambda index: (-(exact[index] - counts[index]), index))
    for index in order[:remainder]:
        counts[index] += 1
    return counts


def split_by_topology(
    records: Iterable[IFCNLayout],
    *,
    train_ratio: float = 0.8,
    val_ratio: float = 0.1,
    test_ratio: float = 0.1,
    seed: int = 0,
) -> Dict[str, List[IFCNLayout]]:
    """Deterministically split complete topology groups without leakage."""

    ratios = (float(train_ratio), float(val_ratio), float(test_ratio))
    if any(ratio < 0 for ratio in ratios) or sum(ratios) <= 0:
        raise ValueError("split ratios must be non-negative and have a positive sum")
    groups: Dict[str, List[IFCNLayout]] = defaultdict(list)
    for record in records:
        groups[record.topology_hash].append(record)
    topology_order = sorted(
        groups,
        key=lambda topology_hash: hashlib.sha256(
            f"{int(seed)}:{topology_hash}".encode("ascii")
        ).digest(),
    )
    counts = _partition_counts(len(topology_order), ratios)
    names = ("train", "val", "test")
    result: Dict[str, List[IFCNLayout]] = {name: [] for name in names}
    cursor = 0
    for name, count in zip(names, counts):
        assigned = topology_order[cursor : cursor + count]
        cursor += count
        result[name] = sorted(
            (record for topology_hash in assigned for record in groups[topology_hash]),
            key=lambda item: (item.topology_hash, item.source_path),
        )
    assert_no_topology_leakage(result)
    return result


def assert_no_topology_leakage(splits: Mapping[str, Sequence[IFCNLayout]]) -> None:
    owners: Dict[str, str] = {}
    for split_name, records in splits.items():
        for topology_hash in {record.topology_hash for record in records}:
            previous = owners.setdefault(topology_hash, split_name)
            if previous != split_name:
                raise ValueError(
                    f"topology {topology_hash} appears in both {previous!r} and {split_name!r}"
                )


def save_jsonl_index(records: Iterable[IFCNLayout], path: PathLike) -> Path:
    """Save full parsed records as a deterministic, streamable JSONL index."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="utf-8", newline="\n") as handle:
        for record in records:
            json.dump(record.to_dict(), handle, sort_keys=True, separators=(",", ":"))
            handle.write("\n")
    return destination


def load_jsonl_index(path: PathLike) -> List[IFCNLayout]:
    records: List[IFCNLayout] = []
    with Path(path).open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                records.append(IFCNLayout.from_dict(json.loads(line)))
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
                raise ValueError(f"invalid IFCN JSONL index at line {line_number}: {error}") from error
    return records


# Short aliases are convenient in training scripts while the explicit names
# remain discoverable for data tooling.
save_jsonl = save_jsonl_index
load_jsonl = load_jsonl_index


__all__ = [
    "DEFAULT_PARETO_OBJECTIVES",
    "IFCNLayout",
    "IFCNNode",
    "IFCNPath",
    "IFCNQualityReport",
    "MAPPING_MODE_COMBINATIONAL",
    "MAPPING_MODE_SEQUENTIAL",
    "PackedPhaseMetadata",
    "PackedPhaseTile",
    "assert_no_topology_leakage",
    "build_ifcn_dataset",
    "deduplicate_layouts",
    "load_jsonl",
    "load_jsonl_index",
    "normalize_mapping_mode",
    "iter_ifcn",
    "pareto_by_topology",
    "pareto_frontier",
    "parse_ifcn",
    "save_jsonl",
    "save_jsonl_index",
    "scan_ifcn",
    "split_by_topology",
    "topology_fingerprint",
]
