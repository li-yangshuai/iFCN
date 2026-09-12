import os
import sys
import unittest

import numpy as np


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.layout_retrieval_memory import (  # noqa: E402
    LayoutRetrievalMemory,
    graph_descriptor_from_circuit,
    topology_hash_from_circuit,
)
from src.memory_policy_bridge import (  # noqa: E402
    remember_exact_layout,
    retrieve_policy_memory,
)


class FakeCircuit:
    effective_nodes = [10, 11, 12]
    effective_edges = [(10, 11), (11, 12)]

    @staticmethod
    def getNodeTypeString(node_id):
        return {10: "input", 11: "and", 12: "output"}[int(node_id)]


class FakeField:
    class Spec:
        phase_count = 4
        mode = "axis"
        primary_axis = "x"
        primary_direction = 1
        secondary_direction = 1

    spec = Spec()
    causal = True
    secondary_advance_ratio = 0.0

    @staticmethod
    def descriptor():
        return (1.0, 1.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0)


class FakeEnv:
    circuit = FakeCircuit()
    node_ids = [10, 11, 12]
    phase_cycle = 4
    max_same_phase = 4
    padding = 2
    orientation = "left-right"


class MemoryPolicyBridgeTest(unittest.TestCase):
    def test_exact_memory_produces_aligned_node_hints_and_can_be_excluded(self):
        env = FakeEnv()
        field = FakeField()
        memory = LayoutRetrievalMemory()
        entry_id = remember_exact_layout(
            memory,
            env,
            field,
            {10: (1, 2), 11: (3, 2), 12: (5, 2)},
            {"legal": 1.0, "area": 15.0, "failed_edges": 0.0},
        )
        self.assertTrue(entry_id)
        retrieved = retrieve_policy_memory(memory, env, field, top_k=2)
        self.assertEqual(retrieved.exact_count, 1)
        self.assertEqual(set(retrieved.node_hints), {10, 11, 12})
        self.assertTrue(np.isfinite(retrieved.features.numpy()).all())

        held_out = retrieve_policy_memory(
            memory,
            env,
            field,
            top_k=2,
            exclude_exact_topology=True,
        )
        self.assertEqual(held_out.exact_count, 0)
        self.assertFalse(held_out.node_hints)

    def test_empty_memory_returns_fixed_zero_context(self):
        env = FakeEnv()
        empty = retrieve_policy_memory(None, env, FakeField())
        self.assertEqual(tuple(empty.features.shape), (32,))
        self.assertEqual(float(empty.features.abs().sum()), 0.0)
        self.assertEqual(
            empty.topology_hash,
            topology_hash_from_circuit(env.circuit),
        )
        self.assertEqual(tuple(graph_descriptor_from_circuit(env.circuit).shape), (32,))


if __name__ == "__main__":
    unittest.main()
