#!/usr/bin/env python3
"""Packed IFCN phases must reach QCA unchanged in each mapping mode."""

from __future__ import annotations

import re
import importlib.util
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/python'))
from ifcn.layout_validation import export_native_ifcn


def layout(phase_count: int, block_size: int, mode: str) -> tuple[str, str]:
    headers = "" if mode == "legacy" else f"#mapping mode: {mode}\n"
    if mode == "sequential":
        headers += "#phase granularity: tile\n"
    distance = "#iteration_distance=0\n" if mode == "sequential" else ""
    body = f"""#circuit name: packed_phase_export
{headers}#nodes info
0, a, input, (0,0);
1, y, output, (5,0);
#nodes info
#paths info
{distance}(0,1): (0,0),(1,0),(2,0),(3,0),(4,0),(5,0);
#paths info
#phase map
"""
    codec = (f"#phase codec: phase_count={phase_count}, block_size={block_size}, "
             "encoding=packed_hex_2bit_row_major\n")
    packed, expanded = [], []
    for tile_x in range(2):
        rows = []
        for y in range(block_size):
            byte = 0
            for column in range(block_size):
                x = tile_x * block_size + column
                phase = (x + y) % phase_count
                byte |= phase << (2 * column)
                expanded.append(f"({x},{y}):{phase};\n")
            rows.append(f"{byte:02x}")
        packed.append(f"tile({tile_x},0):0x{''.join(rows)};\n")
    return (body + codec + "".join(packed) + "#phase map\n",
            body + "".join(expanded) + "#phase map\n")


def export(binary: Path, root: Path, name: str, text: str) -> tuple[subprocess.CompletedProcess[str], Path]:
    source = root / f"{name}.ifcn"
    source.write_text(text)
    prefix = root / name
    result = subprocess.run([str(binary), str(source), str(prefix), "--qca-only"],
                            text=True, capture_output=True, timeout=10)
    return result, root / f"{name}_energy_input.qca"


def production_writers(binary: Path, root: Path) -> list[Path]:
    """Exercise real writers without installing the optional layout extension."""
    spec = importlib.util.spec_from_file_location('ifcn_toolkit', ROOT /
        'include/layout_backend/src/algorithm/src/toolkit.py')
    toolkit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(toolkit)
    artifacts = []
    for count in (3, 4):
        points = [(x, 6) for x in range(5, 11)]
        candidate = {'phase_count': count,
            'nodes': [dict(id=i, name=name, type=kind, x=points[index][0], y=6,
                           is_input=i == 0, is_output=i == 1)
                      for i, name, kind, index in [(0, 'a', 'input', 0), (1, 'y', 'output', 5)]],
            'routes': [dict(source=0, target=1, path=points)],
            'cells': [dict(x=x, y=y, phase=(x + y) % count + 1) for x, y in points]}
        native = root / f'native_{count}.ifcn'
        assert export_native_ifcn(candidate, native, 'irregular') == (6, 1)
        parse = SimpleNamespace(fileName='writer_probe', effective_nodes=[0, 1],
            effective_nodes_num=2, effective_edges_num=1, InputNodesNum=1,
            OutputNodesNum=1, total_layers=2,
            getNodeName=lambda node: ('a', 'y')[node],
            getNodeTypeString=lambda node: ('input', 'output')[node])
        board = SimpleNamespace(nodePairRoutes={(0, 1): points},
            isPhaseEnabled=lambda: True, getPhase=lambda pos: sum(pos) % count)
        draw = SimpleNamespace(parse=parse, mapChessboard=board, phase_cycle=count,
            clock_scheme_name='2DDWave', clock_template_ok=True,
            get_node_coord=lambda node: points[node * 5])
        with patch.object(toolkit, '_estimate_critical_path_metrics', return_value=(5, 2)), \
             patch.object(toolkit, '_insert_or_update_mapping_metrics'):
            classic = Path(toolkit.generate_gate_level_mapping_file(
                draw, output_dir=str(root), filename_stem=f'classic_{count}', verbose=False))
        assert not list(root.glob('*_encoded.ifcn'))
        for name, artifact in [('native', native), ('classic', classic)]:
            text = artifact.read_text()
            assert 'block_size=4, encoding=packed_hex_2bit_row_major' in text
            assert '(0,0);' in text and '(5,0);' in text
            expanded = []
            tiles = list(re.finditer(r'tile\((\d+),(\d+)\):0x([0-9a-f]{8});', text))
            assert len(tiles) == 2
            for tile in tiles:
                tx, ty, code = int(tile[1]), int(tile[2]), int(tile[3], 16)
                for ly in range(4):
                    for lx in range(4):
                        x, y = tx * 4 + lx, ty * 4 + ly
                        phase = (code >> (8 * (3 - ly) + 2 * lx)) & 3
                        if x < 6 and y == 0:
                            assert phase == (x + 11) % count, (name, count, x, phase)
                            expanded.append(f'({x},{y}):{phase};\n')
                        else:
                            assert phase == 0, (name, count, 'nonzero padding', x, y)
            legacy = re.sub(r'#phase codec:[^\n]+\n', '', text)
            legacy = re.sub(r'tile\([^\n]+\n', '', legacy)
            legacy = legacy.replace('#phase map\n', '#phase map\n' + ''.join(expanded), 1)
            packed_result, packed_qca = export(binary, root, f'{name}_{count}_packed', text)
            expanded_result, expanded_qca = export(binary, root, f'{name}_{count}_expanded', legacy)
            assert packed_result.returncode == expanded_result.returncode == 0, (
                packed_result.stderr, expanded_result.stderr)
            assert packed_qca.read_bytes() == expanded_qca.read_bytes(), (name, count)
            artifacts.append(artifact)
        candidate['cells'][0]['phase'] = count + 1
        invalid = root / f'invalid_writer_{count}.ifcn'
        try:
            export_native_ifcn(candidate, invalid, 'irregular')
        except ValueError:
            pass
        else:
            raise AssertionError('Native writer accepted an invalid phase')
        assert not invalid.exists()
        draw.clock_scheme_name = 'USE'
        board.getPhase = lambda pos: count
        with patch.object(toolkit, '_estimate_critical_path_metrics', return_value=(5, 2)):
            try:
                toolkit.generate_gate_level_mapping_file(
                    draw, output_dir=str(root), filename_stem=f'invalid_classic_{count}', verbose=False)
            except ValueError:
                pass
            else:
                raise AssertionError('Classic writer accepted an invalid phase')
        assert not (root / f'invalid_classic_{count}.ifcn').exists()
    return artifacts


def historical_writers(binary: Path, root: Path) -> list[Path]:
    # These fixtures were produced by the actual _write_encoded_phase_mapping_file
    # implementation at commit 2fad954, with a nonzero layout origin. They retain
    # its bare block coordinates, several blocks per line, and 3x3/4x4 metadata.
    artifacts = []
    for count in (3, 4):
        text = (ROOT / f'tests/fixtures/routing/legacy_encoded_{count}_phase.ifcn').read_text()
        expanded = text.split('#encoded phase map', 1)[0]
        expanded += '#phase map\n'
        expanded += ''.join(f'({x},6):{(x + 6) % count};\n' for x in range(5, 11))
        expanded += '#phase map\n'
        packed_result, packed_qca = export(binary, root, f'historical_{count}_packed', text)
        expanded_result, expanded_qca = export(binary, root, f'historical_{count}_expanded', expanded)
        assert packed_result.returncode == expanded_result.returncode == 0, (
            packed_result.stderr, expanded_result.stderr)
        assert packed_qca.read_bytes() == expanded_qca.read_bytes(), ('historical', count)
        artifacts.extend([root / f'historical_{count}_packed.ifcn',
                          root / f'historical_{count}_expanded.ifcn'])
    # Canonical tile entries may also share a line, as accepted by older GUI
    # versions. Unknown metadata comments inside the phase section stay harmless.
    packed, expanded = layout(4, 4, 'legacy')
    packed = packed.replace(';\ntile', '; tile')
    packed = packed.replace('tile(0,0)', '#note: ignore (0,0):3; 0xbad\ntile(0,0)', 1)
    packed_result, packed_qca = export(binary, root, 'multiple_tiles_packed', packed)
    expanded_result, expanded_qca = export(binary, root, 'multiple_tiles_expanded', expanded)
    assert packed_result.returncode == expanded_result.returncode == 0, (
        packed_result.stderr, expanded_result.stderr)
    assert packed_qca.read_bytes() == expanded_qca.read_bytes()
    artifacts.append(root / 'multiple_tiles_packed.ifcn')
    return artifacts


def sequential_writers(binary: Path, root: Path, register_cut: Path, cyclic: Path) -> None:
    source = ROOT / 'tests/benchmarks_f/SEQUENTIAL/rtl_v/toggle_ff_cut.v'
    for name, writer, extra in [('register_cut', register_cut, ['--spacing', '5']),
                               ('cyclic', cyclic, ['--spacing', '2', '--route-search-cost', '80',
                                '--compaction-max-states', '256', '--compaction-seeds', '16'])]:
        artifact = root / (name + '.ifcn')
        result = subprocess.run([str(writer), str(source), str(artifact), '--state', 'd:q',
                                 '--ii', '4,8', *extra], capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stderr
        packed = artifact.read_text()
        assert '#phase codec: phase_count=4, block_size=4, encoding=packed_hex_2bit_row_major' in packed
        assert '#iteration_distance=' in packed
        if name == 'cyclic':
            assert '#iteration_distance=1' in packed
        expanded = re.sub(r'#phase codec:[^\n]+\n', '', packed)
        def unpack(match):
            tx, ty, code = int(match[1]), int(match[2]), int(match[3], 16)
            return '\n'.join(f'({4*tx+x},{4*ty+y}):{(code >> (8*(3-y)+2*x)) & 3};'
                             for y in range(4) for x in range(4))
        expanded = re.sub(r'tile\((\d+),(\d+)\):0x([0-9a-f]+);', unpack, expanded)
        packed_result, packed_qca = export(binary, root, name + '_packed', packed)
        expanded_result, expanded_qca = export(binary, root, name + '_expanded', expanded)
        assert packed_result.returncode == expanded_result.returncode == 0, (
            packed_result.stderr, expanded_result.stderr)
        assert packed_qca.read_bytes() == expanded_qca.read_bytes(), name


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    gui = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
    with tempfile.TemporaryDirectory(prefix="ifcn-packed-phase-") as directory:
        root = Path(directory)
        artifacts = production_writers(binary, root)
        artifacts.extend(historical_writers(binary, root))
        if len(sys.argv) > 4:
            sequential_writers(binary, root, Path(sys.argv[3]).resolve(), Path(sys.argv[4]).resolve())
        for mode, count, size in [("legacy", 4, 4), ("combinational", 3, 3),
                                  ("combinational", 3, 4),
                                  ("sequential", 4, 4)]:
            packed, expanded = layout(count, size, mode)
            result, packed_qca = export(binary, root, mode + "_packed", packed)
            assert result.returncode == 0, result.stderr
            result, expanded_qca = export(binary, root, mode + "_expanded", expanded)
            assert result.returncode == 0, result.stderr
            assert packed_qca.read_bytes() == expanded_qca.read_bytes(), mode
            # Independent coordinate expectations prevent two equally wrong
            # all-zero exports from satisfying the byte-equivalence check.
            cells = re.findall(r"\[TYPE:QCADCell\](.*?)\[#TYPE:QCADCell\]",
                               packed_qca.read_text(), re.S)
            assert cells
            observed = set()
            for cell in cells:
                x = float(re.search(r"^x=(.*)$", cell, re.M)[1])
                y = float(re.search(r"^y=(.*)$", cell, re.M)[1])
                phase = int(re.search(r"cell_options.clock=(\d+)", cell)[1])
                tile_x, tile_y = int((x - 200) // 100), int((y - 200) // 100)
                assert phase == (tile_x + tile_y) % count, (mode, x, y, phase)
                observed.add(phase)
            assert len(observed) == count, (mode, observed)

        packed, _ = layout(4, 4, "legacy")
        invalid = {
            "bad_hex": re.sub(r"tile\(0,0\):[^;]+;", "tile(0,0):not_hex;", packed),
            "missing_tile": re.sub(r"tile\(1,0\):[^;]+;\n", "", packed),
            "empty_map": re.sub(r"tile\([^)]+\):[^;]+;\n", "", packed),
            "bad_codec": packed.replace("block_size=4", "block_size=5"),
            "bad_encoding": packed.replace("packed_hex_2bit_row_major", "unknown"),
            "oversize_hex": re.sub(r'tile\(0,0\):[^;]+;', 'tile(0,0):0x100000000;', packed),
            "duplicate_tile": packed.replace('tile(1,0)', 'tile(0,0)'),
            "missing_encoding": packed.replace(', encoding=packed_hex_2bit_row_major', ''),
            "invalid_three_phase_value": packed.replace('phase_count=4', 'phase_count=3'),
        }
        legacy_three = (ROOT / 'tests/fixtures/routing/legacy_encoded_3_phase.ifcn').read_text()
        invalid.update({
            'legacy_bad_block_size': legacy_three.replace('3x3', '5x5'),
            'legacy_nonzero_unused_bits': legacy_three.replace('0x120000', '0x400000'),
            'legacy_duplicate_block': legacy_three.replace('(1,0):0x', '(0,0):0x'),
            'legacy_trailing_garbage': legacy_three.replace('0x120000;\n', '0x120000; garbage\n'),
        })
        for name, text in invalid.items():
            result, qca = export(binary, root, name, text)
            assert result.returncode != 0, (name, result.stdout)
            assert not qca.exists(), name
            assert "phase" in result.stderr, (name, result.stderr)
        if gui:
            for index, artifact in enumerate(artifacts):
                png = root / f'writer_{index}.png'
                environment = os.environ | dict(QT_QPA_PLATFORM='offscreen', IFCN_ENABLE_CONSOLE_LOG='1',
                    IFCN_UI_SCREENSHOT_INPUT=str(artifact), IFCN_UI_SCREENSHOT=str(png),
                    IFCN_UI_SCREENSHOT_VIEW='layout')
                result = subprocess.run([str(gui)], env=environment, text=True, capture_output=True, timeout=10)
                assert result.returncode == 0 and png.is_file(), result.stderr
                count_match = re.search(r'(?:native|classic|historical)_(3|4)', artifact.name)
                if count_match:
                    # Inspect actual GUI clock colors, not merely its exit status.
                    # The first six occupied tiles retain an offset of 11 epochs.
                    svg = root / f'writer_{index}.svg'
                    environment = os.environ | dict(QT_QPA_PLATFORM='offscreen', IFCN_ENABLE_CONSOLE_LOG='1',
                        IFCN_AUTO_MAP_FILE=str(artifact), IFCN_AUTO_EXPORT_CELL_LAYOUT=str(svg))
                    result = subprocess.run([str(gui)], env=environment, text=True, capture_output=True, timeout=10)
                    assert result.returncode == 0 and svg.is_file(), result.stderr
                    zones = re.findall(r'<g fill="(#[0-9a-fA-F]{6})"[^>]*transform="matrix\(([^)]+)\)"[^>]*>\s*'
                        r'<path [^>]*d="M-50,-50 L50,-50 L50,50 L-50,50 L-50,-50"/>', svg.read_text())
                    ordered = sorted((float(matrix.split(',')[-1]), float(matrix.split(',')[-2]), color.lower())
                                     for color, matrix in zones)
                    colors = ['#f2f2f2', '#dfdfdf', '#bfbfbf', '#808080']
                    count = int(count_match[1])
                    assert [zone[2] for zone in ordered[:6]] == [colors[(x + 11) % count] for x in range(6)], artifact
            for name in invalid:
                png = root / (name + '.png')
                environment = os.environ | dict(QT_QPA_PLATFORM='offscreen', IFCN_ENABLE_CONSOLE_LOG='1',
                    IFCN_UI_SCREENSHOT_INPUT=str(root / (name + '.ifcn')), IFCN_UI_SCREENSHOT=str(png),
                    IFCN_UI_SCREENSHOT_VIEW='layout')
                result = subprocess.run([str(gui)], env=environment, text=True, capture_output=True, timeout=10)
                assert result.returncode != 0 and not png.exists(), (name, result.stderr)
    print("actual native/classic/sequential writers, legacy files and GUI preserve packed/expanded QCA phases")


if __name__ == "__main__":
    main()
