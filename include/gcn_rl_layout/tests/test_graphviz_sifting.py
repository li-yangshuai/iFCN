import os
import shutil
import sys
import unittest
from collections import Counter

import numpy as np


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ALGORITHM_ROOT = os.path.join(REPO_ROOT, "src", "algorithm")
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.graphviz_sifting import (  # noqa: E402
    adjacent_layer_edges,
    deterministic_layout_embeddings,
    exact_gain_sifting,
    graphviz_fixed_layer_order,
)


def count_crossings(upper, lower, edges):
    upper_rank = {node: index for index, node in enumerate(upper)}
    lower_rank = {node: index for index, node in enumerate(lower)}
    boundary = [
        (source, target)
        for source, target in edges
        if source in upper_rank and target in lower_rank
    ]
    total = 0
    for first_index, first in enumerate(boundary):
        for second in boundary[first_index + 1 :]:
            if first[0] == second[0] or first[1] == second[1]:
                continue
            if (
                (upper_rank[first[0]] - upper_rank[second[0]])
                * (lower_rank[first[1]] - lower_rank[second[1]])
                < 0
            ):
                total += 1
    return total


class GraphvizSiftingTest(unittest.TestCase):
    def test_exact_gain_sifting_is_monotone_and_preserves_layers(self):
        layers = [[0, 1], [2, 3]]
        edges = [(0, 3), (1, 2)]
        refined, diagnostics = exact_gain_sifting(
            layers,
            edges,
            count_crossings,
            max_passes=5,
            time_limit=5,
            evaluation_budget=100,
        )
        self.assertEqual(diagnostics["initial_crossings"], 1)
        self.assertEqual(diagnostics["final_crossings"], 0)
        self.assertEqual(diagnostics["crossings_removed"], 1)
        for expected, actual in zip(layers, refined):
            self.assertEqual(Counter(expected), Counter(actual))

    def test_adjacent_edge_filter_excludes_long_edges(self):
        layers = [[0], [1], [2]]
        self.assertEqual(adjacent_layer_edges(layers, [(0, 1), (1, 2), (0, 2)]), [(0, 1), (1, 2)])

    def test_structural_embeddings_keep_legacy_matrix_contract(self):
        layers = [[0, 1], [2, 3]]
        node_to_index = {0: 0, 1: 1, 2: 2, 3: 3}
        embeddings = deterministic_layout_embeddings(
            layers, [(0, 2), (1, 3)], node_to_index
        )
        self.assertEqual(embeddings.shape, (4, 6))
        self.assertEqual(embeddings.dtype, np.float32)
        self.assertTrue(np.isfinite(embeddings).all())
        self.assertFalse(np.allclose(embeddings[0], embeddings[1]))

    @unittest.skipUnless(shutil.which("dot"), "Graphviz dot is not installed")
    def test_graphviz_order_preserves_fixed_layer_node_sets(self):
        layers = [[0, 1, 2], [3, 4, 5]]
        edges = [(0, 5), (1, 4), (2, 3)]
        ordered, elapsed = graphviz_fixed_layer_order(layers, edges, timeout_seconds=5)
        self.assertGreaterEqual(elapsed, 0.0)
        self.assertEqual(len(ordered), len(layers))
        for expected, actual in zip(layers, ordered):
            self.assertEqual(Counter(expected), Counter(actual))


if __name__ == "__main__":
    unittest.main()
