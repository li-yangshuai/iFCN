import os
import sys
import unittest


LAYOUT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ALGORITHM_ROOT = os.path.join(LAYOUT_ROOT, "src", "algorithm")
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from lib import iFCN_Lab  # noqa: E402


class RightDownCongestionRoutingTest(unittest.TestCase):
    def test_different_sources_cannot_share_a_bending_gate_port(self):
        for use_new_router in (False, True):
            with self.subTest(use_new_router=use_new_router):
                board = iFCN_Lab.MapChessboard()
                for node, coord in [(0, (0, 2)), (1, (3, 3)),
                                    (2, (0, 0)), (3, (4, 2))]:
                    board.placeNode(node, coord, iFCN_Lab.NodeType.Input
                                    if node in (0, 2) else iFCN_Lab.NodeType.Output)
                router = iFCN_Lab.RightDownAStar(board)
                first = [tuple(p) for p in router.route_with_dirs(0, 1, (1, 0), (0, -1))]
                self.assertEqual(first, [(0, 2), (1, 2), (2, 2), (3, 2), (3, 3)])
                # Both routes have (3,2) as their selected sink-access tile.
                # Its existing horizontal-to-vertical bend cannot host a
                # different source, even though coarse capacity remains.
                if use_new_router:
                    board.savePath((0, 1), first)
                    router = iFCN_Lab.RightDownAStar(board)
                self.assertEqual(list(router.route_with_dirs(2, 3, (0, 1), (-1, 0))), [])

    def test_straight_perpendicular_crossing_remains_legal(self):
        board = iFCN_Lab.MapChessboard()
        for node, coord in [(0, (3, 0)), (1, (3, 4)),
                            (2, (0, 2)), (3, (5, 2))]:
            board.placeNode(node, coord, iFCN_Lab.NodeType.Input
                            if node in (0, 2) else iFCN_Lab.NodeType.Output)
        router = iFCN_Lab.RightDownAStar(board)
        vertical = [tuple(p) for p in router.route_with_dirs(0, 1, (0, 1), (0, -1))]
        horizontal = [tuple(p) for p in router.route_with_dirs(2, 3, (1, 0), (-1, 0))]
        self.assertEqual(vertical, [(3, y) for y in range(5)])
        self.assertEqual(horizontal, [(x, 2) for x in range(6)])
        self.assertEqual(set(vertical) & set(horizontal), {(3, 2)})

    def test_same_source_can_reuse_its_launch_prefix(self):
        board = iFCN_Lab.MapChessboard()
        for node, coord in [(0, (0, 0)), (1, (4, 3)), (2, (5, 4))]:
            board.placeNode(node, coord, iFCN_Lab.NodeType.Input
                            if node == 0 else iFCN_Lab.NodeType.Output)
        router = iFCN_Lab.RightDownAStar(board)
        first = [tuple(p) for p in router.route_with_dirs(0, 1, (0, 1), (-1, 0))]
        second = [tuple(p) for p in router.route_with_dirs(0, 2, (0, 1), (-1, 0))]
        self.assertTrue(first)
        self.assertTrue(second)
        self.assertEqual(first[:2], second[:2])
        self.assertEqual(second[-1], (5, 4))

    def test_route_avoids_occupied_cells_when_empty_monotone_path_exists(self):
        board = iFCN_Lab.MapChessboard()
        board.placeNode(0, (0, 0), iFCN_Lab.NodeType.Input)
        board.placeNode(1, (4, 4), iFCN_Lab.NodeType.Output)

        # One wire still leaves enough capacity for a second wire.  The old
        # direct-L-first router reused these cells and formed capacity walls;
        # the congestion-aware DP should take the equally short empty path.
        board.placeWire((0, 2))
        board.placeWire((0, 3))

        router = iFCN_Lab.RightDownAStar(board)
        path = [tuple(coord) for coord in router.route(0, 1, (0, -1))]

        self.assertTrue(path)
        self.assertEqual(path[0], (0, 0))
        self.assertEqual(path[-1], (4, 4))
        self.assertNotIn((0, 2), path)
        self.assertNotIn((0, 3), path)


if __name__ == "__main__":
    unittest.main()
