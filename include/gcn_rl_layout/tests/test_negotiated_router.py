from pathlib import Path
import sys


ALGORITHM_ROOT = Path(__file__).resolve().parents[1] / "src" / "algorithm"
if str(ALGORITHM_ROOT) not in sys.path:
    sys.path.insert(0, str(ALGORITHM_ROOT))

from src.negotiated_router import MonotoneNegotiatedRouter, RouteRequest


def _assert_endpoint_dirs(request, path):
    assert path[0] == request.start
    assert path[-1] == request.goal
    assert (path[1][0] - path[0][0], path[1][1] - path[0][1]) == request.fanout_dir
    assert (path[-2][0] - path[-1][0], path[-2][1] - path[-1][1]) == request.fanin_dir


def test_candidates_are_monotone_and_obey_ports():
    request = RouteRequest(0, 1, (0, 0), (5, 5), (1, 0), (0, -1))
    router = MonotoneNegotiatedRouter(max_tracks=6)
    candidates = router.generate_candidates(request, {request.start, request.goal})

    assert candidates
    for path in candidates:
        _assert_endpoint_dirs(request, path)
        assert all(
            (bx - ax, by - ay) in {(1, 0), (0, 1)}
            for (ax, ay), (bx, by) in zip(path, path[1:])
        )


def test_duplicate_fanin_port_is_a_hard_failure():
    requests = [
        RouteRequest(0, 2, (0, 0), (3, 3), (1, 0), (0, -1)),
        RouteRequest(1, 2, (1, 0), (3, 3), (0, 1), (0, -1)),
    ]
    blocked = {request.start for request in requests} | {requests[0].goal}
    result = MonotoneNegotiatedRouter().route(requests, blocked)

    assert result.failed_edges == {(0, 2), (1, 2)}
    assert any(conflict.kind == "duplicate-fanin-port" for conflict in result.conflicts)


def test_unavoidable_crossing_requires_explicit_crossover_support():
    horizontal = RouteRequest(0, 1, (0, 2), (4, 2), (1, 0), (-1, 0))
    vertical = RouteRequest(2, 3, (2, 0), (2, 4), (0, 1), (0, -1))
    requests = [horizontal, vertical]
    blocked = {request.start for request in requests} | {request.goal for request in requests}

    planar = MonotoneNegotiatedRouter(allow_crossovers=False).route(requests, blocked)
    assert planar.failed_edges == {(0, 1), (2, 3)}

    crossed = MonotoneNegotiatedRouter(allow_crossovers=True).route(requests, blocked)
    assert crossed.failed_edges == set()
    assert set(crossed.paths) == {(0, 1), (2, 3)}


def test_same_source_overlap_is_one_legal_fanout_tree():
    requests = [
        RouteRequest(0, 1, (0, 0), (4, 3), (1, 0), (0, -1)),
        RouteRequest(0, 2, (0, 0), (5, 4), (1, 0), (-1, 0)),
    ]
    blocked = {request.start for request in requests} | {request.goal for request in requests}
    result = MonotoneNegotiatedRouter().route(requests, blocked)

    assert result.failed_edges == set()
    assert set(result.paths) == {(0, 1), (0, 2)}
    for request in requests:
        _assert_endpoint_dirs(request, result.paths[request.edge])

    first_path = result.paths[(0, 1)]
    second_path = result.paths[(0, 2)]
    common_prefix = []
    for first_coord, second_coord in zip(first_path, second_path):
        if first_coord != second_coord:
            break
        common_prefix.append(first_coord)
    assert len(common_prefix) > 1
    assert set(first_path) & set(second_path) == set(common_prefix)


def test_parallel_segments_cannot_masquerade_as_a_crossover():
    for orientation in ("H", "V"):
        entries = [
            ((0, 1), 0, orientation),
            ((2, 3), 2, orientation),
        ]
        assert not MonotoneNegotiatedRouter(
            allow_crossovers=True
        )._usage_is_legal(entries)
        assert not MonotoneNegotiatedRouter(
            allow_crossovers=False
        )._usage_is_legal(entries)


def test_turning_segment_cannot_use_the_crossover_layer():
    for other_orientation in ("H", "V", "B"):
        entries = [((0, 1), 0, "B"), ((2, 3), 2, other_orientation)]
        assert not MonotoneNegotiatedRouter(
            allow_crossovers=True
        )._usage_is_legal(entries)


def test_same_source_fanout_may_share_a_turning_trunk():
    entries = [((0, 1), 0, "H"), ((0, 2), 0, "B")]
    assert MonotoneNegotiatedRouter(allow_crossovers=False)._usage_is_legal(entries)


def test_same_source_fanout_cannot_split_then_rejoin():
    requests = {
        (0, 1): RouteRequest(0, 1, (0, 0), (2, 3), (1, 0), (0, -1)),
        (0, 2): RouteRequest(0, 2, (0, 0), (3, 2), (1, 0), (-1, 0)),
    }
    paths = {
        (0, 1): [(0, 0), (1, 0), (2, 0), (2, 1), (2, 2), (2, 3)],
        (0, 2): [(0, 0), (1, 0), (1, 1), (1, 2), (2, 2), (3, 2)],
    }

    conflicts = MonotoneNegotiatedRouter()._classify_conflicts(paths, requests)

    assert len(conflicts) == 1
    assert conflicts[0].kind == "fanout-reconvergence"
    assert conflicts[0].coord == (2, 2)
    assert conflicts[0].edges == ((0, 1), (0, 2))


def test_two_source_trees_cannot_weave_through_two_crossovers():
    requests = {
        (0, 1): RouteRequest(0, 1, (0, 1), (2, 4), (1, 0), (0, -1)),
        (2, 3): RouteRequest(2, 3, (1, 0), (3, 3), (0, 1), (-1, 0)),
    }
    paths = {
        (0, 1): [(0, 1), (1, 1), (2, 1), (2, 2), (2, 3), (2, 4)],
        (2, 3): [(1, 0), (1, 1), (1, 2), (1, 3), (2, 3), (3, 3)],
    }

    conflicts = MonotoneNegotiatedRouter(
        allow_crossovers=True
    )._classify_conflicts(paths, requests)

    assert len(conflicts) == 2
    assert {conflict.kind for conflict in conflicts} == {"repeated-crossover"}
    assert {conflict.coord for conflict in conflicts} == {(1, 1), (2, 3)}


def test_crossover_layer_never_accepts_three_source_trees():
    entries = [
        ((0, 1), 0, "H"),
        ((2, 3), 2, "V"),
        ((4, 5), 4, "B"),
    ]
    assert not MonotoneNegotiatedRouter(allow_crossovers=True)._usage_is_legal(entries)


if __name__ == "__main__":
    test_candidates_are_monotone_and_obey_ports()
    test_duplicate_fanin_port_is_a_hard_failure()
    test_unavoidable_crossing_requires_explicit_crossover_support()
    test_same_source_overlap_is_one_legal_fanout_tree()
    test_parallel_segments_cannot_masquerade_as_a_crossover()
    test_turning_segment_cannot_use_the_crossover_layer()
    test_same_source_fanout_may_share_a_turning_trunk()
    test_same_source_fanout_cannot_split_then_rejoin()
    test_two_source_trees_cannot_weave_through_two_crossovers()
    test_crossover_layer_never_accepts_three_source_trees()
