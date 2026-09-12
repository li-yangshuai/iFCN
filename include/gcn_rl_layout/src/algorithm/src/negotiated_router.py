"""Fast negotiated router for a fixed 2DDWave clock field.

The router deliberately avoids grid A*.  Every candidate is a monotone
right/down L or Z path with hard endpoint directions.  Routing is global: wire
overlap is negotiated between complete source fanout trees instead of making
the first committed edge a permanent obstacle for every later edge.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Set, Tuple


Coord = Tuple[int, int]
Edge = Tuple[int, int]
Direction = Tuple[int, int]


@dataclass(frozen=True)
class RouteRequest:
    src: int
    dst: int
    start: Coord
    goal: Coord
    fanout_dir: Direction
    fanin_dir: Direction

    @property
    def edge(self) -> Edge:
        return int(self.src), int(self.dst)


@dataclass(frozen=True)
class RoutingConflict:
    kind: str
    coord: Optional[Coord]
    edges: Tuple[Edge, ...]


@dataclass
class NegotiatedRoutingResult:
    paths: Dict[Edge, List[Coord]] = field(default_factory=dict)
    failed_edges: Set[Edge] = field(default_factory=set)
    conflicts: List[RoutingConflict] = field(default_factory=list)
    iterations: int = 0
    overflow_history: Dict[Coord, float] = field(default_factory=dict)


class MonotoneNegotiatedRouter:
    """Bounded-candidate Manhattan router with Pathfinder-style history cost."""

    _VALID_FANOUTS = {(1, 0), (0, 1)}
    _VALID_FANINS = {(-1, 0), (0, -1)}

    def __init__(
        self,
        max_tracks: int = 10,
        max_iterations: int = 24,
        bend_cost: float = 0.35,
        crossover_cost: float = 3.0,
        present_penalty: float = 8.0,
        history_increment: float = 2.0,
        allow_crossovers: bool = False,
        compound_detours: bool = True,
    ):
        self.max_tracks = max(2, int(max_tracks))
        self.max_iterations = max(1, int(max_iterations))
        self.bend_cost = float(bend_cost)
        self.crossover_cost = float(crossover_cost)
        self.present_penalty = float(present_penalty)
        self.history_increment = float(history_increment)
        self.allow_crossovers = bool(allow_crossovers)
        self.compound_detours = bool(compound_detours)

    @staticmethod
    def _append_segment(path: List[Coord], target: Coord) -> bool:
        if not path:
            return False
        x, y = path[-1]
        tx, ty = int(target[0]), int(target[1])
        if tx < x or ty < y or (tx != x and ty != y):
            return False
        dx = 1 if tx > x else 0
        dy = 1 if ty > y else 0
        while (x, y) != (tx, ty):
            x += dx
            y += dy
            path.append((x, y))
        return True

    @staticmethod
    def _track_values(lo: int, hi: int, limit: int) -> List[int]:
        lo, hi = int(lo), int(hi)
        if hi < lo:
            return []
        span = hi - lo + 1
        if span <= limit:
            return list(range(lo, hi + 1))

        values = {lo, min(hi, lo + 1), max(lo, hi - 1), hi}
        remaining = max(0, int(limit) - len(values))
        for index in range(1, remaining + 1):
            value = lo + round(index * (hi - lo) / (remaining + 1))
            values.add(int(value))
        return sorted(values)[:limit]

    @staticmethod
    def _compound_track_values(lo: int, hi: int) -> List[int]:
        """Small deterministic track set for four-bend detours."""
        lo, hi = int(lo), int(hi)
        if hi < lo:
            return []
        if hi - lo <= 3:
            return list(range(lo, hi + 1))
        return sorted(
            {
                lo,
                lo + (hi - lo) // 3,
                lo + (2 * (hi - lo)) // 3,
                hi,
            }
        )

    @staticmethod
    def _path_is_monotone(path: Sequence[Coord]) -> bool:
        if not path:
            return False
        return all(
            (bx - ax, by - ay) in {(1, 0), (0, 1)}
            for (ax, ay), (bx, by) in zip(path, path[1:])
        )

    @staticmethod
    def _endpoint_dirs(path: Sequence[Coord]) -> Tuple[Optional[Direction], Optional[Direction]]:
        if len(path) < 2:
            return None, None
        fanout = (
            int(path[1][0]) - int(path[0][0]),
            int(path[1][1]) - int(path[0][1]),
        )
        fanin = (
            int(path[-2][0]) - int(path[-1][0]),
            int(path[-2][1]) - int(path[-1][1]),
        )
        return fanout, fanin

    @staticmethod
    def _bend_count(path: Sequence[Coord]) -> int:
        if len(path) < 3:
            return 0
        directions = [
            (bx - ax, by - ay)
            for (ax, ay), (bx, by) in zip(path, path[1:])
        ]
        return sum(a != b for a, b in zip(directions, directions[1:]))

    @staticmethod
    def _cell_orientation(path: Sequence[Coord], index: int) -> str:
        """Return H/V for a straight internal cell and B for a bend."""
        if index <= 0 or index + 1 >= len(path):
            return "E"
        prev_coord = path[index - 1]
        coord = path[index]
        next_coord = path[index + 1]
        before = (coord[0] - prev_coord[0], coord[1] - prev_coord[1])
        after = (next_coord[0] - coord[0], next_coord[1] - coord[1])
        if before == after == (1, 0):
            return "H"
        if before == after == (0, 1):
            return "V"
        return "B"

    def _finalize_candidate(
        self,
        request: RouteRequest,
        path: List[Coord],
        blocked_coords: Set[Coord],
    ) -> Optional[List[Coord]]:
        if not path or path[0] != request.start or path[-1] != request.goal:
            return None
        if not self._path_is_monotone(path):
            return None
        actual_out, actual_in = self._endpoint_dirs(path)
        if actual_out != request.fanout_dir or actual_in != request.fanin_dir:
            return None
        if any(coord in blocked_coords for coord in path[1:-1]):
            return None
        return path

    def generate_candidates(
        self,
        request: RouteRequest,
        blocked_coords: Iterable[Coord],
    ) -> List[List[Coord]]:
        """Enumerate direct L and bounded-track Z paths for one edge."""
        blocked = {(int(x), int(y)) for x, y in blocked_coords}
        start = (int(request.start[0]), int(request.start[1]))
        goal = (int(request.goal[0]), int(request.goal[1]))
        fanout = (int(request.fanout_dir[0]), int(request.fanout_dir[1]))
        fanin = (int(request.fanin_dir[0]), int(request.fanin_dir[1]))

        if fanout not in self._VALID_FANOUTS or fanin not in self._VALID_FANINS:
            return []
        if goal[0] < start[0] or goal[1] < start[1] or start == goal:
            return []

        first = (start[0] + fanout[0], start[1] + fanout[1])
        pre_goal = (goal[0] + fanin[0], goal[1] + fanin[1])

        if first == goal:
            if pre_goal != start:
                return []
            path = [start, goal]
            candidate = self._finalize_candidate(request, path, blocked)
            return [candidate] if candidate else []

        if (
            pre_goal[0] < first[0]
            or pre_goal[1] < first[1]
            or first in blocked
            or pre_goal in blocked
        ):
            return []

        candidates: List[List[Coord]] = []
        seen: Set[Tuple[Coord, ...]] = set()

        def add_waypoints(waypoints: Sequence[Coord]) -> None:
            path = [start]
            for target in waypoints:
                if not self._append_segment(path, target):
                    return
            candidate = self._finalize_candidate(request, path, blocked)
            if candidate is None:
                return
            key = tuple(candidate)
            if key not in seen:
                seen.add(key)
                candidates.append(candidate)

        # Direct L candidates appear as the endpoint tracks below.  Interior
        # tracks form V-H-V and H-V-H Z candidates.
        for track_y in self._track_values(first[1], pre_goal[1], self.max_tracks):
            add_waypoints(
                [
                    first,
                    (first[0], track_y),
                    (pre_goal[0], track_y),
                    pre_goal,
                    goal,
                ]
            )
        for track_x in self._track_values(first[0], pre_goal[0], self.max_tracks):
            add_waypoints(
                [
                    first,
                    (track_x, first[1]),
                    (track_x, pre_goal[1]),
                    pre_goal,
                    goal,
                ]
            )

        # A small H-V-H-V / V-H-V-H family can bypass two independent
        # obstacles while keeping candidate count bounded (at most 32 extra
        # paths). This is the non-A* fallback for nets that need more than one
        # dogleg.
        if self.compound_detours:
            compound_x = self._compound_track_values(first[0], pre_goal[0])
            compound_y = self._compound_track_values(first[1], pre_goal[1])
            for track_x in compound_x:
                for track_y in compound_y:
                    add_waypoints(
                        [
                            first,
                            (track_x, first[1]),
                            (track_x, track_y),
                            (pre_goal[0], track_y),
                            pre_goal,
                            goal,
                        ]
                    )
                    add_waypoints(
                        [
                            first,
                            (first[0], track_y),
                            (track_x, track_y),
                            (track_x, pre_goal[1]),
                            pre_goal,
                            goal,
                        ]
                    )

        candidates.sort(key=lambda path: (self._bend_count(path), len(path), tuple(path)))
        return candidates

    @staticmethod
    def _build_usage(
        paths: Mapping[Edge, Sequence[Coord]],
        request_by_edge: Mapping[Edge, RouteRequest],
    ) -> Dict[Coord, List[Tuple[Edge, int, str]]]:
        usage: Dict[Coord, List[Tuple[Edge, int, str]]] = defaultdict(list)
        for edge, path in paths.items():
            source = int(request_by_edge[edge].src)
            for index in range(1, len(path) - 1):
                usage[path[index]].append(
                    (edge, source, MonotoneNegotiatedRouter._cell_orientation(path, index))
                )
        return usage

    def _usage_is_legal(self, entries: Sequence[Tuple[Edge, int, str]]) -> bool:
        sources = {source for _, source, _ in entries}
        if len(sources) <= 1:
            return True
        if not self.allow_crossovers or len(sources) != 2:
            return False

        # The cell-level mapper implements a local orthogonal crossover: one
        # straight horizontal source tree and one straight vertical source tree
        # may occupy the same gate-level tile.  Parallel overlaps and bends are
        # not legal crossovers.  Treating those cases as legal can make the
        # mapper lift a long, turning corridor to L3 instead of producing one
        # bounded crossover structure.
        orientations_by_source: Dict[int, Set[str]] = defaultdict(set)
        for _, source, orientation in entries:
            orientations_by_source[int(source)].add(str(orientation))

        orientation_sets = {
            frozenset(orientations)
            for orientations in orientations_by_source.values()
        }
        return orientation_sets == {frozenset({"H"}), frozenset({"V"})}

    def _classify_conflicts(
        self,
        paths: Mapping[Edge, Sequence[Coord]],
        request_by_edge: Mapping[Edge, RouteRequest],
    ) -> List[RoutingConflict]:
        conflicts: List[RoutingConflict] = []

        # Same-source sharing represents one physical fanout tree.  Local
        # occupancy alone cannot distinguish a shared trunk from two branches
        # that split and later rejoin: both contain only one logical source at
        # the overlap.  Require the directed union of every source's paths to
        # be an arborescence.  With monotone paths, a coordinate having two
        # different predecessors is exactly a split-then-rejoin structure and
        # would otherwise be misinterpreted by the cell mapper as a crossover.
        roots_by_source: Dict[int, Dict[Coord, Set[Edge]]] = defaultdict(
            lambda: defaultdict(set)
        )
        incoming_by_source: Dict[
            int, Dict[Coord, Dict[Coord, Set[Edge]]]
        ] = defaultdict(lambda: defaultdict(lambda: defaultdict(set)))
        for edge, path in paths.items():
            if not path:
                continue
            source = int(request_by_edge[edge].src)
            roots_by_source[source][path[0]].add(edge)
            for predecessor, coord in zip(path, path[1:]):
                incoming_by_source[source][coord][predecessor].add(edge)

        for source, roots in sorted(roots_by_source.items()):
            if len(roots) > 1:
                conflicts.append(
                    RoutingConflict(
                        "fanout-root-mismatch",
                        None,
                        tuple(sorted({edge for edges in roots.values() for edge in edges})),
                    )
                )
            for coord, incoming in sorted(incoming_by_source[source].items()):
                if len(incoming) <= 1:
                    continue
                conflicts.append(
                    RoutingConflict(
                        "fanout-reconvergence",
                        coord,
                        tuple(
                            sorted(
                                {
                                    edge
                                    for edges in incoming.values()
                                    for edge in edges
                                }
                            )
                        ),
                    )
                )

        legal_crossings: Dict[Tuple[int, int], List[Tuple[Coord, Tuple[Edge, ...]]]] = (
            defaultdict(list)
        )
        for coord, entries in sorted(self._build_usage(paths, request_by_edge).items()):
            if self._usage_is_legal(entries):
                sources = tuple(sorted({source for _, source, _ in entries}))
                if len(sources) == 2:
                    legal_crossings[sources].append(
                        (coord, tuple(sorted({edge for edge, _, _ in entries})))
                    )
                continue
            edges = tuple(sorted({edge for edge, _, _ in entries}))
            conflicts.append(RoutingConflict("wire-overflow", coord, edges))

        # A pair of monotone source trees never needs to cross twice.  Multiple
        # intersections form a weave that the cell mapper cannot represent as
        # one bounded crossover and can otherwise be mistaken for a lifted
        # corridor after unit-cell stitching.
        for crossings in legal_crossings.values():
            if len(crossings) <= 1:
                continue
            involved_edges = tuple(
                sorted({edge for _, edges in crossings for edge in edges})
            )
            for coord, _ in crossings:
                conflicts.append(
                    RoutingConflict("repeated-crossover", coord, involved_edges)
                )
        return conflicts

    @staticmethod
    def _port_conflicts(requests: Sequence[RouteRequest]) -> List[RoutingConflict]:
        owners: Dict[Tuple[int, Direction], List[Edge]] = defaultdict(list)
        for request in requests:
            owners[(int(request.dst), request.fanin_dir)].append(request.edge)
        conflicts = []
        for (_, _), edges in sorted(owners.items(), key=lambda item: item[0]):
            if len(edges) > 1:
                conflicts.append(
                    RoutingConflict("duplicate-fanin-port", None, tuple(sorted(edges)))
                )
        return conflicts

    def _candidate_cost(
        self,
        request: RouteRequest,
        path: Sequence[Coord],
        usage: Mapping[Coord, Sequence[Tuple[Edge, int, str]]],
        history: Mapping[Coord, float],
    ) -> float:
        cost = float(max(0, len(path) - 1)) + self.bend_cost * self._bend_count(path)
        for index, coord in enumerate(path[1:-1], start=1):
            entries = usage.get(coord, ())
            other_sources = {
                source for _, source, _ in entries if source != int(request.src)
            }
            if other_sources:
                provisional = list(entries) + [
                    (request.edge, int(request.src), self._cell_orientation(path, index))
                ]
                if self._usage_is_legal(provisional):
                    cost += self.crossover_cost * len(other_sources)
                else:
                    cost += self.present_penalty * len(other_sources)
            cost += float(history.get(coord, 0.0))
        return cost

    def route(
        self,
        requests: Sequence[RouteRequest],
        blocked_coords: Iterable[Coord],
        priority_edges: Iterable[Edge] = (),
    ) -> NegotiatedRoutingResult:
        requests = list(requests)
        request_by_edge = {request.edge: request for request in requests}
        priority = {(int(src), int(dst)) for src, dst in priority_edges}
        history: Dict[Coord, float] = defaultdict(float)
        candidates = {
            request.edge: self.generate_candidates(request, blocked_coords)
            for request in requests
        }

        port_conflicts = self._port_conflicts(requests)
        permanently_failed = {
            edge for conflict in port_conflicts for edge in conflict.edges
        }
        permanently_failed.update(
            edge for edge, edge_candidates in candidates.items() if not edge_candidates
        )

        paths: Dict[Edge, List[Coord]] = {}
        active_sources = {
            int(request.src)
            for request in requests
            if request.edge not in permanently_failed
        }
        best_key = None
        best_paths: Dict[Edge, List[Coord]] = {}
        best_wire_conflicts: List[RoutingConflict] = []
        final_conflicts: List[RoutingConflict] = list(port_conflicts)

        for iteration in range(1, self.max_iterations + 1):
            active_edges = {
                request.edge
                for request in requests
                if int(request.src) in active_sources
                and request.edge not in permanently_failed
            }
            for edge in active_edges:
                paths.pop(edge, None)

            source_requests: Dict[int, List[RouteRequest]] = defaultdict(list)
            for request in requests:
                if request.edge in active_edges:
                    source_requests[int(request.src)].append(request)
            source_keys = {}
            for source, branches in source_requests.items():
                source_keys[source] = (
                    0 if any(branch.edge in priority for branch in branches) else 1,
                    min(len(candidates[branch.edge]) for branch in branches),
                    -max(
                        branch.goal[0]
                        - branch.start[0]
                        + branch.goal[1]
                        - branch.start[1]
                        for branch in branches
                    ),
                    # Rotate equal-priority source trees between negotiation
                    # passes so one fixed input order cannot monopolize every
                    # good track forever. This is deterministic for a run.
                    (
                        source * 1103515245
                        + iteration * 2654435761
                    ) & 0xFFFFFFFF,
                )

            ordered_requests = sorted(
                (
                    request
                    for request in requests
                    if request.edge in active_edges
                ),
                key=lambda request: (
                    source_keys[int(request.src)],
                    len(candidates[request.edge]),
                    -(
                        request.goal[0]
                        - request.start[0]
                        + request.goal[1]
                        - request.start[1]
                    ),
                    request.dst,
                ),
            )
            usage = self._build_usage(paths, request_by_edge)

            for request in ordered_requests:
                best_path = min(
                    candidates[request.edge],
                    key=lambda path: (
                        self._candidate_cost(
                            request,
                            path,
                            usage,
                            history,
                        ),
                        tuple(path),
                    ),
                )
                paths[request.edge] = list(best_path)
                for index in range(1, len(best_path) - 1):
                    usage[best_path[index]].append(
                        (
                            request.edge,
                            int(request.src),
                            self._cell_orientation(best_path, index),
                        )
                    )

            wire_conflicts = self._classify_conflicts(paths, request_by_edge)
            final_conflicts = list(port_conflicts) + wire_conflicts
            if not wire_conflicts and not permanently_failed:
                return NegotiatedRoutingResult(
                    paths=paths,
                    failed_edges=set(),
                    conflicts=[],
                    iterations=iteration,
                    overflow_history=dict(history),
                )

            conflict_edges = {
                edge for conflict in wire_conflicts for edge in conflict.edges
            }
            state_key = (len(conflict_edges), len(wire_conflicts))
            if best_key is None or state_key < best_key:
                best_key = state_key
                best_paths = {edge: list(path) for edge, path in paths.items()}
                best_wire_conflicts = list(wire_conflicts)
            active_sources = {
                int(request_by_edge[edge].src) for edge in conflict_edges
            }
            for conflict in wire_conflicts:
                if conflict.coord is not None:
                    history[conflict.coord] += self.history_increment

            # A repeated conflict count does not mean the same resources are
            # involved: historical prices can still push the trees onto new
            # tracks. Let the explicit iteration budget decide convergence.
            if not active_sources:
                break

        if best_key is not None:
            paths = best_paths
            final_conflicts = list(port_conflicts) + best_wire_conflicts
        conflict_edges = {
            edge for conflict in final_conflicts for edge in conflict.edges
        }
        failed_edges = set(permanently_failed) | conflict_edges
        failed_sources = {
            int(request_by_edge[edge].src)
            for edge in failed_edges
            if edge in request_by_edge
        }
        # A fanout is one physical source tree. If one branch remains illegal,
        # do not export sibling branches as if they were an independent tree.
        failed_edges.update(
            request.edge for request in requests if int(request.src) in failed_sources
        )
        safe_paths = {
            edge: path for edge, path in paths.items() if edge not in failed_edges
        }
        return NegotiatedRoutingResult(
            paths=safe_paths,
            failed_edges=failed_edges,
            conflicts=final_conflicts,
            iterations=min(self.max_iterations, iteration if requests else 0),
            overflow_history=dict(history),
        )
