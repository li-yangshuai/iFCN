from pathlib import Path
from types import SimpleNamespace
import sys

ALGORITHM_ROOT = Path(__file__).resolve().parents[1] / "src" / "algorithm"
if str(ALGORITHM_ROOT) not in sys.path:
    sys.path.insert(0, str(ALGORITHM_ROOT))

from lib import iFCN_Lab
from src.normalGraphDraw import NormalGraphDraw


def _snapshot_draw():
    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    draw.parse = SimpleNamespace(fileName="snapshot_test.v")
    draw.mapChessboard = iFCN_Lab.MapChessboard()
    draw._stage_snapshot_counter = 0
    draw._stage_snapshot_last_tex = None
    draw._stage_initial_route_recorded = False
    draw._stage_conflict_repair_recorded = False
    return draw


def test_stage_tex_starts_at_initial_layout_and_skips_adjacent_duplicates(tmp_path):
    draw = _snapshot_draw()
    draw.mapChessboard.placeNode(0, (0, 0), iFCN_Lab.NodeType.And)

    assert draw._snapshot_stage_tex(str(tmp_path), "stage1_initial")
    assert not draw._snapshot_stage_tex(str(tmp_path), "duplicate_event")

    draw.mapChessboard.placeNode(1, (1, 1), iFCN_Lab.NodeType.Or)
    assert draw._snapshot_stage_tex(str(tmp_path), "layout_changed")

    assert sorted(path.name for path in tmp_path.glob("*.tex")) == [
        "snapshot_test_00_stage1_initial.tex",
        "snapshot_test_01_layout_changed.tex",
    ]


def test_initial_routing_checkpoint_is_recorded_only_once():
    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    recorded = []

    def record(stage_name, failed_pairs=None):
        recorded.append((stage_name, dict(failed_pairs or {})))
        return True

    draw._snapshot_routing_change = record
    failed_pairs = {(0, 1): (0, -1)}

    assert draw._snapshot_initial_routing(failed_pairs)
    assert not draw._snapshot_initial_routing({})
    assert recorded == [
        ("stage3_initial_routing_failed_1", failed_pairs),
    ]


def test_conflict_repair_checkpoint_is_recorded_only_once():
    draw = NormalGraphDraw.__new__(NormalGraphDraw)
    recorded = []
    placements = []

    def record(stage_name, failed_pairs=None):
        recorded.append((stage_name, dict(failed_pairs or {})))
        return True

    draw.place_all_nodes_on_chessboard = lambda: placements.append(True)
    draw._snapshot_routing_change = record
    failed_pairs = {(2, 3): (-1, 0), (4, 5): (0, -1)}

    assert draw._snapshot_conflict_repair_placement(failed_pairs)
    assert not draw._snapshot_conflict_repair_placement({})
    assert placements == [True]
    assert recorded == [
        (
            "stage4_conflict_repair_expanded_placement_failed_2",
            failed_pairs,
        ),
    ]
