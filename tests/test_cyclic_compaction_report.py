#!/usr/bin/env python3
"""Validate the compact cyclic P&R geometry report produced by CTest."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_cyclic_compaction_report.py "
            "<layout.ifcn.json> <layout.tex>"
        )

    report_path = Path(sys.argv[1])
    latex_path = Path(sys.argv[2])
    report = json.loads(report_path.read_text(encoding="utf-8"))
    ifcn_path = report_path.with_suffix("")
    ifcn = ifcn_path.read_text(encoding="utf-8")
    optimization = report["geometry_optimization"]

    assert report["mapping_mode"] == "sequential"
    assert ifcn.count("#mapping mode: sequential") == 1
    iteration_distances = [
        int(value)
        for value in re.findall(r"(?m)^#iteration_distance=(\d+)$", ifcn)
    ]
    route_lines = re.findall(r"(?m)^\(\d+,\d+\):\s+\(.*;$", ifcn)
    assert len(iteration_distances) == len(route_lines) == report["routes"]
    assert sum(distance > 0 for distance in iteration_distances) == report[
        "feedback_routes"
    ]
    assert "#mapped unique xy sites:" in ifcn
    assert report["mapped_unique_xy_sites"] == report["mapped_qca_cells"]
    assert report["phase_granularity"] == "tile"
    assert report["tile_phase_drc_scope"] == "ordered_route_tiles"
    assert report["tile_phase_drc"] is True
    assert report["max_same_phase_tiles"] == 4
    assert 1 <= report["max_observed_same_phase_tile_run"] <= 4
    assert report["tile_clock_resources"] >= 1
    assert ifcn.count("#phase granularity: tile") == 1
    assert ifcn.count("#tile phase drc scope: ordered_route_tiles") == 1
    assert ifcn.count("#max same phase tiles: 4") == 1
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

    route_tile_paths = []
    for route_text in re.findall(
        r"(?m)^\(\d+,\d+\):\s*((?:\(\d+,\d+\),?)+);$", ifcn
    ):
        route_tile_paths.append(
            [
                (int(x), int(y))
                for x, y in re.findall(r"\((\d+),(\d+)\)", route_text)
            ]
        )
    assert len(route_tile_paths) == report["routes"]
    measured_max_run = 0
    for route in route_tile_paths:
        assert route and all(tile in tile_phases for tile in route)
        run = 1
        measured_max_run = max(measured_max_run, run)
        for previous_tile, current_tile in zip(route, route[1:]):
            previous = tile_phases[previous_tile]
            current = tile_phases[current_tile]
            # A routed tile either holds its predecessor's phase or advances
            # by exactly one phase modulo four.
            assert (current - previous) % 4 in (0, 1)
            run = run + 1 if current == previous else 1
            measured_max_run = max(measured_max_run, run)
            assert run <= report["max_same_phase_tiles"]
    assert measured_max_run == report["max_observed_same_phase_tile_run"]

    assert report["mapping_drc"] is True
    assert report["directed_cycle_present"] is True
    assert report["feedback_routes"] == 1
    assert report["initiation_interval"] == 4
    assert report["crossover_segments"] >= 0
    assert optimization["objective"] == (
        "bbox_area_route_steps_max_dimension_perimeter"
    )
    assert optimization["placement_candidates"] >= 20
    assert optimization["q_filtered_placement_candidates"] >= 12
    assert optimization["legacy_placement_fallback_candidates"] == 8
    assert (
        optimization["q_filtered_placement_candidates"]
        + optimization["legacy_placement_fallback_candidates"]
        == optimization["placement_candidates"]
    )
    assert optimization["search_cost_candidates"] == 2
    assert optimization["routing_window_templates"] >= 1
    assert optimization["bounded_routing_attempts"] >= (
        optimization["routing_window_templates"] * 4
    )
    assert optimization["unbounded_routing_attempts"] == (
        optimization["placement_candidates"] * 52
    )
    assert optimization["bounded_routed_candidates"] >= 1
    assert optimization["unbounded_routed_candidates"] >= 1
    assert optimization["drc_valid_candidates"] >= 1
    assert optimization["routed_candidates"] >= optimization["drc_valid_candidates"]
    assert optimization["raw_distinct_candidates"] >= 1
    assert optimization["compaction_seed_candidates"] == min(
        16, optimization["raw_distinct_candidates"]
    )
    assert optimization["all_raw_candidates_compacted"] is (
        optimization["compaction_seed_candidates"]
        == optimization["raw_distinct_candidates"]
    )
    assert optimization["distinct_candidates"] >= 1
    assert optimization["distinct_candidates"] >= optimization[
        "raw_distinct_candidates"
    ]
    assert optimization["selected_rank"] == 0
    assert optimization["compaction_model"] == (
        "monotone_unit_cut_contraction"
    )
    assert optimization["compaction_seed_policy"] == (
        "top_lexicographic_geometry"
    )
    expected_scope = (
        "enumerated_routes_plus_monotone_seam_contractions"
        if optimization["all_raw_candidates_compacted"]
        else "top_ranked_enumerated_routes_plus_monotone_seam_contractions"
    )
    assert optimization["optimality_scope"] == expected_scope
    assert optimization["global_pnr_optimality_claimed"] is False
    assert optimization["compaction_states_explored"] >= optimization[
        "compaction_seed_candidates"
    ]
    assert optimization["compaction_legal_moves"] >= 1
    assert optimization["compaction_reduced_candidates"] >= 1
    assert optimization["compaction_proven_candidates"] == optimization[
        "compaction_seed_candidates"
    ]
    assert optimization["all_compaction_optimality_proven"] is True
    assert optimization["selected_seam_optimality_proven"] is True
    # The tight-window/unit-spacing router can now produce the 2x2 optimum
    # directly; seam removal is therefore optional for the selected rank.
    assert optimization["selected_rows_removed"] >= 0
    assert optimization["selected_columns_removed"] >= 0
    assert report["bbox_area"] == report["bbox_width"] * report["bbox_height"]

    # Two opposing physical nets cannot share one grid segment.  The smallest
    # valid embedding of this two-gate feedback fixture is therefore the 2x2
    # square found by the tight-window routing plus seam-closure search.
    assert report["bbox_width"] == 2
    assert report["bbox_height"] == 2
    assert report["bbox_area"] == 4
    assert report["route_steps"] == 4

    # The selected routing candidate and the shared chessboard must be restored
    # as one atomic snapshot.  A stale board from the final search attempt used
    # to translate the native TikZ output far outside the selected bounding box.
    latex = latex_path.read_text(encoding="utf-8")
    phase_tiles = re.findall(
        r"\\node\[c[1-4]\] at \((\d+),(\d+)\) \{\};", latex
    )
    assert phase_tiles
    rendered_x = [int(x) for x, _ in phase_tiles]
    rendered_y = [int(y) for _, y in phase_tiles]
    assert max(rendered_x) - min(rendered_x) + 1 == report["bbox_width"]
    assert min(rendered_y) == 0
    assert max(rendered_y) == report["bbox_height"] - 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
