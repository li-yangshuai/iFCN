import copy
import os
import sys
import tempfile
import unittest
from pathlib import Path


ALGORITHM_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../src/algorithm"))
if ALGORITHM_ROOT not in sys.path:
    sys.path.insert(0, ALGORITHM_ROOT)

from src.ifcn_layout_dataset import (  # noqa: E402
    assert_no_topology_leakage,
    build_ifcn_dataset,
    deduplicate_layouts,
    load_jsonl_index,
    pareto_frontier,
    parse_ifcn,
    save_jsonl_index,
    scan_ifcn,
    split_by_topology,
)


def raw_fixture(ids=(0, 1, 2), order=None, circuit="chain.v"):
    source, middle, target = ids
    node_rows = {
        source: f"{source}, in, input, (0,0);",
        middle: f"{middle}, gate, and, (1,1);",
        target: f"{target}, out, output, (2,2);",
    }
    node_order = list(ids if order is None else order)
    phase_rows = []
    for y in range(3):
        phase_rows.append(
            " ".join(f"({x},{y}):{(x + y) % 4};" for x in range(3))
        )
    return f"""#circuit name: {circuit}

#designed by unit test.
#gate level placement and routing infomation
#gates number: 3
#input/output: 1 / 1
#edges number: 2
#total layers: 3
#layout area: width: 3, height: 3, area: 9
#cell count: 8
#cross count: 0
#critical path: 4
#run time: 0.25 s
#phase count: 4
#clock scheme: 2DDWave
#clock scheme consistency: success
#clock scheme conflicts: 0

#nodes info
### nodeIndex, nodeName, nodeType, nodePosition ###
{chr(10).join(node_rows[node_id] for node_id in node_order)}
#nodes info

#paths info
### {{node1, node2}} : path ###
({source},{middle}): (0,0),(1,0),(1,1);
({middle},{target}): (1,1),(2,1),(2,2);
#paths info

#phase map
### (x,y) : phase ###
{chr(10).join(phase_rows)}
#phase map
"""


def packed_fixture(*, legacy=False):
    marker = "#phase map" if legacy else "#encoded phase map"
    tile = "tile(0,0):0xe4e4e4e4;" if legacy else "(0,0):0xe4e4e4e4;"
    codec = "#phase codec: phase_count=4, block_size=4, encoding=packed_hex_2bit_row_major\n"
    size = "" if legacy else "#raw size: width: 2, height: 1\n#padded size: width: 4, height: 4\n#blocks: columns: 1, rows: 1\n"
    return f"""#circuit name: packed.v
#gates number: 2
#input/output: 1 / 1
#edges number: 1
#total layers: 2
#layout area: width: 2, height: 1, area: 2
#phase count: 4
#block size: 4x4
{codec}{size}
#nodes info
### nodeIndex, nodeName, nodeType, nodePosition ###
10, a, input, (0,0);
20, z, output, (1,0);
#nodes info
#paths info
### {{node1, node2}} : path ###
(10,20): (0,0),(1,0);
#paths info
{marker}
### phase data ###
{tile}
{marker}
"""


class IFCNLayoutParserTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return path

    def test_raw_layout_yields_training_features_and_quality_report(self):
        layout = parse_ifcn(self.write("chain.ifcn", raw_fixture()))

        self.assertEqual(layout.circuit_name, "chain.v")
        self.assertEqual(len(layout.nodes), 3)
        self.assertEqual(len(layout.paths), 2)
        self.assertEqual(len(layout.raw_phase_map), 9)
        self.assertEqual(layout.phase_encoding, "raw")
        self.assertTrue(layout.quality.complete)
        self.assertTrue(layout.quality.valid_for_training)
        self.assertEqual(layout.quality.phase_coverage, 1.0)
        self.assertEqual(layout.quality.wirelength, 4.0)

        middle = next(node for node in layout.nodes if node.name == "gate")
        self.assertEqual(middle.relative_position, (1, 1))
        self.assertEqual(middle.normalized_position, (0.5, 0.5))
        first_path = layout.paths[0]
        self.assertEqual(first_path.directions, ("R", "D"))
        self.assertEqual(first_path.direction_counts, {"D": 1, "R": 1})
        self.assertEqual(first_path.waypoints, ((0, 0), (1, 0), (1, 1)))
        self.assertEqual(first_path.turns, 1)
        self.assertEqual(first_path.detour_ratio, 1.0)
        self.assertTrue(first_path.endpoint_match)

    def test_packed_block_and_legacy_tile_are_decoded(self):
        encoded = parse_ifcn(self.write("encoded.ifcn", packed_fixture()))
        legacy = parse_ifcn(self.write("legacy.ifcn", packed_fixture(legacy=True)))

        for layout in (encoded, legacy):
            self.assertEqual(layout.phase_encoding, "packed")
            self.assertIsNotNone(layout.packed_phase)
            self.assertEqual(layout.packed_phase.tile_count, 1)
            self.assertEqual(layout.packed_phase.block_size, 4)
            decoded = layout.effective_phase_map()
            self.assertEqual(decoded[(0, 0)], 0)
            self.assertEqual(decoded[(1, 0)], 1)
            self.assertTrue(layout.quality.valid_for_training)

        self.assertEqual(encoded.layout_hash, legacy.layout_hash)

    def test_topology_hash_ignores_node_ids_order_and_coordinates(self):
        original = parse_ifcn(self.write("first.ifcn", raw_fixture()))
        permuted_text = raw_fixture(ids=(91, 7, 42), order=(42, 91, 7))
        # Geometry is irrelevant to topology; IDs and declaration order differ.
        permuted_text = permuted_text.replace("(1,1);", "(1,1);")
        permuted = parse_ifcn(self.write("second.ifcn", permuted_text))

        self.assertEqual(original.topology_hash, permuted.topology_hash)
        self.assertNotEqual(original.source_path, permuted.source_path)

    def test_completeness_reports_declared_edge_and_phase_failures(self):
        broken = raw_fixture().replace("#edges number: 2", "#edges number: 3")
        broken = broken.replace("(2,2):0;", "")
        layout = parse_ifcn(self.write("broken.ifcn", broken))

        self.assertFalse(layout.quality.complete)
        self.assertFalse(layout.quality.valid_for_training)
        self.assertTrue(any("declared edges" in error for error in layout.quality.errors))
        self.assertLess(layout.quality.phase_coverage, 1.0)


class IFCNDatasetOperationsTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.base_path = self.root / "base.ifcn"
        self.base_path.write_text(raw_fixture(), encoding="utf-8")
        self.base = parse_ifcn(self.base_path)

    def tearDown(self):
        self.temporary.cleanup()

    def variant(self, name, *, area, wirelength, runtime=1.0):
        layout = copy.deepcopy(self.base)
        layout.source_path = str(self.root / name)
        layout.layout_hash = name
        layout.quality.area = float(area)
        layout.quality.wirelength = float(wirelength)
        layout.quality.crossings = 0.0
        layout.quality.cell_count = 10.0
        layout.quality.runtime = float(runtime)
        return layout

    def test_deduplication_and_per_topology_pareto_frontier(self):
        duplicate = copy.deepcopy(self.base)
        duplicate.source_path = str(self.root / "duplicate.ifcn")
        self.assertEqual(deduplicate_layouts([self.base, duplicate]), [self.base])

        compact = self.variant("compact", area=8, wirelength=8)
        balanced = self.variant("balanced", area=10, wirelength=4)
        dominated = self.variant("dominated", area=12, wirelength=6)
        frontier = pareto_frontier([dominated, compact, balanced])
        self.assertEqual({record.source_path for record in frontier}, {compact.source_path, balanced.source_path})

    def test_topology_split_is_deterministic_and_leak_free(self):
        records = []
        for topology_number in range(6):
            for replica in range(2):
                record = copy.deepcopy(self.base)
                record.source_path = f"topology-{topology_number}-replica-{replica}"
                record.topology_hash = f"topology-{topology_number}"
                records.append(record)

        first = split_by_topology(
            records, train_ratio=0.5, val_ratio=0.25, test_ratio=0.25, seed=19
        )
        second = split_by_topology(
            list(reversed(records)), train_ratio=0.5, val_ratio=0.25, test_ratio=0.25, seed=19
        )
        assert_no_topology_leakage(first)
        self.assertEqual(
            {name: [item.source_path for item in values] for name, values in first.items()},
            {name: [item.source_path for item in values] for name, values in second.items()},
        )
        self.assertEqual(sum(len(values) for values in first.values()), len(records))
        for split_records in first.values():
            topology_counts = {}
            for record in split_records:
                topology_counts[record.topology_hash] = topology_counts.get(record.topology_hash, 0) + 1
            self.assertTrue(all(count == 2 for count in topology_counts.values()))

    def test_jsonl_round_trip_preserves_hashes_geometry_and_phase(self):
        packed_path = self.root / "packed.ifcn"
        packed_path.write_text(packed_fixture(), encoding="utf-8")
        records = [self.base, parse_ifcn(packed_path)]
        index_path = save_jsonl_index(records, self.root / "index" / "layouts.jsonl")
        loaded = load_jsonl_index(index_path)

        self.assertEqual([record.topology_hash for record in loaded], [record.topology_hash for record in records])
        self.assertEqual([record.layout_hash for record in loaded], [record.layout_hash for record in records])
        self.assertEqual(loaded[0].paths[0].directions, records[0].paths[0].directions)
        self.assertEqual(loaded[1].effective_phase_map(), records[1].effective_phase_map())

    def test_scan_and_dataset_builder_filter_invalid_and_duplicates(self):
        nested = self.root / "nested"
        nested.mkdir()
        (nested / "duplicate.ifcn").write_text(raw_fixture(), encoding="utf-8")
        invalid = raw_fixture(circuit="invalid.v").replace("#edges number: 2", "#edges number: 9")
        (nested / "invalid.ifcn").write_text(invalid, encoding="utf-8")

        scanned = scan_ifcn(self.root)
        valid = scan_ifcn(self.root, valid_only=True)
        dataset = build_ifcn_dataset(self.root, pareto_only=False)
        self.assertEqual(len(scanned), 3)
        self.assertEqual(len(valid), 2)
        self.assertEqual(len(dataset), 1)


if __name__ == "__main__":
    unittest.main()
