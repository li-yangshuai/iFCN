import os
import sys
import unittest
from unittest import mock


GCN_RL_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ALGORITHM_ROOT = os.path.join(GCN_RL_ROOT, "src", "algorithm")
MAIN_ROOT = os.path.join(ALGORITHM_ROOT, "main")
for path in (MAIN_ROOT, ALGORITHM_ROOT):
    if path not in sys.path:
        sys.path.insert(0, path)

from test_randomPhase import (  # noqa: E402
    contract_random_layout_after_cut,
    insert_random_layout_rows_and_cols,
    random_phase_layout_is_legal,
)
from src.normalGraphDraw import NormalGraphDraw  # noqa: E402


class LayoutContractionTest(unittest.TestCase):
    @staticmethod
    def make_phase_move_draw(shared_first_wire=False):
        class FakeBoard:
            def __init__(self):
                self.nodePairRoutes = {
                    (1, 2): [(0, 0), (0, 1), (0, 2), (0, 3)],
                }
                if shared_first_wire:
                    self.nodePairRoutes[(1, 3)] = [
                        (0, 0),
                        (0, 1),
                        (1, 1),
                        (1, 2),
                        (1, 3),
                    ]

            def getPhase(self, coord):
                return (coord[0] + coord[1]) % 4

        class FakeParse:
            def get_fanins(self, node):
                return {1: [], 2: [1], 3: [1]}.get(node, [])

            def get_fanouts(self, node):
                fanouts = [2, 3] if shared_first_wire else [2]
                return {1: fanouts, 2: [], 3: []}.get(node, [])

        draw = NormalGraphDraw.__new__(NormalGraphDraw)
        draw.mapChessboard = FakeBoard()
        draw.parse = FakeParse()
        draw._node_coord = {1: (0, 0), 2: (0, 3)}
        if shared_first_wire:
            draw._node_coord[3] = (1, 3)
        draw._coord_set = set(draw._node_coord.values())
        draw._node_id_to_idx = {
            node_id: index for index, node_id in enumerate(draw._node_coord)
        }
        draw._coord_cache_dirty = False
        return draw

    def test_top_down_node_absorbs_its_single_outgoing_wire_cell(self):
        draw = self.make_phase_move_draw()
        candidates = draw._phase_node_move_candidates("top_down", center_twice=3)
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0]["node"], 1)
        self.assertEqual(candidates[0]["from_coord"], (0, 0))
        self.assertEqual(candidates[0]["to_coord"], (0, 1))
        self.assertEqual(candidates[0]["route_edge"], (1, 2))

    def test_bottom_up_node_absorbs_its_single_incoming_wire_cell(self):
        draw = self.make_phase_move_draw()
        candidates = draw._phase_node_move_candidates("bottom_up", center_twice=3)
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0]["node"], 2)
        self.assertEqual(candidates[0]["from_coord"], (0, 3))
        self.assertEqual(candidates[0]["to_coord"], (0, 2))
        self.assertEqual(candidates[0]["route_edge"], (1, 2))

    def test_shared_wire_cell_is_not_a_node_move_candidate(self):
        draw = self.make_phase_move_draw(shared_first_wire=True)
        self.assertEqual(
            draw._phase_node_move_candidates("top_down", center_twice=3),
            [],
        )

    def test_recursive_windows_are_scheduled_global_to_internal(self):
        draw = NormalGraphDraw.__new__(NormalGraphDraw)
        windows = draw._recursive_phase_contraction_windows(0, 9)
        self.assertEqual(
            [(w["min_y"], w["max_y"], w["depth"]) for w in windows],
            [(0, 9, 0), (0, 4, 1), (5, 9, 1), (0, 2, 2), (5, 7, 2)],
        )

    def test_recursive_candidate_respects_window_and_direction_lock(self):
        draw = self.make_phase_move_draw()
        unlocked = draw._phase_node_move_candidates(
            "bottom_up",
            center_twice=4,
            y_bounds=(1, 3),
            recursive_depth=1,
        )
        self.assertEqual([item["node"] for item in unlocked], [2])
        self.assertEqual(unlocked[0]["recursive_depth"], 1)
        self.assertEqual(unlocked[0]["y_window"], [1, 3])
        locked = draw._phase_node_move_candidates(
            "bottom_up",
            center_twice=4,
            y_bounds=(1, 3),
            direction_locks={2: "top_down"},
            recursive_depth=1,
        )
        self.assertEqual(locked, [])

    def test_compaction_records_top_down_phase_node_move(self):
        class FakeBoard:
            def findLayoutBoard(self):
                return 0, 0, 4, 4

        class FakeCoords:
            def clone(self):
                return FakeCoords()

        draw = NormalGraphDraw.__new__(NormalGraphDraw)
        draw.mapChessboard = FakeBoard()
        draw.coords = FakeCoords()
        candidate = {
            "node": 7,
            "from_coord": (2, 0),
            "to_coord": (2, 1),
            "route_edge": (7, 8),
            "phase_from": 2,
            "phase_target": 3,
            "recursive_depth": 0,
            "y_window": [0, 4],
        }
        draw._phase_node_move_candidates = lambda sweep, center, **kwargs: (
            [candidate]
            if sweep == "top_down" and kwargs.get("recursive_depth") == 0
            else []
        )

        old_metrics = {
            "width": 5,
            "height": 5,
            "area": 25,
            "used_cell_count": 12,
            "routed_wire_cells": 8,
        }
        new_metrics = {
            "width": 5,
            "height": 4,
            "area": 20,
            "used_cell_count": 11,
            "routed_wire_cells": 7,
            "phase_after": 3,
        }
        draw._try_phase_node_move = lambda item, verbose=False: (
            True,
            old_metrics,
            new_metrics,
        )
        draw._empty_line_candidates = lambda: []

        draw._reroute_and_validate_current_coords = lambda verbose=False: True
        with mock.patch.dict(
            os.environ,
            {
                "IFCN_CONTRACTION_EVALUATIONS": "4",
                "IFCN_CONTRACTION_TIMEOUT": "30",
            },
        ):
            reductions = draw.compact_layout(max_iters=1)

        self.assertEqual(reductions, 1)
        self.assertEqual(
            draw.contraction_history[0]["mode"],
            "phase_node_global_top_down",
        )
        self.assertEqual(draw.contraction_history[0]["node"], 7)
        self.assertEqual(draw.contraction_history[0]["to_coord"], [2, 1])
        self.assertEqual(draw.contraction_history[0]["area"], 20)

    def test_empty_line_candidates_find_only_node_empty_lines(self):
        class FakeBoard:
            def findLayoutBoard(self):
                return 0, 0, 4, 4

        draw = NormalGraphDraw.__new__(NormalGraphDraw)
        draw.mapChessboard = FakeBoard()
        draw._node_coord = {
            index: coord
            for index, coord in enumerate(
                (
                    (0, 0),
                    (4, 0),
                    (0, 1),
                    (4, 1),
                    (0, 3),
                    (4, 3),
                    (0, 4),
                    (4, 4),
                )
            )
        }
        draw._coord_cache_dirty = False
        self.assertEqual(
            draw._empty_line_candidates(),
            [("y", 2), ("x", 1), ("x", 2), ("x", 3)],
        )

    def test_compaction_records_empty_row_deletion(self):
        class FakeBoard:
            def findLayoutBoard(self):
                return 0, 0, 4, 4

        class FakeCoords:
            def clone(self):
                return FakeCoords()

        draw = NormalGraphDraw.__new__(NormalGraphDraw)
        draw.mapChessboard = FakeBoard()
        draw.coords = FakeCoords()
        draw._phase_node_move_candidates = lambda *args, **kwargs: []
        deleted = []
        draw._empty_line_candidates = lambda: [] if deleted else [("y", 2)]
        old_metrics = {
            "width": 5,
            "height": 5,
            "area": 25,
            "used_cell_count": 10,
            "routed_wire_cells": 6,
        }
        new_metrics = {
            "width": 5,
            "height": 4,
            "area": 20,
            "used_cell_count": 10,
            "routed_wire_cells": 6,
        }

        def delete_line(axis, line, verbose=False):
            deleted.append((axis, line))
            return True, old_metrics, new_metrics

        draw._try_delete_empty_line = delete_line
        draw._reroute_and_validate_current_coords = lambda verbose=False: True
        reductions = draw.compact_layout(max_iters=1)
        self.assertEqual(reductions, 1)
        self.assertEqual(deleted, [("y", 2)])
        self.assertEqual(draw.contraction_history[0]["mode"], "empty_row_delete")
        self.assertEqual(draw.contraction_history[0]["area"], 20)

    def test_insert_rows_and_columns_preserves_unique_node_coordinates(self):
        positions = {1: (0, 0), 2: (1, 1), 3: (2, 2)}
        expanded = insert_random_layout_rows_and_cols(
            positions,
            row_ops=[1],
            col_ops=[1, 2],
        )
        self.assertEqual(expanded, {1: (0, 0), 2: (2, 2), 3: (4, 3)})
        self.assertEqual(len(set(expanded.values())), len(expanded))

    def test_outer_cut_rejects_node_overlap(self):
        positions = {1: (0, 0), 2: (1, 0)}
        self.assertIsNone(contract_random_layout_after_cut(positions, "x", 0))

    def test_outer_cut_moves_only_coordinates_beyond_cut(self):
        positions = {1: (0, 0), 2: (2, 1), 3: (4, 2)}
        contracted = contract_random_layout_after_cut(positions, "x", 2)
        self.assertEqual(contracted, {1: (0, 0), 2: (2, 1), 3: (3, 2)})

    def test_legal_result_requires_zero_failures_and_direction_conflicts(self):
        self.assertTrue(
            random_phase_layout_is_legal(
                {"failed_edges": [], "direction_violation_count": 0}
            )
        )
        self.assertFalse(
            random_phase_layout_is_legal(
                {"failed_edges": [(1, 2)], "direction_violation_count": 0}
            )
        )
        self.assertFalse(
            random_phase_layout_is_legal(
                {"failed_edges": [], "direction_violation_count": 1}
            )
        )


if __name__ == "__main__":
    unittest.main()
