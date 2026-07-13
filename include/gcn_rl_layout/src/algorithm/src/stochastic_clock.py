"""Causal stochastic clock fields and robust multi-field objectives.

The legacy random-phase router assigns phases while routing.  This module models
the other problem: placement and routing on a clock field that is sampled first
and then kept immutable for the whole episode.

The primary representation is an absolute clock stage ``tau``.  A cell phase is
derived as ``tau % phase_count``.  Keeping ``tau`` avoids treating two cells four
clock periods apart as if they were in the same clock zone.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import cached_property
import hashlib
import json
import math
import random
from statistics import fmean
from typing import Iterable, Mapping, Protocol, Sequence


Coord = tuple[int, int]
Bounds = tuple[int, int, int, int]

CAUSAL_CLOCK_MODES = ("axis", "diagonal", "stochastic-bands")
STRESS_CLOCK_MODES = ("raw",)
SUPPORTED_CLOCK_MODES = CAUSAL_CLOCK_MODES + STRESS_CLOCK_MODES


class PackedPhaseBoard(Protocol):
    """Minimal pybind board protocol used by :func:`install_phase_field`."""

    def setPackedPhaseBlock4x4(self, origin: Coord, packed_code: int) -> None: ...


def _positive_mod(value: int, modulus: int) -> int:
    return int(value) % int(modulus)


def _validate_bounds(bounds: Bounds) -> Bounds:
    min_x, min_y, max_x, max_y = (int(value) for value in bounds)
    if max_x < min_x or max_y < min_y:
        raise ValueError(f"invalid inclusive bounds: {bounds!r}")
    return min_x, min_y, max_x, max_y


def _floor_to_multiple(value: int, block_size: int) -> int:
    return math.floor(int(value) / int(block_size)) * int(block_size)


def align_bounds_to_blocks(bounds: Bounds, block_size: int = 4) -> Bounds:
    """Expand inclusive bounds so every covered packed block is complete."""

    min_x, min_y, max_x, max_y = _validate_bounds(bounds)
    block_size = int(block_size)
    if block_size <= 0:
        raise ValueError("block_size must be positive")
    aligned_min_x = _floor_to_multiple(min_x, block_size)
    aligned_min_y = _floor_to_multiple(min_y, block_size)
    aligned_max_x = _floor_to_multiple(max_x, block_size) + block_size - 1
    aligned_max_y = _floor_to_multiple(max_y, block_size) + block_size - 1
    return aligned_min_x, aligned_min_y, aligned_max_x, aligned_max_y


@dataclass(frozen=True)
class ClockFieldSpec:
    """Configuration for one immutable spatial clock realization.

    ``primary_direction`` and ``secondary_direction`` define the two monotone
    directions in which the causal modes are guaranteed to expose transitions
    of stage delta 0 or +1.  The primary direction always advances by one stage.
    """

    seed: int
    phase_count: int = 4
    mode: str = "stochastic-bands"
    primary_axis: str = "x"
    primary_direction: int = 1
    secondary_direction: int = 1
    secondary_advance_probability: float = 0.5
    block_size: int = 4

    def __post_init__(self) -> None:
        if int(self.phase_count) not in (3, 4):
            raise ValueError("phase_count must be 3 or 4")
        if str(self.mode) not in SUPPORTED_CLOCK_MODES:
            raise ValueError(
                f"unsupported clock mode {self.mode!r}; expected {SUPPORTED_CLOCK_MODES}"
            )
        if str(self.primary_axis) not in ("x", "y"):
            raise ValueError("primary_axis must be 'x' or 'y'")
        if int(self.primary_direction) not in (-1, 1):
            raise ValueError("primary_direction must be -1 or 1")
        if int(self.secondary_direction) not in (-1, 1):
            raise ValueError("secondary_direction must be -1 or 1")
        probability = float(self.secondary_advance_probability)
        if not 0.0 <= probability <= 1.0:
            raise ValueError("secondary_advance_probability must be in [0, 1]")
        if int(self.block_size) != 4:
            raise ValueError("the current IFCN packed phase format requires block_size=4")


@dataclass(frozen=True)
class ClockField:
    """One sampled clock field over inclusive, block-aligned bounds."""

    spec: ClockFieldSpec
    bounds: Bounds
    stages: Mapping[Coord, int]
    phases: Mapping[Coord, int]
    causal: bool
    secondary_advance_ratio: float

    def stage_at(self, coord: Coord) -> int:
        try:
            return int(self.stages[(int(coord[0]), int(coord[1]))])
        except KeyError as exc:
            raise KeyError(f"coordinate {coord!r} is outside clock field {self.bounds!r}") from exc

    def phase_at(self, coord: Coord) -> int:
        try:
            return int(self.phases[(int(coord[0]), int(coord[1]))])
        except KeyError as exc:
            raise KeyError(f"coordinate {coord!r} is outside clock field {self.bounds!r}") from exc

    def transition_allowed(self, source: Coord, target: Coord) -> bool:
        """Return whether an adjacent move holds or advances one absolute stage."""

        if abs(int(source[0]) - int(target[0])) + abs(int(source[1]) - int(target[1])) != 1:
            return False
        delta = self.stage_at(target) - self.stage_at(source)
        return delta in (0, 1)

    @cached_property
    def field_hash(self) -> str:
        payload = {
            "spec": {
                "seed": int(self.spec.seed),
                "phase_count": int(self.spec.phase_count),
                "mode": str(self.spec.mode),
                "primary_axis": str(self.spec.primary_axis),
                "primary_direction": int(self.spec.primary_direction),
                "secondary_direction": int(self.spec.secondary_direction),
                "secondary_advance_probability": float(
                    self.spec.secondary_advance_probability
                ),
            },
            "bounds": list(self.bounds),
            "stages": [
                [int(x), int(y), int(stage)]
                for (x, y), stage in sorted(self.stages.items())
            ],
        }
        encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()[:20]

    def descriptor(self) -> tuple[float, ...]:
        """Fixed-size context vector suitable for a graph policy."""

        mode_one_hot = [float(self.spec.mode == mode) for mode in SUPPORTED_CLOCK_MODES]
        return tuple(
            [
                float(self.spec.phase_count) / 4.0,
                float(self.spec.primary_axis == "x"),
                float(self.spec.primary_direction),
                float(self.spec.secondary_direction),
                float(self.secondary_advance_ratio),
                float(self.causal),
            ]
            + mode_one_hot
        )


def _axis_values(bounds: Bounds, axis: str) -> range:
    min_x, min_y, max_x, max_y = bounds
    return range(min_x, max_x + 1) if axis == "x" else range(min_y, max_y + 1)


def _coord_axes(coord: Coord, primary_axis: str) -> tuple[int, int]:
    return (int(coord[0]), int(coord[1])) if primary_axis == "x" else (int(coord[1]), int(coord[0]))


def sample_clock_field(bounds: Bounds, spec: ClockFieldSpec) -> ClockField:
    """Sample a deterministic field from ``spec.seed``.

    Causal modes use ``tau(p, s) = direction*p + offset(s)``.  Consecutive
    offsets in the configured secondary direction differ by 0 or 1, so the
    resulting bands are connected and cannot cross.  ``raw`` deliberately drops
    this guarantee and exists only for adversarial stress tests.
    """

    aligned_bounds = align_bounds_to_blocks(bounds, spec.block_size)
    min_x, min_y, max_x, max_y = aligned_bounds
    rng = random.Random(int(spec.seed))
    coords = [
        (x, y)
        for y in range(min_y, max_y + 1)
        for x in range(min_x, max_x + 1)
    ]

    if spec.mode == "raw":
        phases = {
            coord: rng.randrange(int(spec.phase_count))
            for coord in coords
        }
        # ``stage=phase`` makes the projection explicit but does not claim a
        # globally causal order for raw stress fields.
        stages = dict(phases)
        return ClockField(
            spec=spec,
            bounds=aligned_bounds,
            stages=stages,
            phases=phases,
            causal=False,
            # Keep policy context finite; ``causal`` and the mode one-hot carry
            # the fact that this is an adversarial, unconstrained field.
            secondary_advance_ratio=0.0,
        )

    secondary_axis = "y" if spec.primary_axis == "x" else "x"
    secondary_values = list(_axis_values(aligned_bounds, secondary_axis))
    secondary_values.sort(key=lambda value: int(spec.secondary_direction) * int(value))

    if spec.mode == "axis":
        increments = [0] * max(0, len(secondary_values) - 1)
    elif spec.mode == "diagonal":
        increments = [1] * max(0, len(secondary_values) - 1)
    else:
        increments = [
            int(rng.random() < float(spec.secondary_advance_probability))
            for _ in range(max(0, len(secondary_values) - 1))
        ]

    offsets: dict[int, int] = {}
    running_offset = rng.randrange(int(spec.phase_count))
    for index, value in enumerate(secondary_values):
        if index:
            running_offset += int(increments[index - 1])
        offsets[int(value)] = int(running_offset)

    raw_stages: dict[Coord, int] = {}
    for coord in coords:
        primary, secondary = _coord_axes(coord, spec.primary_axis)
        raw_stages[coord] = (
            int(spec.primary_direction) * int(primary) + offsets[int(secondary)]
        )
    minimum_stage = min(raw_stages.values(), default=0)
    stages = {coord: int(stage - minimum_stage) for coord, stage in raw_stages.items()}
    phases = {
        coord: _positive_mod(stage, int(spec.phase_count))
        for coord, stage in stages.items()
    }
    advance_ratio = fmean(increments) if increments else 0.0
    field = ClockField(
        spec=spec,
        bounds=aligned_bounds,
        stages=stages,
        phases=phases,
        causal=True,
        secondary_advance_ratio=float(advance_ratio),
    )
    report = validate_causal_field(field)
    if not report["valid"]:
        raise RuntimeError(f"causal clock sampler produced an invalid field: {report}")
    return field


def validate_causal_field(field: ClockField) -> dict[str, int | bool | float]:
    """Validate the sampler's two guaranteed monotone transition directions."""

    min_x, min_y, max_x, max_y = field.bounds
    if field.spec.primary_axis == "x":
        primary_step = (int(field.spec.primary_direction), 0)
        secondary_step = (0, int(field.spec.secondary_direction))
    else:
        primary_step = (0, int(field.spec.primary_direction))
        secondary_step = (int(field.spec.secondary_direction), 0)

    checked = 0
    illegal = 0
    primary_non_advance = 0
    for x, y in field.stages:
        for step, require_advance in ((primary_step, True), (secondary_step, False)):
            target = (x + step[0], y + step[1])
            if not (min_x <= target[0] <= max_x and min_y <= target[1] <= max_y):
                continue
            checked += 1
            delta = field.stage_at(target) - field.stage_at((x, y))
            if delta not in (0, 1):
                illegal += 1
            if require_advance and delta != 1:
                primary_non_advance += 1
    return {
        "valid": bool(field.causal and illegal == 0 and primary_non_advance == 0),
        "checked_transitions": int(checked),
        "illegal_transitions": int(illegal),
        "primary_non_advance": int(primary_non_advance),
    }


def pack_phase_block(field: ClockField, origin: Coord) -> int:
    """Encode one 4x4 block using MapChessboard's row-byte convention."""

    origin_x, origin_y = (int(origin[0]), int(origin[1]))
    if origin_x % 4 or origin_y % 4:
        raise ValueError("packed 4x4 block origin must align to multiples of 4")
    packed = 0
    for local_y in range(4):
        row_byte = 0
        for local_x in range(4):
            phase = field.phase_at((origin_x + local_x, origin_y + local_y))
            if not 0 <= phase <= 3:
                raise ValueError(f"phase {phase} cannot be represented with two bits")
            row_byte |= (int(phase) & 0x3) << (2 * local_x)
        packed |= int(row_byte) << (8 * (3 - local_y))
    return int(packed)


def iter_packed_phase_blocks(field: ClockField) -> Iterable[tuple[Coord, int]]:
    min_x, min_y, max_x, max_y = field.bounds
    for origin_y in range(min_y, max_y + 1, 4):
        for origin_x in range(min_x, max_x + 1, 4):
            origin = (origin_x, origin_y)
            yield origin, pack_phase_block(field, origin)


def install_phase_field(board: PackedPhaseBoard, field: ClockField) -> None:
    """Install the phase projection on an existing pybind ``MapChessboard``.

    Absolute stages remain in ``ClockField`` until the IFCN/C++ schema gains an
    explicit stage map.  Routing code should therefore retain the field object
    for absolute-stage DRC rather than relying only on the board projection.
    """

    for origin, packed_code in iter_packed_phase_blocks(field):
        board.setPackedPhaseBlock4x4(origin, int(packed_code))


@dataclass(frozen=True)
class ClockEvaluation:
    """Exact result for one frozen clock realization."""

    legal: bool
    cost: float
    failed_edges: int = 0
    direction_violations: int = 0
    clock_violations: int = 0
    area: float = 0.0
    runtime_sec: float = 0.0
    field_hash: str = ""

    @property
    def violation_count(self) -> int:
        return (
            int(self.failed_edges)
            + int(self.direction_violations)
            + int(self.clock_violations)
        )


@dataclass(frozen=True)
class RobustClockMetrics:
    sample_count: int
    legal_count: int
    success_rate: float
    mean_cost: float
    cvar_cost: float
    worst_cost: float
    mean_violations: float
    cvar_violations: float
    mean_area: float
    mean_runtime_sec: float
    robust_loss: float

    def selection_key(self, minimum_success_rate: float = 0.95) -> tuple[float, ...]:
        """Legality-first key; smaller is better."""

        misses_target = float(self.success_rate + 1e-12 < float(minimum_success_rate))
        return (
            misses_target,
            -float(self.success_rate),
            float(self.cvar_violations),
            float(self.mean_violations),
            float(self.cvar_cost),
            float(self.mean_cost),
        )


def _upper_tail_mean(values: Sequence[float], alpha: float) -> float:
    if not values:
        raise ValueError("CVaR requires at least one value")
    if not 0.0 <= float(alpha) < 1.0:
        raise ValueError("cvar_alpha must be in [0, 1)")
    tail_count = max(1, int(math.ceil((1.0 - float(alpha)) * len(values))))
    return float(fmean(sorted((float(value) for value in values), reverse=True)[:tail_count]))


def aggregate_clock_evaluations(
    evaluations: Sequence[ClockEvaluation],
    *,
    cvar_alpha: float = 0.9,
    risk_weight: float = 1.0,
    failure_penalty: float = 100_000.0,
    violation_penalty: float = 10_000.0,
) -> RobustClockMetrics:
    """Aggregate exact results as mean loss plus upper-tail (CVaR) loss."""

    if not evaluations:
        raise ValueError("at least one clock evaluation is required")
    losses = []
    costs = []
    violations = []
    for evaluation in evaluations:
        violation_count = int(evaluation.violation_count)
        cost = float(evaluation.cost)
        loss = cost + float(violation_penalty) * violation_count
        if not bool(evaluation.legal):
            loss += float(failure_penalty)
        costs.append(cost)
        violations.append(float(violation_count))
        losses.append(float(loss))

    mean_loss = float(fmean(losses))
    tail_loss = _upper_tail_mean(losses, cvar_alpha)
    legal_count = sum(bool(evaluation.legal) for evaluation in evaluations)
    return RobustClockMetrics(
        sample_count=len(evaluations),
        legal_count=int(legal_count),
        success_rate=float(legal_count) / float(len(evaluations)),
        mean_cost=float(fmean(costs)),
        cvar_cost=_upper_tail_mean(costs, cvar_alpha),
        worst_cost=float(max(costs)),
        mean_violations=float(fmean(violations)),
        cvar_violations=_upper_tail_mean(violations, cvar_alpha),
        mean_area=float(fmean(float(item.area) for item in evaluations)),
        mean_runtime_sec=float(fmean(float(item.runtime_sec) for item in evaluations)),
        robust_loss=float(mean_loss + float(risk_weight) * tail_loss),
    )
