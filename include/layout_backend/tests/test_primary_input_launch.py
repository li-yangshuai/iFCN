"""Whole-cycle launch alignment on the immutable 2DDWave template."""
from pathlib import Path
from types import SimpleNamespace
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/algorithm"))
from lib import iFCN_Lab
from src.normalGraphDraw import NormalGraphDraw


def make_draw(points, inputs):
    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    draw.node_ids = np.asarray(list(points), dtype=np.int32)
    draw._node_id_to_idx = {node: index for index, node in enumerate(points)}
    draw.coords = np.asarray(list(points.values()), dtype=np.int32)
    draw.parse = SimpleNamespace(getInputNodesIndex=set(inputs))
    draw._coord_cache_dirty = True
    draw.mapChessboard = iFCN_Lab.MapChessboard()
    return draw


def test_equal_modulo_phases_do_not_hide_a_delayed_primary_input():
    draw = make_draw({0: (0, 0), 1: (0, 4), 2: (2, 5)}, [0, 1])
    draw.mapChessboard.savePath((0, 2),
        [(0, 0), (1, 0), (2, 0), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5)])
    draw.mapChessboard.savePath((1, 2), [(0, 4), (0, 5), (1, 5), (2, 5)])
    assert draw.solve_clock_template_consistency()[1] == []
    assert not draw.verify_clock_template_consistency(verbose=False)[0]


def test_relocation_preserves_monotone_reachability_and_logic_geometry():
    points = {0: (0, 0), 1: (0, 4), 2: (2, 8), 3: (4, 10), 4: (6, 12)}
    draw = make_draw(points, [0, 1, 2])
    assert draw.align_primary_input_launches()
    assert draw._primary_launch_conflicts() == []
    assert len(set(draw._node_coord.values())) == len(points)
    assert all(min(point) >= 0 for point in draw._node_coord.values())
    for node in [0, 1, 2]:
        assert all(a <= b for a, b in zip(draw._node_coord[node], draw._node_coord[3]))
    assert np.array_equal(draw.coords[4] - draw.coords[3], np.asarray(points[4]) - points[3])
    assert not draw.align_primary_input_launches()


def test_already_valid_input_launches_are_unchanged():
    draw = make_draw({0: (0, 0), 1: (0, 1), 2: (2, 2)}, [0, 1])
    before = draw.coords.copy()
    assert not draw.align_primary_input_launches()
    assert np.array_equal(draw.coords, before)


def make_repair_draw():
    draw = make_draw({0: (0, 0), 1: (0, 3), 2: (2, 6)}, [0, 1])
    draw.edges = [(0, 2), (1, 2)]
    draw.route_expansion_history = []
    draw.structural_tuning = {"route_insert_max_ops": 4}
    draw._last_route_explicit_priority = ()
    draw._all_route_pairs = lambda: draw.edges
    draw._materialize_priority_route_order = lambda *args, **kwargs: draw.edges
    draw._refresh_fanin_directions_for_current_coords = lambda: None
    draw.targeted_ripup_reroute = lambda failed, **kwargs: failed
    return draw


def test_expansion_realigns_inputs_before_rerouting():
    draw = make_repair_draw()
    draw.place_all_nodes_on_chessboard = lambda: None
    draw.sequence_route_all_edges = lambda **kwargs: {(0, 2): (0, -1)}
    draw._snapshot_initial_routing = lambda failed: None
    draw.separate_failed_vertical_channels = lambda *args, **kwargs: 0
    # This cut moves PI 1 from cycle zero to cycle one.
    draw.failed_pairs_to_insert_ops = lambda *args, **kwargs: ([1], [])
    routed = []

    def reroute(*args, **kwargs):
        assert not draw._primary_launch_conflicts()
        assert not draw._right_down_invariant_violations()
        routed.append(draw.coords.copy())
        return {}

    draw.reroute_with_priority_pairs = reroute
    assert draw.route_until_success(max_expansion_rounds=1, timeout_sec=1) == {}
    assert len(routed) == 1
    assert draw.route_expansion_history[0]["inserted_rows"] == [1]


def test_initial_relocation_refreshes_ports_before_first_route():
    draw = make_repair_draw()
    draw.coords[1] = (0, 4)
    draw._coord_cache_dirty = True
    refreshed = []
    draw._refresh_fanin_directions_for_current_coords = lambda: refreshed.append(draw.coords.copy())

    def place_nodes():
        assert len(refreshed) == 1
        assert not draw._primary_launch_conflicts()

    draw.place_all_nodes_on_chessboard = place_nodes
    draw.sequence_route_all_edges = lambda **kwargs: {}
    draw._snapshot_initial_routing = lambda failed: None
    assert draw.route_until_success(max_expansion_rounds=0, timeout_sec=1) == {}
    assert not draw._right_down_invariant_violations()


def test_targeted_corridor_realigns_inputs_before_rerouting():
    draw = make_repair_draw()
    routed = []

    def reroute(*args, **kwargs):
        assert not draw._primary_launch_conflicts()
        assert not draw._right_down_invariant_violations()
        routed.append(draw.coords.copy())
        return {}

    draw.reroute_with_priority_pairs = reroute
    assert draw.targeted_corridor_expansion(
        {(0, 2): (0, -1)}, draw.edges, max_candidates=1, timeout_sec=1
    ) == {}
    assert len(routed) == 1


def test_compaction_rejects_delayed_input_and_restores_legal_routes():
    draw = make_repair_draw()
    draw.fanin_directions = {}
    draw._last_route_priority = set()
    draw._last_route_reverse_priority = False
    draw._last_route_reverse_remaining = False
    draw._current_phase_contraction_metrics = lambda: {"area": 21, "used_cell_count": 8}
    verified_cycles = []

    def reroute(verbose=False):
        draw.mapChessboard = iFCN_Lab.MapChessboard()
        for src, dst in draw.edges:
            x, y = draw.get_node_coord(src)
            end_x, end_y = draw.get_node_coord(dst)
            path = [(x, step) for step in range(y, end_y + 1)]
            path.extend((step, end_y) for step in range(x + 1, end_x + 1))
            draw.mapChessboard.savePath((src, dst), path)
        # Local modulo-four routing remains valid for both geometries.
        assert draw.solve_clock_template_consistency()[1] == []
        verified_cycles.append(sum(draw.get_node_coord(1)) // 4)
        return draw.verify_clock_template_consistency(verbose=False)[0]

    draw._reroute_and_validate_current_coords = reroute
    before = draw.coords.copy()
    assert reroute()
    old_route = list(draw.mapChessboard.nodePairRoutes[(1, 2)])
    accepted, _, _ = draw._try_phase_node_move({"node": 1, "to_coord": (0, 4)})
    assert not accepted
    assert np.array_equal(draw.coords, before)
    assert list(draw.mapChessboard.nodePairRoutes[(1, 2)]) == old_route
    assert verified_cycles == [0, 1, 0]
    assert draw.clock_template_ok
