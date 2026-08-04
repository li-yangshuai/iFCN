from pathlib import Path
import sys


ALGORITHM_ROOT = Path(__file__).resolve().parents[1] / "src" / "algorithm"
if str(ALGORITHM_ROOT) not in sys.path:
    sys.path.insert(0, str(ALGORITHM_ROOT))

from lib import iFCN_Lab
from src.normalGraphDraw import NormalGraphDraw


def _router(src_coord=(0, 0), dst_coord=(3, 3)):
    board = iFCN_Lab.MapChessboard()
    board.placeNode(0, src_coord, iFCN_Lab.NodeType.And)
    board.placeNode(1, dst_coord, iFCN_Lab.NodeType.Or)
    return board, iFCN_Lab.RightDownAStar(board)


def test_route_obeys_both_endpoint_ports():
    _, router = _router()
    path = router.route_with_dirs(0, 1, (1, 0), (0, -1))

    assert path
    assert (path[1][0] - path[0][0], path[1][1] - path[0][1]) == (1, 0)
    assert (path[-2][0] - path[-1][0], path[-2][1] - path[-1][1]) == (0, -1)


def test_adjacent_route_cannot_bypass_incompatible_sink_port():
    _, valid_router = _router(dst_coord=(1, 0))
    valid = valid_router.route_with_dirs(0, 1, (1, 0), (-1, 0))
    _, invalid_router = _router(dst_coord=(1, 0))
    invalid = invalid_router.route_with_dirs(0, 1, (1, 0), (0, -1))

    assert valid == [(0, 0), (1, 0)]
    assert invalid == []


def test_rejects_non_monotone_endpoint_ports():
    _, bad_source_router = _router()
    assert bad_source_router.route_with_dirs(0, 1, (-1, 0), (0, -1)) == []
    _, bad_sink_router = _router()
    assert bad_sink_router.route_with_dirs(0, 1, (0, 1), (1, 0)) == []


def test_port_validator_rejects_two_fanins_on_same_gate_side():
    board = iFCN_Lab.MapChessboard()
    board.savePath((0, 2), [(0, 0), (0, 1), (1, 1), (1, 2)])
    board.savePath((1, 2), [(2, 0), (2, 1), (1, 1), (1, 2)])

    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    draw.mapChessboard = board
    draw.fanout_directions = {0: (0, 1), 1: (0, 1)}
    draw.fanin_directions = {(0, 2): (0, -1), (1, 2): (0, -1)}
    draw.last_port_direction_violations = []

    ports_ok, violations, bad_edges = draw.validate_gate_port_directions()

    assert not ports_ok
    assert bad_edges == {(0, 2), (1, 2)}
    assert any("multiple fanins share input port" in message for _, message in violations)


def test_fallback_cannot_take_another_fanins_reserved_port():
    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    attempted = []

    def fake_route(src, dst, fanout_direction, fanin_direction):
        attempted.append(fanin_direction)
        if fanin_direction == (0, -1):
            return [(0, 0), (0, 1)]
        return []

    draw._route_edge = fake_route
    path, selected = draw._route_edge_with_direction_options(
        0,
        2,
        (1, 0),
        (-1, 0),
        forbidden_directions={(0, -1)},
    )

    assert path == []
    assert selected == (-1, 0)
    assert attempted == [(-1, 0)]
