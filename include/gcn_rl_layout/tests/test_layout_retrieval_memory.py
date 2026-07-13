import json
import os
import sys
import tempfile
import unittest

import numpy as np
import torch


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.layout_retrieval_memory import (  # noqa: E402
    DEFAULT_CLOCK_DESCRIPTOR_DIM,
    GRAPH_DESCRIPTOR_DIM,
    MEMORY_CONTEXT_DIM,
    LayoutMemoryEntry,
    LayoutRetrievalMemory,
    graph_descriptor_from_circuit,
    topology_hash_from_circuit,
)
from src.ifcn_layout_dataset import (  # noqa: E402
    IFCNLayout,
    IFCNNode,
    IFCNPath,
    IFCNQualityReport,
    topology_fingerprint,
)


def descriptor(axis, secondary=0.0):
    value = np.zeros(GRAPH_DESCRIPTOR_DIM, dtype=np.float32)
    value[int(axis)] = 1.0
    value[(int(axis) + 1) % GRAPH_DESCRIPTOR_DIM] = float(secondary)
    return value


def clock_descriptor(axis=0):
    value = np.zeros(DEFAULT_CLOCK_DESCRIPTOR_DIM, dtype=np.float32)
    value[int(axis) % DEFAULT_CLOCK_DESCRIPTOR_DIM] = 1.0
    return value


def make_entry(
    entry_id,
    topology_hash,
    graph,
    *,
    area=100.0,
    legal=True,
    placement=None,
    routes=None,
    clock=None,
):
    return LayoutMemoryEntry.create(
        entry_id=entry_id,
        topology_hash=topology_hash,
        graph_descriptor=graph,
        clock_descriptor=clock_descriptor() if clock is None else clock,
        normalized_placement=placement
        or {"in": (0.0, 0.0), "gate": (0.5, 0.5), "out": (1.0, 1.0)},
        route_hints=routes or {"net_order": ["in->gate", "gate->out"]},
        quality_metrics={
            "legal": float(legal),
            "failed_edges": 0.0 if legal else 1.0,
            "area": float(area),
        },
        metadata={"source": "unit-test"},
    )


class DescriptorTest(unittest.TestCase):
    def test_descriptor_and_hash_are_invariant_to_node_renaming(self):
        first = {
            "nodes": {
                10: {"type": "input", "layer": 0},
                11: {"type": "maj", "layer": 1},
                12: {"type": "output", "layer": 2},
            },
            "edges": [(10, 11), (11, 12)],
        }
        renamed = {
            "nodes": {
                "x": {"type": "input", "layer": 0},
                "y": {"type": "maj", "layer": 1},
                "z": {"type": "output", "layer": 2},
            },
            "edges": [("x", "y"), ("y", "z")],
        }

        first_descriptor = graph_descriptor_from_circuit(first)
        renamed_descriptor = graph_descriptor_from_circuit(renamed)
        self.assertEqual(first_descriptor.shape, (GRAPH_DESCRIPTOR_DIM,))
        self.assertTrue(np.allclose(first_descriptor, renamed_descriptor))
        self.assertEqual(
            topology_hash_from_circuit(first),
            topology_hash_from_circuit(renamed),
        )

    def test_explicit_learned_descriptor_is_supported(self):
        expected = descriptor(7, 0.25)
        actual = graph_descriptor_from_circuit({"graph_descriptor": torch.from_numpy(expected)})
        self.assertTrue(np.array_equal(actual, expected))

    def test_ifcn_layout_and_online_circuit_use_identical_topology_hash(self):
        nodes = (
            IFCNNode(1, "a", "Input", (0, 0), normalized_position=(0.0, 0.0)),
            IFCNNode(2, "m", "Maj", (1, 1), normalized_position=(0.5, 0.5)),
            IFCNNode(3, "z", "Output", (2, 2), normalized_position=(1.0, 1.0)),
        )
        paths = (
            IFCNPath(1, 2, ((0, 0), (1, 0), (1, 1))),
            IFCNPath(2, 3, ((1, 1), (2, 1), (2, 2))),
        )
        expected_hash = topology_fingerprint(nodes, paths)
        layout = IFCNLayout(
            source_path="memory-test.ifcn",
            header={},
            nodes=nodes,
            paths=paths,
            raw_phase_map={},
            packed_phase=None,
            topology_hash=expected_hash,
            layout_hash="layout-hash",
            quality=IFCNQualityReport(complete=True, valid_for_training=True),
        )
        online_record = {
            "nodes": {
                101: {"type": "Input", "layer": 0},
                202: {"type": "Maj", "layer": 1},
                303: {"type": "Output", "layer": 2},
            },
            "effective_edges": [(101, 202), (202, 303)],
        }

        layout_descriptor = graph_descriptor_from_circuit(layout)
        online_descriptor = graph_descriptor_from_circuit(online_record)
        self.assertEqual(layout_descriptor.shape, (GRAPH_DESCRIPTOR_DIM,))
        self.assertTrue(np.allclose(layout_descriptor, online_descriptor))
        self.assertEqual(topology_hash_from_circuit(layout), expected_hash)
        self.assertEqual(topology_hash_from_circuit(online_record), expected_hash)


class RetrievalTest(unittest.TestCase):
    def setUp(self):
        self.memory = LayoutRetrievalMemory()

    def test_exact_retrieval_prioritises_legality_then_quality(self):
        self.memory.extend(
            [
                make_entry("legal-large", "same", descriptor(0), area=100.0),
                make_entry("illegal-small", "same", descriptor(0), area=1.0, legal=False),
                make_entry("legal-small", "same", descriptor(0), area=60.0),
            ]
        )
        results = self.memory.retrieve_exact(
            "same",
            top_k=3,
            quality_weight=1.0,
            diversity_weight=0.0,
        )

        self.assertEqual(results[0].entry.entry_id, "legal-small")
        self.assertTrue(results[0].exact_topology)
        legal_scores = [item.quality_score for item in results if item.entry.quality_metrics["legal"]]
        illegal_score = next(
            item.quality_score for item in results if not item.entry.quality_metrics["legal"]
        )
        self.assertGreater(min(legal_scores), illegal_score)

    def test_cosine_top_k_and_exclude_hashes_prevent_leakage(self):
        nearest = make_entry("nearest-id", "held-out-topology", descriptor(0, 0.05))
        second = make_entry("second-id", "train-topology", descriptor(0, 0.3))
        distant = make_entry("distant-id", "other-topology", descriptor(4))
        self.memory.extend([nearest, second, distant])

        raw = self.memory.retrieve_similar(
            descriptor(0),
            top_k=3,
            quality_weight=0.0,
            diversity_weight=0.0,
        )
        self.assertEqual(raw[0].entry.entry_id, "nearest-id")

        topology_excluded = self.memory.retrieve_similar(
            descriptor(0),
            top_k=3,
            exclude_hashes={"held-out-topology"},
            quality_weight=0.0,
            diversity_weight=0.0,
        )
        self.assertNotIn("held-out-topology", {item.entry.topology_hash for item in topology_excluded})
        self.assertEqual(topology_excluded[0].entry.entry_id, "second-id")

        entry_excluded = self.memory.retrieve_similar(
            descriptor(0),
            top_k=3,
            exclude_hashes={"nearest-id"},
            quality_weight=0.0,
            diversity_weight=0.0,
        )
        self.assertNotIn("nearest-id", {item.entry.entry_id for item in entry_excluded})

    def test_mmr_selects_a_diverse_layout_instead_of_a_duplicate(self):
        duplicate_placement = {"0": (0.0, 0.0), "1": (0.5, 0.5), "2": (1.0, 1.0)}
        diverse_placement = {"0": (1.0, 0.0), "1": (-0.7, 0.8), "2": (0.0, -1.0)}
        self.memory.extend(
            [
                make_entry("a", "topology", descriptor(0), placement=duplicate_placement),
                make_entry("b", "topology", descriptor(0), placement=duplicate_placement),
                make_entry(
                    "c",
                    "topology",
                    descriptor(0),
                    placement=diverse_placement,
                    routes={"corridors": [["south", "east"], ["north", "west"]]},
                ),
            ]
        )
        results = self.memory.retrieve_exact(
            "topology",
            top_k=2,
            quality_weight=0.0,
            diversity_weight=1.0,
        )
        self.assertEqual([item.entry.entry_id for item in results], ["a", "c"])

    def test_exact_first_query_fills_remaining_slots_with_similar_topologies(self):
        self.memory.extend(
            [
                make_entry("exact", "query", descriptor(2)),
                make_entry("neighbour", "near", descriptor(2, 0.1)),
                make_entry("far", "far", descriptor(9)),
            ]
        )
        results = self.memory.retrieve(
            descriptor(2),
            topology_hash="query",
            top_k=2,
            quality_weight=0.0,
            diversity_weight=0.0,
        )
        self.assertEqual([result.entry.entry_id for result in results], ["exact", "neighbour"])
        self.assertEqual([result.exact_topology for result in results], [True, False])


class PersistenceAndContextTest(unittest.TestCase):
    def test_incremental_add_is_idempotent_and_json_round_trips(self):
        memory = LayoutRetrievalMemory()
        entry = make_entry("stable-id", "alpha", descriptor(1), area=42.0)
        self.assertEqual(memory.add(entry), "stable-id")
        self.assertEqual(memory.add(entry), "stable-id")
        self.assertEqual(len(memory), 1)

        changed = make_entry("stable-id", "alpha", descriptor(1), area=41.0)
        with self.assertRaises(ValueError):
            memory.add(changed)
        memory.add(changed, replace_existing=True)

        with tempfile.TemporaryDirectory() as temporary_directory:
            path = os.path.join(temporary_directory, "layout-memory.json")
            returned_path = memory.save(path)
            self.assertEqual(os.fspath(returned_path), path)
            with open(path, "r", encoding="utf-8") as handle:
                payload = json.load(handle)
            self.assertEqual(payload["entries"][0]["entry_id"], "stable-id")
            restored = LayoutRetrievalMemory.load(path)

        self.assertEqual(restored.to_dict(), memory.to_dict())
        self.assertEqual(restored.topology_count, 1)
        self.assertEqual(restored.entries[0].quality_metrics["area"], 41.0)

    def test_retrieve_context_is_fixed_size_finite_and_has_torch_adapter(self):
        memory = LayoutRetrievalMemory()
        empty_context = memory.aggregate_context([])
        self.assertEqual(empty_context.shape, (MEMORY_CONTEXT_DIM,))
        self.assertTrue(np.array_equal(empty_context, np.zeros(MEMORY_CONTEXT_DIM)))

        memory.extend(
            [
                make_entry("one", "one-topology", descriptor(3), area=20.0),
                make_entry(
                    "two",
                    "two-topology",
                    descriptor(3, 0.2),
                    area=30.0,
                    clock=clock_descriptor(2),
                ),
            ]
        )
        context, results = memory.retrieve_context(
            descriptor(3),
            clock_descriptor=clock_descriptor(0),
            topology_hash="unseen",
            top_k=2,
        )
        self.assertEqual(context.shape, (MEMORY_CONTEXT_DIM,))
        self.assertEqual(context.dtype, np.float32)
        self.assertTrue(np.isfinite(context).all())
        self.assertEqual(context[0], 1.0)
        self.assertEqual(len(results), 2)

        tensor = memory.context_tensor(results, dtype=torch.float64)
        self.assertEqual(tuple(tensor.shape), (MEMORY_CONTEXT_DIM,))
        self.assertEqual(tensor.dtype, torch.float64)
        self.assertTrue(torch.isfinite(tensor).all())

    def test_dimension_and_finite_value_validation(self):
        memory = LayoutRetrievalMemory()
        bad_dimension = make_entry("bad-dim", "x", descriptor(0), area=1.0)
        bad_dimension = LayoutMemoryEntry.create(
            entry_id=bad_dimension.entry_id,
            topology_hash=bad_dimension.topology_hash,
            graph_descriptor=[1.0, 0.0],
            clock_descriptor=bad_dimension.clock_descriptor,
            normalized_placement=bad_dimension.normalized_placement,
            route_hints=bad_dimension.route_hints,
            quality_metrics=bad_dimension.quality_metrics,
        )
        with self.assertRaises(ValueError):
            memory.add(bad_dimension)
        with self.assertRaises(ValueError):
            memory.retrieve_similar(np.full(GRAPH_DESCRIPTOR_DIM, np.nan))


if __name__ == "__main__":
    unittest.main()
