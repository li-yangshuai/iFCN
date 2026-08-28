import os
import sys
import unittest


GCN_RL_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ALGORITHM_ROOT = os.path.join(GCN_RL_ROOT, "src", "algorithm")
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from lib import iFCN_Lab  # noqa: E402


class RightDownCongestionRoutingTest(unittest.TestCase):
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
