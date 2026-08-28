#!/usr/bin/env python3
"""Regression checks for feedback-aware compact cyclic placement/routing."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: test_cyclic_compact_area_report.py <layout.ifcn.json>"
        )

    report_path = Path(sys.argv[1])
    report = json.loads(report_path.read_text(encoding="utf-8"))
    ifcn = report_path.with_suffix("").read_text(encoding="utf-8")
    optimization = report["geometry_optimization"]

    assert report["mapping_mode"] == "sequential"
    assert report["mapping_drc"] is True
    assert report["directed_cycle_present"] is True
    assert report["feedback_routes"] == 1
    # Clocking is assigned to ordered coarse tiles.  Fine QCA cells and
    # L0/L1/L2 crossover pillars inherit their enclosing tile's phase and do
    # not add latency occurrences.
    assert report["initiation_interval"] == 4
    assert report["bbox_area"] == report["bbox_width"] * report["bbox_height"]
    assert report["phase_granularity"] == "tile"
    assert report["tile_phase_drc_scope"] == "ordered_route_tiles"
    assert report["tile_phase_drc"] is True
    assert report["max_same_phase_tiles"] == 4
    assert 1 <= report["max_observed_same_phase_tile_run"] <= 4
    assert report["tile_clock_resources"] >= 1
    assert ifcn.count("#phase granularity: tile") == 1
    assert "#phase granularity: qca_cell" not in ifcn
    assert "#physical phase map" not in ifcn
    assert "#physical phase trace" not in ifcn
    stale_physical_clock_keys = {
        "physical_clock_resources",
        "max_same_phase_cells",
        "max_observed_same_phase_run",
        "physical_phase_drc",
    }
    assert stale_physical_clock_keys.isdisjoint(report)
    assert not any(key.startswith("physical_phase_") for key in report)

    phase_entries = re.findall(
        r"(?m)^\((\d+),(\d+)\):\s*([0-3]);$", ifcn
    )
    tile_phases = {
        (int(x), int(y)): int(phase) for x, y, phase in phase_entries
    }
    assert len(phase_entries) == len(tile_phases)
    assert len(tile_phases) == report["tile_clock_resources"]

    measured_max_run = 0
    route_texts = re.findall(
        r"(?m)^\(\d+,\d+\):\s*((?:\(\d+,\d+\),?)+);$", ifcn
    )
    assert len(route_texts) == report["routes"]
    for route_text in route_texts:
        route = [
            (int(x), int(y))
            for x, y in re.findall(r"\((\d+),(\d+)\)", route_text)
        ]
        assert route and all(tile in tile_phases for tile in route)
        run = 1
        measured_max_run = max(measured_max_run, run)
        for previous_tile, current_tile in zip(route, route[1:]):
            previous = tile_phases[previous_tile]
            current = tile_phases[current_tile]
            assert (current - previous) % 4 in (0, 1)
            run = run + 1 if current == previous else 1
            measured_max_run = max(measured_max_run, run)
            assert run <= report["max_same_phase_tiles"]
    assert measured_max_run == report["max_observed_same_phase_tile_run"]

    # Rank zero must remain the smallest tile-clock-feasible geometry. Its
    # timing constraint is independent of the number of QCA cells emitted
    # inside each tile.
    assert optimization["selected_rank"] == 0
    assert report["bbox_width"] <= 5
    assert report["bbox_height"] <= 7
    assert report["bbox_area"] <= 35
    assert report["mapped_unique_xy_sites"] <= 177
    assert report["mapped_unique_xy_sites"] == report["mapped_qca_cells"]
    assert report["mapped_layer_cell_records"] >= report["mapped_unique_xy_sites"]
    assert report["route_steps"] <= 32

    assert optimization["q_filtered_placement_candidates"] > 0
    assert optimization["legacy_placement_fallback_candidates"] > 0
    assert optimization["routing_window_templates"] > 0
    assert optimization["bounded_routing_attempts"] > 0
    assert optimization["unbounded_routing_attempts"] > 0
    assert optimization["compaction_seed_candidates"] == 16
    assert optimization["global_pnr_optimality_claimed"] is False
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
