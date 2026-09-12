#!/usr/bin/env python3
"""Exercise sequential IFCN metadata and tile-to-QCA phase inheritance."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path


ROUTE = "(0,1): (0,0),(0,1),(1,1),(2,1),(2,0);"
ROUTE_TILES = {(0, 0), (0, 1), (1, 1), (2, 1), (2, 0)}


def tile_phase(x: int, y: int) -> int:
    # Canonical coarse routes advance horizontally and hold vertically.  This
    # makes every ordered route transition either hold or +1 (mod 4).
    return x % 4


def phase_map(tiles: set[tuple[int, int]]) -> str:
    entries = "\n".join(
        f"({x},{y}): {tile_phase(x, y)};" for x, y in sorted(tiles)
    )
    return (
        "#phase map\n"
        "### authoritative clock tile (x,y) : zero-based phase ###\n"
        f"{entries}\n"
        "#phase map\n"
    )


def layout(
    *,
    headers: list[str],
    directives: list[str],
    phases: set[tuple[int, int]] | None = None,
) -> str:
    header_text = "\n".join(headers)
    directive_text = "\n".join(directives)
    text = f"""#circuit name: sequential_metadata_fixture
{header_text}
#nodes info
### nodeIndex, nodeName, nodeType, nodePosition ###
0, input_a, Input, (0,0);
1, output_z, Output, (2,0);
#nodes info
#paths info
### {{node1, node2}} : path ###
{directive_text}
{ROUTE}
#paths info
"""
    if phases is not None:
        text += phase_map(phases)
    return text


def canonical_layout(*, distance: int = 0) -> str:
    return layout(
        headers=[
            "#mapping mode: sequential",
            "#phase granularity: tile",
        ],
        directives=[f"#iteration_distance={distance}"],
        phases=ROUTE_TILES,
    )


def sequential_crossing_layout() -> str:
    tiles = {(0, 1), (1, 1), (2, 1), (1, 0), (1, 2)}
    return (
        """#circuit name: sequential_layer_crossing_fixture
#mapping mode: sequential
#phase granularity: tile
#nodes info
### nodeIndex, nodeName, nodeType, nodePosition ###
0, input_a, input, (0,1);
1, output_z, output, (2,1);
2, input_b, input, (1,0);
3, output_y, output, (1,2);
#nodes info
#paths info
### {node1, node2} : path ###
#iteration_distance=0
(0,1): (0,1),(1,1),(2,1);
#iteration_distance=0
(2,3): (1,0),(1,1),(1,2);
#paths info
"""
        + phase_map(tiles)
    )


def sequential_gui_clock_scheme_layout() -> str:
    # A small cyclic layout that the sequential device mapper accepts.  Its
    # solved phases intentionally disagree with the legacy (x+y)%4 template.
    return """#circuit name: sequential_gui_clock_scheme_guard
#mapping mode: sequential
#phase granularity: tile
#clock scheme: 2ddwave
#nodes info
### nodeIndex, nodeName, nodeType, nodePosition ###
0, i0, input, (4,4);
2, d0, output, (4,7);
3, n0, or, (4,5);
4, ~n0, not, (4,6);
#nodes info
#paths info
### {node1, node2} : path ###
#iteration_distance=0
(0,3): (4,4),(4,5);
#iteration_distance=1
(2,3): (4,7),(3,7),(3,6),(3,5),(4,5);
#iteration_distance=0
(3,4): (4,5),(4,6);
#iteration_distance=0
(4,2): (4,6),(4,7);
#paths info
#phase map
### authoritative clock tile (x,y) : zero-based phase ###
(3,5): 3;
(3,6): 3;
(3,7): 2;
(4,4): 0;
(4,5): 0;
(4,6): 1;
(4,7): 1;
#phase map
"""


def write_case(root: Path, name: str, text: str) -> Path:
    input_path = root / f"{name}.ifcn"
    input_path.write_text(text, encoding="utf-8")
    return input_path


def with_physical_phase_map(
    text: str,
    entries: list[str],
    *,
    closing_marker: bool = True,
    opening_marker: str = "#physical phase map",
) -> str:
    lines = [text.rstrip(), opening_marker, *entries]
    if closing_marker:
        lines.append("#physical phase map")
    return "\n".join(lines) + "\n"


def qca_cells(qca_path: Path) -> list[tuple[int, int, int, int]]:
    layer_names = {
        "Main Cell Layer": 0,
        "second layer": 1,
        "third layer": 2,
    }
    result: list[tuple[int, int, int, int]] = []
    current_layer: int | None = None
    cell_lines: list[str] | None = None
    for line in qca_path.read_text(encoding="utf-8").splitlines():
        if line.startswith("pszDescription="):
            current_layer = layer_names.get(line.partition("=")[2], current_layer)
        elif line == "[TYPE:QCADCell]":
            assert current_layer is not None
            cell_lines = []
        elif line == "[#TYPE:QCADCell]":
            assert cell_lines is not None
            x_match = next(
                (
                    re.fullmatch(r"x=(\d+(?:\.\d+)?)", value)
                    for value in cell_lines
                    if value.startswith("x=")
                ),
                None,
            )
            y_match = next(
                (
                    re.fullmatch(r"y=(\d+(?:\.\d+)?)", value)
                    for value in cell_lines
                    if value.startswith("y=")
                ),
                None,
            )
            phase_match = next(
                (
                    re.fullmatch(r"cell_options\.clock=([0-3])", value)
                    for value in cell_lines
                    if value.startswith("cell_options.clock=")
                ),
                None,
            )
            assert x_match and y_match and phase_match
            world_x = float(x_match.group(1))
            world_y = float(y_match.group(1))
            fine_x = round((world_x - 200.0) / 20.0)
            fine_y = round((world_y - 200.0) / 20.0)
            assert world_x == fine_x * 20.0 + 200.0
            assert world_y == fine_y * 20.0 + 200.0
            result.append(
                (fine_x, fine_y, current_layer, int(phase_match.group(1)))
            )
            cell_lines = None
        elif cell_lines is not None:
            cell_lines.append(line)
    return result


def assert_tile_phase_inheritance(
    cells: list[tuple[int, int, int, int]],
) -> None:
    assert cells
    phases_by_tile: dict[tuple[int, int], set[int]] = defaultdict(set)
    for x, y, _layer, phase in cells:
        tile = (x // 5, y // 5)
        assert phase == tile_phase(*tile), (x, y, phase, tile)
        phases_by_tile[tile].add(phase)
    assert all(len(phases) == 1 for phases in phases_by_tile.values())


def expected_fine_phase(x: int, y: int, layer: int) -> int:
    return (x + 2 * y + layer) % 4


def run_metrics(binary: Path, input_path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), str(input_path), "--no-io-contraction"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def run_energy(
    binary: Path, input_path: Path, output_prefix: Path
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(binary), str(input_path), str(output_prefix), "--qca-only"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def run_gui_export(
    binary: Path, input_path: Path, output_path: Path
) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment.update(
        {
            "QT_QPA_PLATFORM": "offscreen",
            "IFCN_NONINTERACTIVE": "1",
            "IFCN_AUTO_MAP_FILE": str(input_path),
            "IFCN_AUTO_EXPORT_CELL_LAYOUT": str(output_path),
        }
    )
    return subprocess.run(
        [str(binary)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env=environment,
        timeout=20,
    )


def clock_zone_fill_counts(svg_path: Path) -> Counter[str]:
    svg = svg_path.read_text(encoding="utf-8")
    # QCADClockScheme draws each coarse clock tile as this 100x100 path.  Cell
    # bodies are 18x18, so this isolates the authoritative tile backgrounds.
    fills = re.findall(
        r'<g fill="(#[0-9a-fA-F]{6})"[^>]*>\s*'
        r'<path [^>]*d="M-50,-50 L50,-50 L50,50 L-50,50 L-50,-50"/>',
        svg,
    )
    return Counter(fill.lower() for fill in fills)


def assert_energy_rejected(
    binary: Path,
    root: Path,
    name: str,
    text: str,
    expected: str,
) -> None:
    input_path = write_case(root, name, text)
    result = run_energy(binary, input_path, root / f"{name}_rejected")
    assert result.returncode != 0, (name, result.stdout, result.stderr)
    assert expected in result.stderr.lower(), (name, result.stderr)


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: test_ifcn_sequential_mapping_metadata.py "
            "<ifcn_mapping_metrics> <ifcn_energy_analysis> <fcnx_gui>"
        )

    metrics_binary = Path(sys.argv[1]).resolve()
    energy_binary = Path(sys.argv[2]).resolve()
    gui_binary = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix="ifcn-sequential-metadata-") as tmp:
        root = Path(tmp)
        accepted = {
            "canonical": canonical_layout(distance=1),
            "canonical_zero": canonical_layout(distance=0),
            # No explicit mode/granularity: retain the two legacy sequential
            # inference paths, but still require complete tile phase data.
            "legacy_feedback": layout(
                headers=[],
                directives=["#iteration_distance=1"],
                phases=ROUTE_TILES,
            ),
            "legacy_cut": layout(
                headers=["#flow: sequential register-cut P&R v0"],
                directives=[],
                phases=ROUTE_TILES,
            ),
            "legacy_combinational": layout(
                headers=[], directives=[], phases=None
            ),
            "distance_alias": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#phase granularity: tile",
                ],
                directives=["#iteration distance: 1"],
                phases=ROUTE_TILES,
            ),
        }
        for name, text in accepted.items():
            input_path = write_case(root, name, text)
            metrics = run_metrics(metrics_binary, input_path)
            assert metrics.returncode == 0, (name, metrics.stdout, metrics.stderr)
            fields = metrics.stdout.split()
            assert len(fields) == 2 and all(field.isdigit() for field in fields), (
                name,
                metrics.stdout,
            )
            output_prefix = root / f"{name}_mapped"
            energy = run_energy(energy_binary, input_path, output_prefix)
            assert energy.returncode == 0, (name, energy.stdout, energy.stderr)
            qca_path = Path(f"{output_prefix}_energy_input.qca")
            assert qca_path.is_file() and qca_path.stat().st_size > 0
            if name != "legacy_combinational":
                assert_tile_phase_inheritance(qca_cells(qca_path))

        # A legacy combinational clock-scheme hint must never overwrite the
        # authoritative phase map of a sequential IFCN.  The chosen tile phases
        # deliberately differ from (x+y)%4; the old bug also synthesized an
        # extra rectangular-bbox tile, so both the phase histogram and tile
        # count catch it through the real offscreen GUI/export path.
        gui_input = write_case(
            root,
            "sequential_clock_scheme_guard",
            sequential_gui_clock_scheme_layout(),
        )
        gui_svg = root / "sequential_clock_scheme_guard.svg"
        gui_result = run_gui_export(gui_binary, gui_input, gui_svg)
        assert gui_result.returncode == 0, (
            gui_result.returncode,
            gui_result.stdout,
            gui_result.stderr,
        )
        assert gui_svg.is_file() and gui_svg.stat().st_size > 0
        assert clock_zone_fill_counts(gui_svg) == Counter(
            {
                "#f2f2f2": 2,  # phase 0
                "#dfdfdf": 2,  # phase 1
                "#bfbfbf": 1,  # phase 2
                "#808080": 2,  # phase 3
            }
        )

        # Sequential crossover topology remains layer-aware, but every L0/L1/L2
        # cell inherits the one phase assigned to its owning coarse tile.
        crossing_input = write_case(
            root, "sequential_tile_crossing", sequential_crossing_layout()
        )
        crossing_prefix = root / "sequential_tile_crossing_mapped"
        crossing_result = run_energy(
            energy_binary, crossing_input, crossing_prefix
        )
        assert crossing_result.returncode == 0, (
            crossing_result.stdout,
            crossing_result.stderr,
        )
        crossing_cells = qca_cells(
            Path(f"{crossing_prefix}_energy_input.qca")
        )
        assert {layer for _, _, layer, _ in crossing_cells} == {0, 1, 2}
        assert_tile_phase_inheritance(crossing_cells)
        phases_by_xy: dict[tuple[int, int], list[int]] = defaultdict(list)
        for x, y, _layer, phase in crossing_cells:
            phases_by_xy[(x, y)].append(phase)
        assert any(
            len(phases) == 3 and len(set(phases)) == 1
            for phases in phases_by_xy.values()
        ), phases_by_xy

        # Canonical sequential files fail closed on missing/ambiguous tile
        # clocks and categorically reject the stale per-QCA-cell contract.
        missing_tile = canonical_layout().replace("(1,1): 1;\n", "")
        illegal_jump = canonical_layout().replace("(1,1): 1;", "(1,1): 3;")
        five_same_phase_tiles = re.sub(
            r"(?m)^(\(\d+,\d+\): )\d(;)$",
            r"\g<1>0\2",
            canonical_layout(),
        )
        sequential_rejected = {
            "sequential_missing_tile_phase": (missing_tile, "tile phase map is missing"),
            "sequential_missing_phase_map": (
                layout(
                    headers=[
                        "#mapping mode: sequential",
                        "#phase granularity: tile",
                    ],
                    directives=["#iteration_distance=0"],
                    phases=None,
                ),
                "tile phase map is missing",
            ),
            "sequential_missing_granularity": (
                layout(
                    headers=["#mapping mode: sequential"],
                    directives=["#iteration_distance=0"],
                    phases=ROUTE_TILES,
                ),
                "phase granularity: tile",
            ),
            "sequential_qca_cell_granularity": (
                canonical_layout().replace(
                    "#phase granularity: tile",
                    "#phase granularity: qca_cell",
                ),
                "tile phase granularity",
            ),
            "sequential_bad_tile_phase": (
                canonical_layout().replace("(1,1): 1;", "(1,1): 4;"),
                "tile phase is out of range",
            ),
            "sequential_illegal_tile_phase_jump": (
                illegal_jump,
                "must be hold or +1 (mod 4)",
            ),
            "sequential_five_same_phase_tiles": (
                five_same_phase_tiles,
                "more than 4 consecutive same-phase tiles",
            ),
            "sequential_physical_map": (
                with_physical_phase_map(canonical_layout(), ["(2,2,0): 0;"]),
                "stale physical phase",
            ),
            "sequential_physical_trace": (
                "#physical phase trace: layer_aware_xyz\n"
                + with_physical_phase_map(
                    canonical_layout(), ["(2,2,0): 0;"]
                ),
                "stale physical phase",
            ),
            "sequential_trace_without_map": (
                "#physical phase trace: layer_aware_xyz\n" + canonical_layout(),
                "stale physical phase",
            ),
        }
        for name, (text, expected) in sequential_rejected.items():
            assert_energy_rejected(
                energy_binary, root, name, text, expected
            )

        # Per-cell maps remain a compatibility feature for combinational IFCN.
        physical_entries = [
            f"({x},{y},{layer}): {expected_fine_phase(x, y, layer)};"
            for x in range(15)
            for y in range(15)
            for layer in range(3)
        ]
        combinational = (
            layout(headers=[], directives=[], phases=ROUTE_TILES)
        )
        physical_input = write_case(
            root,
            "combinational_physical_phase_map",
            with_physical_phase_map(combinational, physical_entries),
        )
        physical_prefix = root / "combinational_physical_phase_map_mapped"
        physical_result = run_energy(
            energy_binary, physical_input, physical_prefix
        )
        assert physical_result.returncode == 0, (
            physical_result.stdout,
            physical_result.stderr,
        )
        physical_cells = qca_cells(
            Path(f"{physical_prefix}_energy_input.qca")
        )
        assert physical_cells
        assert all(
            phase == expected_fine_phase(x, y, layer)
            for x, y, layer, phase in physical_cells
        ), physical_cells

        # The legacy exact physical trace remains exact for combinational files.
        exact_site_phases = {
            (x, y, layer): phase for x, y, layer, phase in physical_cells
        }
        exact_entries = [
            f"({x},{y},{layer}): {phase};"
            for (x, y, layer), phase in sorted(exact_site_phases.items())
        ]
        exact_layout = (
            "#physical phase trace: layer_aware_xyz\n" + combinational
        )
        exact_input = write_case(
            root,
            "combinational_exact_trace",
            with_physical_phase_map(exact_layout, exact_entries),
        )
        exact_prefix = root / "combinational_exact_trace_mapped"
        exact_result = run_energy(energy_binary, exact_input, exact_prefix)
        assert exact_result.returncode == 0, (
            exact_result.stdout,
            exact_result.stderr,
        )
        assert {
            (x, y, layer): phase
            for x, y, layer, phase in qca_cells(
                Path(f"{exact_prefix}_energy_input.qca")
            )
        } == exact_site_phases
        for name, entries in {
            "combinational_exact_extra": [*exact_entries, "(999,999,0): 0;"],
            "combinational_exact_missing": exact_entries[1:],
        }.items():
            assert_energy_rejected(
                energy_binary,
                root,
                name,
                with_physical_phase_map(exact_layout, entries),
                "physical phase",
            )

        rejected = {
            "unknown_mode": layout(
                headers=["#mapping mode: sequential-v2"],
                directives=["#iteration_distance=0"],
            ),
            "conflicting_modes": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#mapping mode: combinational",
                ],
                directives=["#iteration_distance=0"],
            ),
            "combinational_feedback": layout(
                headers=["#mapping mode: combinational"],
                directives=["#iteration_distance=1"],
            ),
            "sequential_missing_distance": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#phase granularity: tile",
                ],
                directives=[],
                phases=ROUTE_TILES,
            ),
            "negative_distance": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#phase granularity: tile",
                ],
                directives=["#iteration_distance=-1"],
                phases=ROUTE_TILES,
            ),
            "duplicate_distance": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#phase granularity: tile",
                ],
                directives=["#iteration_distance=0", "#iteration_distance=1"],
                phases=ROUTE_TILES,
            ),
            "dangling_distance": canonical_layout().replace(
                f"{ROUTE}\n#paths info",
                f"{ROUTE}\n#iteration_distance=1\n#paths info",
            ),
            "overflow_distance": layout(
                headers=[
                    "#mapping mode: sequential",
                    "#phase granularity: tile",
                ],
                directives=["#iteration_distance=4294967296"],
                phases=ROUTE_TILES,
            ),
        }
        for name, text in rejected.items():
            input_path = write_case(root, name, text)
            metrics = run_metrics(metrics_binary, input_path)
            assert metrics.returncode != 0, (name, metrics.stdout, metrics.stderr)
            assert "invalid IFCN" in metrics.stderr, (name, metrics.stderr)
            energy = run_energy(
                energy_binary, input_path, root / f"{name}_rejected"
            )
            assert energy.returncode != 0, (name, energy.stdout, energy.stderr)
            assert "failed" in energy.stderr, (name, energy.stderr)

        physical_rejected = {
            "physical_conflicting_duplicate": with_physical_phase_map(
                combinational, ["(2,2,0): 0;", "(2,2,0): 1;"]
            ),
            "physical_phase_out_of_range": with_physical_phase_map(
                combinational, ["(2,2,0): 4;"]
            ),
            "physical_layer_out_of_range": with_physical_phase_map(
                combinational, ["(2,2,3): 0;"]
            ),
            "physical_malformed_entry": with_physical_phase_map(
                combinational, ["(2,2,0): 0"]
            ),
            "physical_unclosed_section": with_physical_phase_map(
                combinational, ["(2,2,0): 0;"], closing_marker=False
            ),
            "physical_malformed_section_marker": with_physical_phase_map(
                combinational,
                ["(2,2,0): 0;"],
                opening_marker="#physical phase map:",
            ),
            "physical_missing_mapped_cell": with_physical_phase_map(
                combinational, ["(0,0,0): 0;"]
            ),
            "physical_empty_section": with_physical_phase_map(
                combinational, []
            ),
        }
        for name, text in physical_rejected.items():
            assert_energy_rejected(
                energy_binary, root, name, text, "physical phase"
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
